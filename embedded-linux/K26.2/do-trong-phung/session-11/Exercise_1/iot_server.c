#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "iot_server.h"

volatile sig_atomic_t running = 1;

void handle_sigint(int sig)
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

void remove_client(client_t clients[], int index, fd_set *master)
{
    if (clients[index].fd != -1) {
        close(clients[index].fd);

        if (master != NULL)
            FD_CLR(clients[index].fd, master);

        clients[index].fd = -1;
        clients[index].mode = 0;
        clients[index].last_activity = 0;
    }
}

void broadcast_status(client_t clients[], int count, fd_set *master)
{
    double temp = 25.0 + (rand() % 10);
    double humidity = 30.0 + (rand() % 30);
    int mode = 0;
    int active_clients = 0;
    char message[256];

    for (int i = 0; i < count; i++) {
        if (clients[i].fd != -1) {
            active_clients++;

            if (clients[i].mode > mode)
                mode = clients[i].mode;
        }
    }

    snprintf(
        message,
        sizeof(message),
        "[SERVER_STATUS] Temp=%.1f Humidity=%.1f Mode=%d Clients=%d\n",
        temp,
        humidity,
        mode,
        active_clients
    );

    printf("[Server] Broadcasting to %d clients: Temp=%.1f Humidity=%.1f Mode=%d Clients=%d\n",
           active_clients, temp, humidity, mode, active_clients);

    for (int i = 0; i < count; i++) {
        if (clients[i].fd != -1) {
            if (send_all(clients[i].fd, message, strlen(message)) < 0)
                remove_client(clients, i, master);
        }
    }
}

int main(void)
{
    int server_fd;
    int opt = 1;
    struct sockaddr_in server_addr;
    client_t clients[MAX_CLIENTS];

    fd_set master;
    fd_set readfds;

    signal(SIGINT, handle_sigint);

    srand((unsigned int)time(NULL));

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].mode = 0;
        clients[i].last_activity = 0;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    FD_ZERO(&master);
    FD_SET(server_fd, &master);

    int max_fd = server_fd;
    time_t last_broadcast = time(NULL);

    printf("[Server] Listening on localhost:%d\n", SERVER_PORT);

    while (running) {
        readfds = master;

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (result < 0) {
            if (errno == EINTR)
                continue;

            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(
                server_fd,
                (struct sockaddr *)&client_addr,
                &client_len
            );

            if (client_fd < 0) {
                if (errno != EINTR)
                    perror("accept");
            } else {
                int added = 0;

                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) {
                        clients[i].fd = client_fd;
                        clients[i].mode = 0;
                        clients[i].last_activity = time(NULL);

                        FD_SET(client_fd, &master);

                        if (client_fd > max_fd)
                            max_fd = client_fd;

                        printf("[Server] Client %d connected from %s:%d\n",
                               i + 1,
                               inet_ntoa(client_addr.sin_addr),
                               ntohs(client_addr.sin_port));

                        added = 1;
                        break;
                    }
                }

                if (!added) {
                    const char *message = "ERROR: Maximum clients reached\n";
                    send_all(client_fd, message, strlen(message));
                    close(client_fd);
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;

            if (fd == -1)
                continue;

            if (!FD_ISSET(fd, &readfds))
                continue;

            char buffer[256];

            ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);

            if (n < 0) {
                if (errno == EINTR)
                    continue;

                perror("recv");
                remove_client(clients, i, &master);
                continue;
            }

            if (n == 0) {
                printf("[Server] Client %d disconnected.\n", i + 1);
                remove_client(clients, i, &master);
                continue;
            }

            buffer[n] = '\0';

            char *newline = strchr(buffer, '\n');

            if (newline)
                *newline = '\0';

            newline = strchr(buffer, '\r');

            if (newline)
                *newline = '\0';

            clients[i].last_activity = time(NULL);

            char response[256];

            if (strcmp(buffer, "GET_TEMP") == 0) {
                double temp = 25.0 + (rand() % 10);

                snprintf(response, sizeof(response), "%.1f\n", temp);

                printf("[Server] Client %d: GET_TEMP -> %.1f\n", i + 1, temp);

                if (send_all(fd, response, strlen(response)) < 0)
                    remove_client(clients, i, &master);
            } else if (strcmp(buffer, "GET_HUMIDITY") == 0) {
                double humidity = 30.0 + (rand() % 30);

                snprintf(response, sizeof(response), "%.1f\n", humidity);

                printf("[Server] Client %d: GET_HUMIDITY -> %.1f\n",
                       i + 1, humidity);

                if (send_all(fd, response, strlen(response)) < 0)
                    remove_client(clients, i, &master);
            } else if (strncmp(buffer, "SET_MODE ", 9) == 0) {
                int mode;

                if (sscanf(buffer + 9, "%d", &mode) == 1 &&
                    mode >= 0 && mode <= 2) {

                    clients[i].mode = mode;

                    snprintf(response, sizeof(response), "OK\n");

                    printf("[Server] Client %d: SET_MODE %d -> OK\n",
                           i + 1, mode);
                } else {
                    snprintf(response, sizeof(response),
                             "ERROR: Invalid mode\n");
                }

                if (send_all(fd, response, strlen(response)) < 0)
                    remove_client(clients, i, &master);
            } else if (strcmp(buffer, "QUIT") == 0 ||
                       strcmp(buffer, "quit") == 0) {

                printf("[Server] Client %d requested QUIT\n", i + 1);

                remove_client(clients, i, &master);
            } else {
                snprintf(response, sizeof(response),
                         "ERROR: Unknown command\n");

                if (send_all(fd, response, strlen(response)) < 0)
                    remove_client(clients, i, &master);
            }
        }

        time_t now = time(NULL);

        if (now - last_broadcast >= BROADCAST_INTERVAL) {
            broadcast_status(clients, MAX_CLIENTS, &master);
            last_broadcast = now;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1)
            close(clients[i].fd);
    }

    close(server_fd);

    printf("[Server] Shutdown complete.\n");

    return 0;
}