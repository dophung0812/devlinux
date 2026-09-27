CC = gcc
CFLAGS = -Wall -Wextra -O2

all: sensor collector

sensor: sender.c
	$(CC) $(CFLAGS) sender.c -o sensor

collector: receiver.c
	$(CC) $(CFLAGS) receiver.c -o collector

clean:
	rm -f sensor collector

