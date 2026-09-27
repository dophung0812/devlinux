#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <time.h>
#include "event_monitor.h"

volatile sig_atomic_t running = 1;

void handle_sigterm(int sig)
{
    (void)sig;
    running = 0;
}

int send_all(int fd, const char *data, size_t len)
{
    size_t total = 0;

    while (total < len) {
        ssize_t n = send(fd, data + total, len - total, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        total += n;
    }

    return 0;
}

int main(void)
{
    int fifo_fd;
    int socket_fd;
    int file_fd;
    int client_fd;

    int events_seen = 0;
    int active = 1;

    struct stat file_stat;
    off_t last_size = 0;

    time_t start_time = time(NULL);

    signal(SIGTERM, handle_sigterm);

    if (mkfifo(FIFO_PATH, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);

    if (fifo_fd < 0) {
        perror("open FIFO");
        return 1;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        perror("socket");
        close(fifo_fd);
        return 1;
    }

    unlink(SOCKET_PATH);

    struct sockaddr_un address;

    memset(&address, 0, sizeof(address));

    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (bind(socket_fd,
             (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(fifo_fd);
        close(socket_fd);
        return 1;
    }

    if (listen(socket_fd, 5) < 0) {
        perror("listen");
        close(fifo_fd);
        close(socket_fd);
        unlink(SOCKET_PATH);
        return 1;
    }

    file_fd = open(FILE_PATH, O_RDWR | O_CREAT, 0666);

    if (file_fd < 0) {
        perror("open system_status");
        close(fifo_fd);
        close(socket_fd);
        unlink(SOCKET_PATH);
        return 1;
    }

    if (stat(FILE_PATH, &file_stat) < 0) {
        perror("stat");
        close(fifo_fd);
        close(socket_fd);
        close(file_fd);
        unlink(SOCKET_PATH);
        return 1;
    }

    last_size = file_stat.st_size;

    printf("[Monitor] Listening on %s\n", SOCKET_PATH);
    printf("[Monitor] Monitoring %s (FIFO) and %s\n",
           FIFO_PATH,
           FILE_PATH);

    struct pollfd pollfds[NUM_POLL_FDS];

    pollfds[FD_FIFO].fd = fifo_fd;
    pollfds[FD_FIFO].events = POLLIN;

    pollfds[FD_SOCKET_LISTENER].fd = socket_fd;
    pollfds[FD_SOCKET_LISTENER].events = POLLIN;

    pollfds[FD_FILE].fd = file_fd;
    pollfds[FD_FILE].events = 0;

    while (running) {
        int result = poll(
            pollfds,
            NUM_POLL_FDS,
            POLL_TIMEOUT_MS
        );

        if (result < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        if (result == 0) {
            printf("[HEARTBEAT] Monitor alive, events_seen=%d\n",
                   events_seen);
        }

        if (pollfds[FD_FIFO].revents & POLLIN) {
            char buffer[256];

            ssize_t n = read(fifo_fd, buffer, sizeof(buffer) - 1);

            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    perror("read FIFO");
            } else if (n > 0) {
                buffer[n] = '\0';

                char *line = strtok(buffer, "\n");

                while (line != NULL) {
                    if (active) {
                        printf("[FIFO_EVENT] %s\n", line);
                        events_seen++;
                    }

                    line = strtok(NULL, "\n");
                }
            }
        }

        if (pollfds[FD_SOCKET_LISTENER].revents & POLLIN) {
            client_fd = accept(socket_fd, NULL, NULL);

            if (client_fd < 0) {
                if (errno != EINTR)
                    perror("accept");
            } else {
                char command[128];

                ssize_t n = recv(
                    client_fd,
                    command,
                    sizeof(command) - 1,
                    0
                );

                if (n < 0) {
                    if (errno != EINTR)
                        perror("recv");
                } else if (n > 0) {
                    command[n] = '\0';

                    char *newline = strchr(command, '\n');

                    if (newline)
                        *newline = '\0';

                    newline = strchr(command, '\r');

                    if (newline)
                        *newline = '\0';

                    char response[256];

                    if (strcmp(command, "STATUS") == 0) {
                        time_t now = time(NULL);
                        long uptime = (long)(now - start_time);

                        snprintf(
                            response,
                            sizeof(response),
                            "Total events: %d, Uptime: %ld seconds\n",
                            events_seen,
                            uptime
                        );

                        send_all(
                            client_fd,
                            response,
                            strlen(response)
                        );
                    } else if (strcmp(command, "START") == 0) {
                        active = 1;

                        snprintf(
                            response,
                            sizeof(response),
                            "OK\n"
                        );

                        send_all(
                            client_fd,
                            response,
                            strlen(response)
                        );
                    } else if (strcmp(command, "STOP") == 0) {
                        active = 0;

                        snprintf(
                            response,
                            sizeof(response),
                            "OK\n"
                        );

                        send_all(
                            client_fd,
                            response,
                            strlen(response)
                        );
                    } else if (strcmp(command, "EXIT") == 0 ||
                               strcmp(command, "exit") == 0) {

                        snprintf(
                            response,
                            sizeof(response),
                            "OK\n"
                        );

                        send_all(
                            client_fd,
                            response,
                            strlen(response)
                        );
                    } else {
                        snprintf(
                            response,
                            sizeof(response),
                            "ERROR: Unknown command\n"
                        );

                        send_all(
                            client_fd,
                            response,
                            strlen(response)
                        );
                    }
                }

                close(client_fd);
            }
        }

        if (stat(FILE_PATH, &file_stat) < 0) {
            perror("stat");
        } else if (file_stat.st_size != last_size) {
            last_size = file_stat.st_size;

            if (active) {
                printf(
                    "[FILE_EVENT] %s size changed to %ld bytes\n",
                    FILE_PATH,
                    (long)last_size
                );

                events_seen++;
            }
        }
    }

    close(fifo_fd);
    close(socket_fd);
    close(file_fd);

    unlink(SOCKET_PATH);

    printf("[Monitor] Shutdown complete.\n");

    return 0;
}