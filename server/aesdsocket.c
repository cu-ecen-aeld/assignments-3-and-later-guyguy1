#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DATA_FILE "/var/tmp/aesdsocketdata"

static int exit_requested = 0;

void signal_handler(int signo) {
    (void)signo;

    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

void setup_signal_handling() {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(-1);
    }
    if (pid > 0) {
        exit(0);
    }

    if (setsid() == -1) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(-1);
    }

    int fd = open("/dev/null", O_RDWR);
    if (fd == -1)
    {
        syslog(LOG_ERR, "failed opening /dev/null: %s", strerror(errno));
        exit(-1);
    }

    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    close(fd);
}

int main(int argc, char *argv[]) {
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    char *packet = NULL;
    size_t packet_size = 0;
    ssize_t bytes_received;
    ssize_t bytes_read;

    openlog("aesdsocket", LOG_PID, LOG_USER);
    setup_signal_handling();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket() failed: %s", strerror(errno));
        return -1;
    }

    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "bind() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemonize();
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (!exit_requested) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR)
            {
                continue;
            }
            syslog(LOG_ERR, "accept() failed: %s", strerror(errno));
            return -1;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));
        FILE *data_file = fopen(DATA_FILE, "a");
        if (!data_file) {
            syslog(LOG_ERR, "fopen() for appending to %s failed: %s", DATA_FILE, strerror(errno));
            return -1;
        }

        packet = NULL;
        packet_size = 0;

        while (1)
        {
            bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                break;
            }

            packet = realloc(packet, packet_size + bytes_received + 1);
            if (!packet) {
                syslog(LOG_ERR, "realloc failed");
                return -1;
            }

            memcpy(packet + packet_size, buffer, bytes_received);
            packet_size += bytes_received;
            packet[packet_size] = '\0';

            if (strchr(packet, '\n')) {
                fwrite(packet, 1, packet_size, data_file);
                fflush(data_file);
                break;
            }
        }

        free(packet);
        fclose(data_file);

        data_file = fopen(DATA_FILE, "r");
        if (data_file == NULL)
        {
            syslog(LOG_ERR, "fopen for reading %s failed: %s", DATA_FILE, strerror(errno));
        }

        while ((bytes_read = fread(buffer, 1, sizeof(buffer), data_file)) > 0) {
            send(client_fd, buffer, bytes_read, 0);
        }
        fclose(data_file);

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(client_fd);
    }

    close(server_fd);
    unlink(DATA_FILE);
    closelog();
    return 0;
}
