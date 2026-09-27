
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "sensor_shm.h"

int main(void)
{
    int shmid = shmget(SHM_KEY, sizeof(sensor_data_t), 0666);

    if (shmid < 0)
    {
        fprintf(stderr, "Daemon is not running.\n");
        exit(1);
    }

    sensor_data_t *sensor_data = shmat(shmid, NULL, 0);

    if (sensor_data == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    printf("[Sensor Report]\n\n");
    printf("Timestamp : %ld\n", (long)sensor_data->timestamp);
    printf("CPU Temp  : %.2f C\n", sensor_data->cpu_temp);
    printf("RAM Used  : %.2f %%\n", sensor_data->ram_used_pct);

    if (shmdt(sensor_data) < 0)
    {
        perror("shmdt");
        exit(1);
    }

    return 0;
}

