
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "event_monitor.h"

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

int main(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_un server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sun_family = AF_UNIX;

    strncpy(server_addr.sun_path,
            SOCKET_PATH,
            sizeof(server_addr.sun_path) - 1);

    if (connect(fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(fd);
        return 1;
    }

    while (1)
    {
        char command[BUFFER_SIZE];

        printf("> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        command[strcspn(command, "\r\n")] = '\0';

        char send_buffer[BUFFER_SIZE];

        int length = snprintf(send_buffer,
                              sizeof(send_buffer),
                              "%s\n",
                              command);

        if (length < 0 ||
            (size_t)length >= sizeof(send_buffer))
        {
            fprintf(stderr, "Command too long.\n");
            continue;
        }

        if (send_all(fd,
                     send_buffer,
                     (size_t)length) < 0)
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
            printf("[Client] Monitor disconnected.\n");
            break;
        }

        response[received] = '\0';

        printf("%s", response);

        if (strcmp(command, "EXIT") == 0 ||
            strcmp(command, "exit") == 0)
        {
            break;
        }
    }

    close(fd);

    return 0;
}

