
CC = gcc
CFLAGS = -Wall -Wextra -O2

all: iot_server iot_client

iot_server: iot_server.c iot_server.h
	$(CC) $(CFLAGS) iot_server.c -o iot_server

iot_client: iot_client.c iot_server.h
	$(CC) $(CFLAGS) iot_client.c -o iot_client

clean:
	rm -f iot_server iot_client

