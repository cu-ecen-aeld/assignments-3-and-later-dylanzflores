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

void signal_handler(int sig) {
    (void)sig;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

int setup_socket(void) {
    struct addrinfo hints = {0}, *res = NULL;
    int sockfd, yes = 1;

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
    int thread_complete;
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

    // Receive data and append to file, mutex protected
    pthread_mutex_lock(&file_mutex);
    data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (data_fd == -1) {
        perror("open for append");
        syslog(LOG_ERR, "Failed to open %s for append: %s", DATA_FILE, strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        close(client_fd);
        thread->thread_complete = 1;
        return NULL;
    }

    // Read client data until newline or connection closed
    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        // Get current time and write timestamp before data
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timestr[128];
        if (strftime(timestr, sizeof(timestr),
                     "timestamp:%a, %d %b %Y %H:%M:%S %z\n", &timeinfo) == 0) {
            syslog(LOG_ERR, "strftime failed");
        } else {
            ssize_t ts_len = strlen(timestr);
            ssize_t ts_written = write(data_fd, timestr, ts_len);
            if (ts_written != ts_len) {
                syslog(LOG_ERR, "Failed to write full timestamp");
            }
        }

        // Write the received data
        ssize_t bytes_written = write(data_fd, buffer, bytes_read);
        if (bytes_written == -1) {
            perror("write");
            syslog(LOG_ERR, "Write error: %s", strerror(errno));
            close(data_fd);
            pthread_mutex_unlock(&file_mutex);
            close(client_fd);
            thread->thread_complete = 1;
            return NULL;
        }

        // Append newline if data doesn't end with one
        if (buffer[bytes_read - 1] != '\n') {
            if (write(data_fd, "\n", 1) == -1) {
                syslog(LOG_ERR, "Failed to write newline after client data");
            }
        }

        // Logging for debug
        if (bytes_read < (ssize_t)sizeof(buffer)) {
            buffer[bytes_read] = '\0'; // null terminate safely
            syslog(LOG_DEBUG, "Received and wrote: %s", buffer);
        }

        if (memchr(buffer, '\n', bytes_read)) {
            break;  // Stop reading after newline
        }
    }

    if (bytes_read == -1) {
        perror("recv");
        syslog(LOG_ERR, "Receive error: %s", strerror(errno));
    }

    close(data_fd);
    pthread_mutex_unlock(&file_mutex);

    // Send back full file contents, mutex protected
    pthread_mutex_lock(&file_mutex);
    data_fd = open(DATA_FILE, O_RDONLY);
    if (data_fd == -1) {
        perror("open for read");
        syslog(LOG_ERR, "Failed to open %s for reading: %s", DATA_FILE, strerror(errno));
        pthread_mutex_unlock(&file_mutex);
        close(client_fd);
        thread->thread_complete = 1;
        return NULL;
    }

    while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t ret = send(client_fd, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            if (ret == -1) {
                perror("send");
                syslog(LOG_ERR, "Send failed: %s", strerror(errno));
                close(data_fd);
                pthread_mutex_unlock(&file_mutex);
                close(client_fd);
                thread->thread_complete = 1;
                return NULL;
            }
            bytes_sent += ret;
        }
    }

    if (bytes_read == -1) {
        perror("read");
        syslog(LOG_ERR, "Error reading file: %s", strerror(errno));
    }

    close(data_fd);
    pthread_mutex_unlock(&file_mutex);

    close(client_fd);
    thread->thread_complete = 1;
    return NULL;
}

void *timestamp_thread_func(void *arg) {
    (void)arg;
    char timestamp[128];
    time_t now;
    struct tm timeinfo;
    int data_fd;

    while (!exit_requested) {
        sleep(10);

        time(&now);
        localtime_r(&now, &timeinfo);

        if (strftime(timestamp, sizeof(timestamp),
                     "timestamp:%a, %d %b %Y %H:%M:%S %z\n", &timeinfo) == 0) {
            syslog(LOG_ERR, "strftime failed");
            continue;
        }

        pthread_mutex_lock(&file_mutex);
        data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (data_fd == -1) {
            syslog(LOG_ERR, "Failed to open %s for append: %s", DATA_FILE, strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            continue;
        }

        size_t len = strlen(timestamp);
        size_t total_written = 0;
        while (total_written < len) {
            ssize_t written = write(data_fd, timestamp + total_written, len - total_written);
            if (written == -1) {
                syslog(LOG_ERR, "Failed to write timestamp: %s", strerror(errno));
                break;
            }
            total_written += written;
        }

        close(data_fd);
        pthread_mutex_unlock(&file_mutex);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    pthread_t timestamp_thread;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;//SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    int sockfd = setup_socket();
    if (sockfd == -1) {
        syslog(LOG_ERR, "Failed to set up socket");
        exit(EXIT_FAILURE);
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        if (setsid() == -1 || chdir("/") == -1) exit(EXIT_FAILURE);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    if (remove(DATA_FILE) == -1 && errno != ENOENT) {
        syslog(LOG_ERR, "Failed to remove %s: %s", DATA_FILE, strerror(errno));
    }

    if (pthread_create(&timestamp_thread, NULL, timestamp_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (errno == EINTR && exit_requested) {
                break;
            }
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        thread_info_t *new_thread = malloc(sizeof(thread_info_t));
        if (!new_thread) {
            syslog(LOG_ERR, "Failed to allocate memory for thread_info");
            close(client_fd);
            continue;
        }

        new_thread->client_fd = client_fd;
        new_thread->thread_complete = 0;
        new_thread->next = NULL;

        if (pthread_create(&new_thread->thread_id, NULL, handle_client, new_thread) != 0) {
            syslog(LOG_ERR, "Failed to create client thread");
            close(client_fd);
            free(new_thread);
            continue;
        }

        pthread_mutex_lock(&thread_list_mutex);
        new_thread->next = thread_list;
        thread_list = new_thread;
        pthread_mutex_unlock(&thread_list_mutex);

        // Cleanup finished threads
        pthread_mutex_lock(&thread_list_mutex);
        thread_info_t **curr = &thread_list;
        while (*curr) {
            if ((*curr)->thread_complete) {
                pthread_join((*curr)->thread_id, NULL);
                thread_info_t *to_delete = *curr;
                *curr = (*curr)->next;
                // client_fd already closed inside handle_client
                free(to_delete);
            } else {
                curr = &((*curr)->next);
            }
        }
        pthread_mutex_unlock(&thread_list_mutex);
    }

    pthread_join(timestamp_thread, NULL);

    // Final cleanup: join remaining client threads
    pthread_mutex_lock(&thread_list_mutex);
    thread_info_t *curr = thread_list;
    while (curr) {
        pthread_join(curr->thread_id, NULL);
        thread_info_t *to_delete = curr;
        curr = curr->next;
        free(to_delete);
    }
    pthread_mutex_unlock(&thread_list_mutex);

    close(sockfd);
    remove(DATA_FILE);
    closelog();

    return 0;
}

