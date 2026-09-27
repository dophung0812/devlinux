CC = gcc
CFLAGS = -Wall -Wextra -O2

all: event_monitor event_client

event_monitor: event_monitor.c event_monitor.h
	$(CC) $(CFLAGS) event_monitor.c -o event_monitor

event_client: event_client.c event_monitor.h
	$(CC) $(CFLAGS) event_client.c -o event_client

clean:
	rm -f event_monitor event_client