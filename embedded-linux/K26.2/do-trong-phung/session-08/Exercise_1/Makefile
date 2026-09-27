CC = gcc
CFLAGS = -Wall -Wextra -O2

all: monitor-daemon monitor-cli

monitor-daemon: server.c
	$(CC) $(CFLAGS) server.c -o monitor-daemon

monitor-cli: client.c
	$(CC) $(CFLAGS) client.c -o monitor-cli

clean:
	rm -f monitor-daemon monitor-cli
	rm -f /tmp/monitor.sock

