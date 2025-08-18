#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

// Global flag set by signal handler
volatile sig_atomic_t g_exit_signal_received = 0;

// Signal handler to set termination flag
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_exit_signal_received = 1;
        syslog(LOG_INFO, "Caught signal %s, exiting", (sig == SIGINT) ? "SIGINT" : "SIGTERM");
    }
}

// Set up signal handling
static void setup_signals() {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Failed to set up signal handling");
        exit(EXIT_FAILURE);
    }
}

// Create and bind a listening TCP socket
static int create_listening_socket() {
    int sockfd;
    struct sockaddr_in addr = {0};
    int opt = 1;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// Run the process in daemon mode
static void daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() == -1) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") == -1) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

// Receive, write, and echo data
static void handle_client(int client_fd, struct sockaddr_in *client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    char recv_buf[BUFFER_SIZE];
    char *packet_buf = NULL;
    size_t packet_size = 0;
    size_t packet_len = 0;
    ssize_t bytes_received;

    while (!g_exit_signal_received &&
           (bytes_received = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {

        if (packet_len + bytes_received > packet_size) {
            size_t new_size = packet_len + bytes_received;
            char *temp = realloc(packet_buf, new_size);
            if (!temp) {
                syslog(LOG_ERR, "realloc failed: %s", strerror(errno));
                free(packet_buf);
                return;
            }
            packet_buf = temp;
            packet_size = new_size;
        }

        memcpy(packet_buf + packet_len, recv_buf, bytes_received);
        packet_len += bytes_received;

        char *newline = memchr(packet_buf, '\n', packet_len);
        if (!newline) continue;

        size_t complete_len = newline - packet_buf + 1;

        int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            syslog(LOG_ERR, "open failed: %s", strerror(errno));
            break;
        }

        if (write(fd, packet_buf, complete_len) == -1) {
            syslog(LOG_ERR, "write failed: %s", strerror(errno));
        }
        close(fd);

        fd = open(DATA_FILE, O_RDONLY);
        if (fd == -1) {
            syslog(LOG_ERR, "open for reading failed: %s", strerror(errno));
            break;
        }

        ssize_t read_bytes;
        while (!g_exit_signal_received &&
               (read_bytes = read(fd, recv_buf, sizeof(recv_buf))) > 0) {
            ssize_t sent = 0;
            while (sent < read_bytes) {
                ssize_t n = send(client_fd, recv_buf + sent, read_bytes - sent, 0);
                if (n == -1) {
                    syslog(LOG_ERR, "send failed: %s", strerror(errno));
                    break;
                }
                sent += n;
            }
        }

        close(fd);

        size_t remaining = packet_len - complete_len;
        memmove(packet_buf, newline + 1, remaining);
        packet_len = remaining;
    }

    if (bytes_received == -1 && errno != EINTR) {
        syslog(LOG_ERR, "recv failed: %s", strerror(errno));
    }

    free(packet_buf);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

// Entry point
int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
            break;
        }
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    setup_signals();

    int listen_fd = create_listening_socket();
    if (listen_fd == -1) {
        closelog();
        return EXIT_FAILURE;
    }

    if (daemon_mode) {
        daemonize();
    }

    syslog(LOG_INFO, "Listening on port %d", PORT);

    while (!g_exit_signal_received) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        handle_client(client_fd, &client_addr);
    }

    close(listen_fd);

    if (unlink(DATA_FILE) == -1) {
        syslog(LOG_ERR, "Failed to delete %s: %s", DATA_FILE, strerror(errno));
    } else {
        syslog(LOG_INFO, "Deleted file %s", DATA_FILE);
    }

    closelog();
    return EXIT_SUCCESS;
}

