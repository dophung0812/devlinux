#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/monitor.sock"
#define BUFFER_SIZE 256

int main(void)
{
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (client_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(client_fd);
        exit(1);
    }

    printf("[monitor-cli] Connected to %s\n", SOCKET_PATH);

    while (1)
    {
        char buffer[BUFFER_SIZE];

        printf("> ");
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = '\0';

        if (strcmp(buffer, "quit") == 0)
        {
            break;
        }

        size_t command_len = strlen(buffer);

        ssize_t sent = send(client_fd, buffer, command_len, 0);

        if (sent < 0)
        {
            perror("send");
            break;
        }

        if ((size_t)sent != command_len)
        {
            fprintf(stderr, "send: incomplete transmission\n");
            break;
        }

        char response[BUFFER_SIZE];

        ssize_t received = recv(client_fd, response, sizeof(response) - 1, 0);

        if (received < 0)
        {
            perror("recv");
            break;
        }

        if (received == 0)
        {
            printf("[monitor-cli] Server disconnected.\n");
            break;
        }

        response[received] = '\0';

        printf("%s", response);
    }

    close(client_fd);

    return 0;
}
