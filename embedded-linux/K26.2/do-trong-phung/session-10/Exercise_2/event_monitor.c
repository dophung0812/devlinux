
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "event_monitor.h"

volatile sig_atomic_t running = 1;

void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int send_all(int fd, const char *data, size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t sent = send(fd,
                            data + total,
                            length - total,
                            0);

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}

void cleanup(int fifo_fd, int socket_fd, int file_fd)
{
    if (fifo_fd >= 0)
    {
        close(fifo_fd);
    }

    if (socket_fd >= 0)
    {
        close(socket_fd);
    }

    if (file_fd >= 0)
    {
        close(file_fd);
    }

    unlink(SOCKET_PATH);
}

int main(void)
{
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    if (mkfifo(FIFO_PATH, 0666) < 0)
    {
        if (errno != EEXIST)
        {
            perror("mkfifo");
            return 1;
        }
    }

    int fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);

    if (fifo_fd < 0)
    {
        perror("open FIFO");
        return 1;
    }

    unlink(SOCKET_PATH);

    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd < 0)
    {
        perror("socket");
        close(fifo_fd);
        return 1;
    }

    struct sockaddr_un socket_addr;

    memset(&socket_addr, 0, sizeof(socket_addr));

    socket_addr.sun_family = AF_UNIX;

    strncpy(socket_addr.sun_path,
            SOCKET_PATH,
            sizeof(socket_addr.sun_path) - 1);

    if (bind(socket_fd,
             (struct sockaddr *)&socket_addr,
             sizeof(socket_addr)) < 0)
    {
        perror("bind");
        cleanup(fifo_fd, socket_fd, -1);
        return 1;
    }

    if (listen(socket_fd, 5) < 0)
    {
        perror("listen");
        cleanup(fifo_fd, socket_fd, -1);
        return 1;
    }

    int file_fd = open(FILE_PATH, O_RDONLY | O_CREAT, 0666);

    if (file_fd < 0)
    {
        perror("open system_status");
        cleanup(fifo_fd, socket_fd, -1);
        return 1;
    }

    struct stat file_stat;

    if (stat(FILE_PATH, &file_stat) < 0)
    {
        perror("stat");
        cleanup(fifo_fd, socket_fd, file_fd);
        return 1;
    }

    off_t last_size = file_stat.st_size;

    unsigned long total_events = 0;
    time_t start_time = time(NULL);
    int active = 1;

    struct pollfd poll_fds[NUM_POLL_FDS];

    poll_fds[FD_FIFO].fd = fifo_fd;
    poll_fds[FD_FIFO].events = POLLIN;

    poll_fds[FD_SOCKET_LISTENER].fd = socket_fd;
    poll_fds[FD_SOCKET_LISTENER].events = POLLIN;

    poll_fds[FD_FILE].fd = file_fd;
    poll_fds[FD_FILE].events = 0;

    printf("[Monitor] Listening on %s\n", SOCKET_PATH);
    printf("[Monitor] Monitoring %s (FIFO) and %s\n",
           FIFO_PATH,
           FILE_PATH);

    while (running)
    {
        int result = poll(poll_fds,
                          NUM_POLL_FDS,
                          POLL_TIMEOUT_MS);

        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("poll");
            break;
        }

        if (result == 0)
        {
            printf("[HEARTBEAT] Monitor alive, events_seen=%lu\n",
                   total_events);
        }

        if (poll_fds[FD_FIFO].revents & POLLIN)
        {
            char buffer[BUFFER_SIZE];

            ssize_t received = read(fifo_fd,
                                    buffer,
                                    sizeof(buffer) - 1);

            if (received > 0)
            {
                buffer[received] = '\0';

                char *line = strtok(buffer, "\n");

                while (line != NULL)
                {
                    if (active)
                    {
                        printf("[FIFO_EVENT] %s\n", line);
                        total_events++;
                    }

                    line = strtok(NULL, "\n");
                }
            }
            else if (received < 0 &&
                     errno != EAGAIN &&
                     errno != EWOULDBLOCK &&
                     errno != EINTR)
            {
                perror("read FIFO");
            }
        }

        if (poll_fds[FD_SOCKET_LISTENER].revents & POLLIN)
        {
            int client_fd = accept(socket_fd, NULL, NULL);

            if (client_fd < 0)
            {
                if (errno != EINTR)
                {
                    perror("accept");
                }
            }
            else
            {
                char command[BUFFER_SIZE];

                ssize_t received = recv(client_fd,
                                        command,
                                        sizeof(command) - 1,
                                        0);

                if (received < 0)
                {
                    perror("recv");
                    close(client_fd);
                }
                else if (received == 0)
                {
                    close(client_fd);
                }
                else
                {
                    command[received] = '\0';

                    command[strcspn(command,
                                    "\r\n")] = '\0';

                    if (strcmp(command, "STATUS") == 0)
                    {
                        time_t now = time(NULL);
                        long uptime = (long)(now - start_time);

                        char response[BUFFER_SIZE];

                        int length = snprintf(
                            response,
                            sizeof(response),
                            "Total events: %lu, Uptime: %ld seconds\n",
                            total_events,
                            uptime);

                        if (send_all(client_fd,
                                     response,
                                     (size_t)length) < 0)
                        {
                            perror("send");
                        }
                    }
                    else if (strcmp(command, "START") == 0)
                    {
                        active = 1;

                        const char *response = "OK\n";

                        if (send_all(client_fd,
                                     response,
                                     strlen(response)) < 0)
                        {
                            perror("send");
                        }
                    }
                    else if (strcmp(command, "STOP") == 0)
                    {
                        active = 0;

                        const char *response = "OK\n";

                        if (send_all(client_fd,
                                     response,
                                     strlen(response)) < 0)
                        {
                            perror("send");
                        }
                    }
                    else if (strcmp(command, "EXIT") == 0 ||
                             strcmp(command, "exit") == 0)
                    {
                        const char *response = "OK\n";

                        if (send_all(client_fd,
                                     response,
                                     strlen(response)) < 0)
                        {
                            perror("send");
                        }
                    }
                    else
                    {
                        const char *response =
                            "ERROR: Unknown command\n";

                        if (send_all(client_fd,
                                     response,
                                     strlen(response)) < 0)
                        {
                            perror("send");
                        }
                    }

                    close(client_fd);
                }
            }
        }

        if (active)
        {
            if (stat(FILE_PATH, &file_stat) < 0)
            {
                perror("stat");
            }
            else if (file_stat.st_size != last_size)
            {
                last_size = file_stat.st_size;

                printf("[FILE_EVENT] %s size changed to %ld bytes\n",
                       FILE_PATH,
                       (long)last_size);

                total_events++;
            }
        }
    }

    cleanup(fifo_fd, socket_fd, file_fd);

    printf("[Monitor] Shutdown complete.\n");

    return 0;
}

