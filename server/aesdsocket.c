#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
int sockfd = -1;

void signal_handler(int sig) {
    (void)sig;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;

    if (sockfd != -1) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }
}

int setup_socket(void) {
    struct addrinfo hints = {0}, *res = NULL;
    int yes = 1;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

typedef struct thread_info {
    pthread_t thread_id;
    int client_fd;
    struct thread_info *next;
} thread_info_t;

thread_info_t *thread_list = NULL;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg) {
    thread_info_t *thread = (thread_info_t *)arg;
    int client_fd = thread->client_fd;

    char buffer[1024];
    ssize_t bytes_read;
    int data_fd;

    // Accumulate data from client until a newline
    size_t total_len = 0;
    char *msg_buf = NULL;

    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        char *new_buf = realloc(msg_buf, total_len + bytes_read);
        if (!new_buf) {
            free(msg_buf);
            close(client_fd);
            return NULL;
        }
        msg_buf = new_buf;
        memcpy(msg_buf + total_len, buffer, bytes_read);
        total_len += bytes_read;

        if (memchr(buffer, '\n', bytes_read)) break; // stop at first newline
    }

    if (total_len > 0) {
        pthread_mutex_lock(&file_mutex);
        data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (data_fd != -1) {
            ssize_t written = 0;
            while (written < total_len) {
                ssize_t w = write(data_fd, msg_buf + written, total_len - written);
                if (w == -1) {
                    syslog(LOG_ERR, "Write error: %s", strerror(errno));
                    break;
                }
                written += w;
            }
            close(data_fd);
        } else {
            syslog(LOG_ERR, "Failed to open %s for append: %s", DATA_FILE, strerror(errno));
        }
        pthread_mutex_unlock(&file_mutex);
    }

    free(msg_buf);

    // Send full file contents back to client
    pthread_mutex_lock(&file_mutex);
    data_fd = open(DATA_FILE, O_RDONLY);
    if (data_fd != -1) {
        while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
            ssize_t sent = 0;
            while (sent < bytes_read) {
                ssize_t s = send(client_fd, buffer + sent, bytes_read - sent, 0);
                if (s == -1) {
                    syslog(LOG_ERR, "Send failed: %s", strerror(errno));
                    break;
                }
                sent += s;
            }
        }
        close(data_fd);
    } else {
        syslog(LOG_ERR, "Failed to open %s for read: %s", DATA_FILE, strerror(errno));
    }
    pthread_mutex_unlock(&file_mutex);

    close(client_fd);
    return NULL;
}

void *timestamp_thread_func(void *arg) {
    (void)arg;
    while (!exit_requested) {
        for (int i = 0; i < 10 && !exit_requested; i++) sleep(1);

        if (exit_requested) break;

        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        char ts[128];
        strftime(ts, sizeof(ts), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", &t);

        pthread_mutex_lock(&file_mutex);
        int fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd != -1) {
            write(fd, ts, strlen(ts));
            close(fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    pthread_t ts_thread;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sockfd = setup_socket();
    if (sockfd == -1) exit(EXIT_FAILURE);

    if (daemon_mode) {
        if (fork() > 0) exit(EXIT_SUCCESS);
        setsid();
        chdir("/");
        close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
    }

    remove(DATA_FILE);

    pthread_create(&ts_thread, NULL, timestamp_thread_func, NULL);

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            continue;
        }

        thread_info_t *t = malloc(sizeof(thread_info_t));
        t->client_fd = client_fd;
        t->next = NULL;

        pthread_create(&t->thread_id, NULL, handle_client, t);

        pthread_mutex_lock(&thread_list_mutex);
        t->next = thread_list;
        thread_list = t;
        pthread_mutex_unlock(&thread_list_mutex);

        // Clean up finished threads
        pthread_mutex_lock(&thread_list_mutex);
        thread_info_t **curr = &thread_list;
        while (*curr) {
            int status = pthread_tryjoin_np((*curr)->thread_id, NULL);
            if (status == 0) {
                thread_info_t *tmp = *curr;
                *curr = tmp->next;
                free(tmp);
            } else {
                curr = &(*curr)->next;
            }
        }
        pthread_mutex_unlock(&thread_list_mutex);
    }

    pthread_join(ts_thread, NULL);

    pthread_mutex_lock(&thread_list_mutex);
    thread_info_t *curr = thread_list;
    while (curr) {
        pthread_join(curr->thread_id, NULL);
        thread_info_t *tmp = curr;
        curr = curr->next;
        free(tmp);
    }
    pthread_mutex_unlock(&thread_list_mutex);

    close(sockfd);
    remove(DATA_FILE);
    closelog();
    return 0;
}

