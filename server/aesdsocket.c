#include <time.h>
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
#include <pthread.h>
#include <stdbool.h>

#include "queue.h"

#define DATA_FILE "/var/tmp/aesdsocketdata"

struct thread_data
{
    pthread_t thread_id;
    int socket_fd;
    struct in_addr client_addr;
    pthread_mutex_t *data_file_mutex;
    SLIST_ENTRY(thread_data) next;

    bool success;
    bool is_finished;
};

static int exit_requested = 0;
static SLIST_HEAD(thread_list_head, thread_data) threads;

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

void* thread_func(void *arg)
{
    struct thread_data *data = (struct thread_data *)arg;
    int client_fd = data->socket_fd;
    char *packet = NULL;
    size_t packet_size = 0;
    char buffer[1024];
    ssize_t bytes_received;
    ssize_t bytes_read;
    FILE *data_file = NULL;
    bool success = false;

    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(data->client_addr));

    while (1)
    {
        bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            break;
        }

        packet = realloc(packet, packet_size + bytes_received + 1);
        if (!packet) {
            syslog(LOG_ERR, "realloc failed");
            goto cleanup;
        }

        memcpy(packet + packet_size, buffer, bytes_received);
        packet_size += bytes_received;
        packet[packet_size] = '\0';

        if (strchr(packet, '\n')) {
            break;
        }
    }

    if (pthread_mutex_lock(data->data_file_mutex) != 0)
    {
        syslog(LOG_ERR, "lock failed");
        goto cleanup;
    }
    
    data_file = fopen(DATA_FILE, "a");
    if (!data_file) {
        syslog(LOG_ERR, "fopen() for appending to %s failed: %s", DATA_FILE, strerror(errno));
        pthread_mutex_unlock(data->data_file_mutex);
        goto cleanup;
    }
    fwrite(packet, 1, packet_size, data_file);
    fflush(data_file);

    free(packet);
    fclose(data_file);
    data_file = NULL;

    data_file = fopen(DATA_FILE, "r");
    if (data_file == NULL)
    {
        syslog(LOG_ERR, "fopen for reading %s failed: %s", DATA_FILE, strerror(errno));
        pthread_mutex_unlock(data->data_file_mutex);
        goto cleanup;
    }

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), data_file)) > 0) {
        send(client_fd, buffer, bytes_read, 0);
    }

    fclose(data_file);
    data_file = NULL;

    if (pthread_mutex_unlock(data->data_file_mutex) != 0)
    {
        syslog(LOG_ERR, "unlock failed");
        goto cleanup;
    }

    success = true;

cleanup:
    if (data_file != NULL)
    {
        fclose(data_file);
    }
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(data->client_addr));

    data->success = success;
    data->is_finished = true;

    return NULL;
}

void* timestamp_thread_func(void *arg)
{
    pthread_mutex_t *data_file_mutex = (pthread_mutex_t *)arg;

    while (1) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        if (!tm_info) {
            syslog(LOG_ERR,"localtime failed");
            exit(-1);
        }

        char time_str[128];

        if (strftime(time_str, sizeof(time_str), "%a, %d %b %Y %H:%M:%S %z", tm_info) == 0) {
            syslog(LOG_ERR, "strftime returned 0");
            exit(-1);
        }

        syslog(LOG_DEBUG, "timestamp thread: locking and writng timestamp %s", time_str);

        if (pthread_mutex_lock(data_file_mutex) != 0)
        {
            syslog(LOG_ERR, "lock failed in timestamp thread");
            exit(-1);
        }

        FILE *data_file = fopen(DATA_FILE, "a");
        if (data_file == NULL) {
            syslog(LOG_ERR,"fopen %s failed in timestamp thread", DATA_FILE);
            exit(-1);
        }

        fprintf(data_file, "timestamp:%s\n", time_str);
        fflush(data_file);
        fclose(data_file);

        if (pthread_mutex_unlock(data_file_mutex) != 0)
        {
            syslog(LOG_ERR, "unlock failed in timestamp thread");
            exit(-1);
        }

        sleep(10);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int server_fd = -1;
    int client_fd = -1;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_mutex_t data_file_mutex = {0};
    struct thread_data *thread_data = NULL;

    SLIST_FIRST(&threads) = NULL;
    openlog("aesdsocket", LOG_PID, LOG_USER);
    setup_signal_handling();
    pthread_mutex_init(&data_file_mutex, NULL);

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

    pthread_t timestamp_thread;
    if (pthread_create(&timestamp_thread, NULL, timestamp_thread_func, &data_file_mutex) != 0)
    {
        syslog(LOG_ERR, "pthread_create timestamp thread failed");
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

        thread_data = (struct thread_data *)malloc(sizeof(struct thread_data));
        if (thread_data == NULL)
        {
            syslog(LOG_ERR, "malloc failed");
            return -1;
        }
        memset(thread_data, 0, sizeof(struct thread_data));

        thread_data->socket_fd = client_fd;
        thread_data->client_addr = client_addr.sin_addr;
        thread_data->data_file_mutex = &data_file_mutex;
        SLIST_INSERT_HEAD(&threads, thread_data, next);

        if (pthread_create(&thread_data->thread_id, NULL, thread_func, thread_data) != 0)
        {
            syslog(LOG_ERR, "pthread_create failed");
            return -1;
        }

        struct thread_data *current = NULL, *next = NULL;
        SLIST_FOREACH_SAFE(current, &threads, next, next)
        {
            if (current->is_finished)
            {
                syslog(LOG_INFO, "Thread %ld finished with %s", current->thread_id, current->success ? "success" : "failure");
                SLIST_REMOVE(&threads, current, thread_data, next);

                if (pthread_join(current->thread_id, NULL) != 0)
                {
                    syslog(LOG_ERR, "pthread_join failed");
                    return -1;
                }

                free(current);
            }
        }
    }

    close(server_fd);
    unlink(DATA_FILE);
    closelog();
    return 0;
}
