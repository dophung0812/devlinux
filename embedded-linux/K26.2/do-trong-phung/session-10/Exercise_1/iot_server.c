
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "iot_server.h"

volatile sig_atomic_t running = 1;

void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

int send_all(int fd, const char *data, size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t sent = send(fd, data + total, length - total, 0);

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

void close_client(client_t *client)
{
    if (client->fd >= 0)
    {
        close(client->fd);
        client->fd = -1;
        client->mode = 0;
        client->last_activity = 0;
    }
}

int count_clients(client_t clients[])
{
    int count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd >= 0)
        {
            count++;
        }
    }

    return count;
}

void broadcast_status(client_t clients[], int mode)
{
    double temp = 25.0 + (rand() % 100) / 10.0;
    double humidity = 30.0 + (rand() % 300) / 10.0;
    int client_count = count_clients(clients);

    char message[BUFFER_SIZE];

    int length = snprintf(message,
                          sizeof(message),
                          "[BROADCAST] Temp=%.1f Humidity=%.1f Mode=%d Clients=%d\n",
                          temp,
                          humidity,
                          mode,
                          client_count);

    if (length < 0)
    {
        return;
    }

    printf("[Server] Broadcasting to %d clients: Temp=%.1f Humidity=%.1f Mode=%d Clients=%d\n",
           client_count,
           temp,
           humidity,
           mode,
           client_count);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd >= 0)
        {
            if (send_all(clients[i].fd, message, (size_t)length) < 0)
            {
                close_client(&clients[i]);
            }
        }
    }
}

int process_command(client_t *client, char *command, int *server_mode)
{
    command[strcspn(command, "\r\n")] = '\0';

    if (strcmp(command, "GET_TEMP") == 0)
    {
        double temp = 25.0 + (rand() % 10);
        char response[BUFFER_SIZE];

        int length = snprintf(response,
                              sizeof(response),
                              "%.1f\n",
                              temp);

        if (send_all(client->fd, response, (size_t)length) < 0)
        {
            return -1;
        }

        printf("[Server] Client %d: GET_TEMP -> %.1f\n",
               client->fd,
               temp);
    }
    else if (strcmp(command, "GET_HUMIDITY") == 0)
    {
        double humidity = 30.0 + (rand() % 30);
        char response[BUFFER_SIZE];

        int length = snprintf(response,
                              sizeof(response),
                              "%.1f\n",
                              humidity);

        if (send_all(client->fd, response, (size_t)length) < 0)
        {
            return -1;
        }

        printf("[Server] Client %d: GET_HUMIDITY -> %.1f\n",
               client->fd,
               humidity);
    }
    else if (strncmp(command, "SET_MODE ", 9) == 0)
    {
        int mode;

        if (sscanf(command + 9, "%d", &mode) != 1 ||
            mode < 0 ||
            mode > 2)
        {
            const char *response = "ERROR: Invalid mode\n";

            if (send_all(client->fd, response, strlen(response)) < 0)
            {
                return -1;
            }

            return 0;
        }

        *server_mode = mode;
        client->mode = mode;

        const char *response = "OK\n";

        if (send_all(client->fd, response, strlen(response)) < 0)
        {
            return -1;
        }

        printf("[Server] Client %d: SET_MODE %d -> OK\n",
               client->fd,
               mode);
    }
    else if (strcmp(command, "QUIT") == 0 ||
             strcmp(command, "quit") == 0)
    {
        return 1;
    }
    else
    {
        const char *response = "ERROR: Unknown command\n";

        if (send_all(client->fd, response, strlen(response)) < 0)
        {
            return -1;
        }
    }

    return 0;
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    srand((unsigned int)time(NULL));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int reuse = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse,
                   sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    client_t clients[MAX_CLIENTS];

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].mode = 0;
        clients[i].last_activity = 0;
    }

    int server_mode = 0;

    time_t last_broadcast = time(NULL);

    printf("[Server] Listening on localhost:%d\n", SERVER_PORT);

    while (running)
    {
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd >= 0)
            {
                FD_SET(clients[i].fd, &readfds);

                if (clients[i].fd > max_fd)
                {
                    max_fd = clients[i].fd;
                }
            }
        }

        struct timeval timeout;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(max_fd + 1,
                            &readfds,
                            NULL,
                            NULL,
                            &timeout);

        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd,
                                   (struct sockaddr *)&client_addr,
                                   &client_len);

            if (client_fd < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("accept");
            }
            else
            {
                int slot = -1;

                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (clients[i].fd < 0)
                    {
                        slot = i;
                        break;
                    }
                }

                if (slot < 0)
                {
                    const char *response = "ERROR: Server full\n";
                    send_all(client_fd, response, strlen(response));
                    close(client_fd);
                }
                else
                {
                    clients[slot].fd = client_fd;
                    clients[slot].mode = server_mode;
                    clients[slot].last_activity = time(NULL);

                    char ip[INET_ADDRSTRLEN];

                    if (inet_ntop(AF_INET,
                                  &client_addr.sin_addr,
                                  ip,
                                  sizeof(ip)) == NULL)
                    {
                        strcpy(ip, "unknown");
                    }

                    printf("[Server] Client %d connected from %s:%d\n",
                           slot + 1,
                           ip,
                           ntohs(client_addr.sin_port));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd < 0)
            {
                continue;
            }

            if (!FD_ISSET(clients[i].fd, &readfds))
            {
                continue;
            }

            char buffer[BUFFER_SIZE];

            ssize_t received = recv(clients[i].fd,
                                    buffer,
                                    sizeof(buffer) - 1,
                                    0);

            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("recv");
                close_client(&clients[i]);
                continue;
            }

            if (received == 0)
            {
                printf("[Server] Client %d disconnected.\n", i + 1);
                close_client(&clients[i]);
                continue;
            }

            buffer[received] = '\0';

            clients[i].last_activity = time(NULL);

            char *command = strtok(buffer, "\n");

            while (command != NULL)
            {
                int result_command =
                    process_command(&clients[i],
                                    command,
                                    &server_mode);

                if (result_command < 0)
                {
                    close_client(&clients[i]);
                    break;
                }

                if (result_command > 0)
                {
                    close_client(&clients[i]);
                    break;
                }

                command = strtok(NULL, "\n");
            }
        }

        time_t now = time(NULL);

        if (now - last_broadcast >= BROADCAST_INTERVAL)
        {
            broadcast_status(clients, server_mode);
            last_broadcast = now;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd >= 0)
        {
            close(clients[i].fd);
        }
    }

    close(server_fd);

    printf("[Server] Shutdown complete.\n");

    return 0;
}
