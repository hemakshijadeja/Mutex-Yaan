#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>

#define SHM_NAME "/sat_network_shm"
#define SHM_PERMS 0666

typedef struct{
    int debris_id;
    int sat_id;
    int debris_x, debris_y, debris_z;
    int sat_x, sat_y, sat_z;
    int miss_distance_m;
    int time_to_impact_s;
    int evade_dvx, evade_dvy, evade_dvz;
    char description[64];
} CollisionAlert;

typedef struct{
    pthread_mutex_t lock;
    CollisionAlert alert;
    int alert_ready;
    pid_t server_pid;
    int alert_count;
    int child_alive;
} SharedMemory;

SharedMemory *shm_setup(void);

SharedMemory *shm_attach(void);

void shm_teardown(SharedMemory *shm);

void shm_detach(SharedMemory *shm);

int shm_write_alert(SharedMemory *shm, CollisionAlert *alert);

int shm_read_alert(SharedMemory *shm, CollisionAlert *out);

#endif