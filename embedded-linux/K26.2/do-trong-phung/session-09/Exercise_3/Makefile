
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lrt -lpthread

all: controller device

controller: controller.c device_state.h
	$(CC) $(CFLAGS) controller.c -o controller $(LDFLAGS)

device: device.c device_state.h
	$(CC) $(CFLAGS) device.c -o device $(LDFLAGS)

clean:
	rm -f controller device
	rm -f /dev/shm/device_shm

