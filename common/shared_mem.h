#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>

#include "protocol.h"

#define SHM_NAME "/sat_network_shm"
#define SHM_PERMS 0666

#define MAX_DEBRIS 32

// simulated debris object
typedef struct{
    int debris_id;
    int x, y, z; 
    int vx, vy, vz;
} Debris;

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

// live satellite position entry written by the server, read by the child
typedef struct{
    int sat_id;
    int x, y, z;
    int active;
    int vx, vy, vz;
} ShmSatSnapshot;

typedef struct{
    pthread_mutex_t lock;
    CollisionAlert alert;
    int alert_ready;
    pid_t server_pid;
    int alert_count;
    int child_alive;
    ShmSatSnapshot sat_positions[MAX_SATELLITES]; // live positions written by server
    int sat_count;
    Debris debris_field[MAX_DEBRIS];
    int debris_count;
} SharedMemory;

SharedMemory *shm_setup(void);

SharedMemory *shm_attach(void);

void shm_teardown(SharedMemory *shm);

void shm_detach(SharedMemory *shm);

int shm_write_alert(SharedMemory *shm, CollisionAlert *alert);

int shm_read_alert(SharedMemory *shm, CollisionAlert *out);

// called by the server whenever satellite positions change
void shm_update_sat_positions(SharedMemory *shm, ShmSatSnapshot *snaps, int count);

// adds a new debris object to the shared memory array
int shm_add_debris(SharedMemory *shm, int x, int y, int z, int vx, int vy, int vz);

// formats the list of all debris in shared memory into a string
void shm_format_debris(SharedMemory *shm, char *buffer, size_t max_len);

#endif