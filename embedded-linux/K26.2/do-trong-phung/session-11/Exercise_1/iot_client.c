#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "iot_server.h"

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
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[256];

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("[Client] Connected to localhost:%d\n", SERVER_PORT);

    while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (send_all(sock_fd, buffer, strlen(buffer)) < 0) {
            perror("send");
            break;
        }

        if (strncmp(buffer, "quit", 4) == 0 ||
            strncmp(buffer, "QUIT", 4) == 0)
            break;

        ssize_t n = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("recv");
            break;
        }

        if (n == 0)
            break;

        buffer[n] = '\0';

        printf("%s", buffer);
    }

    close(sock_fd);

    return 0;
}