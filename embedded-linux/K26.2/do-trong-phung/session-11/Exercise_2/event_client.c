#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "event_monitor.h"

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
    int socket_fd;
    struct sockaddr_un address;
    char buffer[128];

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&address, 0, sizeof(address));

    address.sun_family = AF_UNIX;
    strncpy(
        address.sun_path,
        SOCKET_PATH,
        sizeof(address.sun_path) - 1
    );

    if (connect(socket_fd,
                (struct sockaddr *)&address,
                sizeof(address)) < 0) {
        perror("connect");
        close(socket_fd);
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (send_all(socket_fd, buffer, strlen(buffer)) < 0) {
            perror("send");
            break;
        }

        ssize_t n = recv(
            socket_fd,
            buffer,
            sizeof(buffer) - 1,
            0
        );

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

        if (strncmp(buffer, "quit", 4) == 0 ||
            strncmp(buffer, "exit", 4) == 0)
            break;
    }

    close(socket_fd);

    return 0;
}