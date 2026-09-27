
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "iot_server.h"

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

int main(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(fd);
        return 1;
    }

    if (connect(fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(fd);
        return 1;
    }

    printf("[Client] Connected to localhost:%d\n", SERVER_PORT);

    while (1)
    {
        char command[BUFFER_SIZE];

        printf("> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        if (strcmp(command, "quit\n") == 0 ||
            strcmp(command, "QUIT\n") == 0)
        {
            if (send_all(fd, command, strlen(command)) < 0)
            {
                perror("send");
            }

            break;
        }

        if (send_all(fd, command, strlen(command)) < 0)
        {
            perror("send");
            break;
        }

        char response[BUFFER_SIZE];

        ssize_t received = recv(fd,
                                response,
                                sizeof(response) - 1,
                                0);

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("recv");
            break;
        }

        if (received == 0)
        {
            printf("[Client] Server disconnected.\n");
            break;
        }

        response[received] = '\0';

        printf("%s", response);
    }

    close(fd);

    return 0;
}
