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

#define PORT "9000"
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;

void signal_handler(int sig) {
    (void)sig;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

int setup_socket() {
    struct addrinfo hints, *res;
    int sockfd, yes = 1;

    memset(&hints, 0, sizeof hints);
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

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
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

void handle_client(int client_fd) {
    char buffer[1024];
    ssize_t bytes_read;
    int data_fd;

    data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (data_fd == -1) {
        perror("open for append");
        syslog(LOG_ERR, "Failed to open %s for append: %s", DATA_FILE, strerror(errno));
        return;
    }

    // Receive data from client and write to file
    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
        ssize_t bytes_written = write(data_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            syslog(LOG_ERR, "Failed to write all bytes to %s", DATA_FILE);
            close(data_fd);
            return;
        }
        fsync(data_fd);  // Ensure data is flushed

        syslog(LOG_DEBUG, "Received and wrote %zd bytes", bytes_read);

        // Stop receiving if newline received
        if (memchr(buffer, '\n', bytes_read)) {
            break;
        }
        // Clear buffer for next recv (optional)
        memset(buffer, 0, sizeof(buffer));
    }

    if (bytes_read == -1) {
        perror("recv");
        syslog(LOG_ERR, "Error during recv: %s", strerror(errno));
        close(data_fd);
        return;
    }

    close(data_fd);

    // Open file for reading to send back to client
    data_fd = open(DATA_FILE, O_RDONLY);
    if (data_fd == -1) {
        perror("open for read");
        syslog(LOG_ERR, "Failed to open %s for reading: %s", DATA_FILE, strerror(errno));
        return;
    }

    // Read file and send contents to client
    while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t ret = send(client_fd, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            if (ret == -1) {
                perror("send");
                syslog(LOG_ERR, "Send failed: %s", strerror(errno));
                close(data_fd);
                return;
            }
            bytes_sent += ret;
        }
        syslog(LOG_DEBUG, "Sent %zd bytes to client", bytes_sent);
        memset(buffer, 0, sizeof(buffer));
    }

    if (bytes_read == -1) {
        perror("read");
        syslog(LOG_ERR, "Error reading file: %s", strerror(errno));
    }

    close(data_fd);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // No SA_RESTART so accept() gets interrupted
    if (sigaction(SIGINT, &sa, NULL) == -1) {
    	perror("sigaction SIGINT");
    	exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
    	perror("sigaction SIGTERM");
    	exit(EXIT_FAILURE);
    }
    

    int sockfd = setup_socket();
    if (sockfd == -1) {
        syslog(LOG_ERR, "Failed to set up socket");
        exit(EXIT_FAILURE);
    }

    if (daemon_mode) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    if (setsid() == -1) exit(EXIT_FAILURE);
    chdir("/");

    if (remove(DATA_FILE) == -1 && errno != ENOENT) {
        perror("remove");
        syslog(LOG_ERR, "Failed to remove %s: %s", DATA_FILE, strerror(errno));
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
} else {
    if (remove(DATA_FILE) == -1 && errno != ENOENT) {
        perror("remove");
        syslog(LOG_ERR, "Failed to remove %s: %s", DATA_FILE, strerror(errno));
    }
}

    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
        	if (errno == EINTR && exit_requested) {
        	// accept() was interrupted by signal
        		syslog(LOG_INFO, "accept() interrupted by signal, shutting down...\n");
        		break;
    		}
    		perror("accept");
    		continue;
	}


        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
    }

    close(sockfd);
    remove(DATA_FILE);
    closelog();

    return 0;
}

