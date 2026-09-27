
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "sensor_shm.h"

int shmid;
sensor_data_t *sensor_data;

void handle_sigint(int sig)
{
    (void)sig;

    printf("\n[Daemon] Cleaning up shared memory. Goodbye.\n");

    if (shmdt(sensor_data) < 0)
    {
        perror("shmdt");
    }

    if (shmctl(shmid, IPC_RMID, NULL) < 0)
    {
        perror("shmctl");
    }

    exit(0);
}

int get_system_data(double *cpu_temp, double *ram_used_pct)
{
    FILE *load_file = fopen("/proc/loadavg", "r");

    if (load_file == NULL)
    {
        return -1;
    }

    double load1;

    if (fscanf(load_file, "%lf", &load1) != 1)
    {
        fclose(load_file);
        return -1;
    }

    fclose(load_file);

    FILE *mem_file = fopen("/proc/meminfo", "r");

    if (mem_file == NULL)
    {
        return -1;
    }

    char line[128];
    long mem_total = 0;
    long mem_free = 0;

    while (fgets(line, sizeof(line), mem_file) != NULL)
    {
        if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemFree: %ld kB", &mem_free) == 1)
        {
            continue;
        }
    }

    fclose(mem_file);

    if (mem_total == 0)
    {
        return -1;
    }

    *cpu_temp = 40.0 + load1 * 10.0;
    *ram_used_pct =
        (double)(mem_total - mem_free) / mem_total * 100.0;

    return 0;
}

int main(void)
{
    signal(SIGINT, handle_sigint);

    shmid = shmget(SHM_KEY,
                   sizeof(sensor_data_t),
                   IPC_CREAT | 0666);

    if (shmid < 0)
    {
        perror("shmget");
        exit(1);
    }

    sensor_data = shmat(shmid, NULL, 0);

    if (sensor_data == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    printf("[Daemon] Shared memory created. Key=0x1234\n");

    while (1)
    {
        double cpu_temp;
        double ram_used_pct;

        if (get_system_data(&cpu_temp, &ram_used_pct) < 0)
        {
            fprintf(stderr, "[Daemon] Failed to read system information\n");
            sleep(2);
            continue;
        }

        sensor_data->timestamp = time(NULL);
        sensor_data->cpu_temp = cpu_temp;
        sensor_data->ram_used_pct = ram_used_pct;

        printf("[Daemon] Written: temp=%.2f ram=%.2f%%\n",
               sensor_data->cpu_temp,
               sensor_data->ram_used_pct);

        sleep(2);
    }

    return 0;
}
