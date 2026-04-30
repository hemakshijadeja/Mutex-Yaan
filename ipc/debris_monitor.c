// the server calls fork() and the child runs run_debris_monitor()
// it simulates orbital debris trajectories, detects collisions writes a CollisionAlert into shared memory, and sends SIGUSR1 to the server PID

/* Must appear before any system header to expose POSIX extensions */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "../common/shared_mem.h"

// how often the monitor recalculates trajectories (seconds)
#define SCAN_INTERVAL_S 5

// how close a debris object has to come before it is a threat (metres)
#define COLLISION_THRESHOLD_M 5000

// number of simulated debris objects in the environment
#define NUM_DEBRIS 6

// simulated debris object
typedef struct{
    int debris_id;
    int x, y, z; 
    int vx, vy, vz;
} Debris;

// static debris field
static Debris debris_field[NUM_DEBRIS] = {
    {101,  7001,    5,    2,  -50,  7490,   10},
    {102,   100, 8010, 1005, 7190,   -20,  195},
    {103, -4995, 5003, 2001,-3010,  5995,   98},
    {104,  3005,-5995,-1498,  998, -6998,  499},
    {105, -1998,-3998, 6001,-4998,  2001,-2999},
    {106,  6500,  300,  100,  200,  7400,   50}
};

// returns the miss distance in metres, or -1 if no threat
static int check_collision(Debris *d, ShmSatSnapshot *s){
    long dx = ((long)d->x - s->x) * 1000;
    long dy = ((long)d->y - s->y) * 1000;
    long dz = ((long)d->z - s->z) * 1000;
    long dist_sq = dx*dx + dy*dy + dz*dz;

    long dist = 0;
    long r = dist_sq;
    if(r > 0){
        long q = r;
        dist = 1;
        while(dist < q / dist) dist++;
    }

    if(dist <= COLLISION_THRESHOLD_M) return (int)dist;
    return -1;
}

// builds a recommended evasion delta-V — a simple nudge away from the debris
static void compute_evasion(Debris *d, ShmSatSnapshot *s, int *dvx, int *dvy, int *dvz){
    int rx = s->x - d->x; // move in the direction opposite to the debris relative position
    int ry = s->y - d->y;
    int rz = s->z - d->z;
    *dvx = (rx != 0) ? (rx > 0 ? 50 : -50) : 0;
    *dvy = (ry != 0) ? (ry > 0 ? 50 : -50) : 0;
    *dvz = (rz != 0) ? (rz > 0 ? 50 : -50) : 0;
}

// advances the debris positions by one time step using simple Euler integration
static void step_debris(int dt_s){
    for(int i = 0; i < NUM_DEBRIS; i++){
        debris_field[i].x += (debris_field[i].vx * dt_s) / 1000;
        debris_field[i].y += (debris_field[i].vy * dt_s) / 1000;
        debris_field[i].z += (debris_field[i].vz * dt_s) / 1000;
    }
}

// the main loop run by the child process after fork()
void run_debris_monitor(void){
    printf("[DM] Debris monitor child started. PID: %d\n", getpid());

    // attach to the shared memory segment the server already created with shm_setup()
    SharedMemory *shm = shm_attach();
    if(shm == NULL){
        fprintf(stderr, "[DM] ERROR: shm_attach() failed — debris monitor exiting.\n");
        exit(1);
    }

    pthread_mutex_lock(&shm->lock);
    shm->child_alive = 1;
    printf("[DM] Attached to shared memory. Server PID: %d\n", shm->server_pid);
    pthread_mutex_unlock(&shm->lock);

    // seed the RNG used to randomise the demo impact timer
    srand((unsigned int)time(NULL));

    while(1){
        // take a local snapshot of the live satellite positions from shared memory
        // holding the lock only long enough to copy — keeps the mutex window tiny
        ShmSatSnapshot live_sats[MAX_SATELLITES];
        int live_count = 0;
        pthread_mutex_lock(&shm->lock);
        live_count = shm->sat_count;
        if(live_count > 0) memcpy(live_sats, shm->sat_positions, live_count * sizeof(ShmSatSnapshot));
        pthread_mutex_unlock(&shm->lock);

        if(live_count == 0){
            printf("[DM] No satellite positions in shared memory yet — waiting.\n");
            sleep(SCAN_INTERVAL_S);
            continue;
        }

        // scan every (debris, satellite) pair for proximity
        for(int d = 0; d < NUM_DEBRIS; d++){
            for(int s = 0; s < live_count; s++){
                if(!live_sats[s].active) continue;
                int miss_m = check_collision(&debris_field[d], &live_sats[s]);
                if(miss_m >= 0){
                    CollisionAlert alert;
                    memset(&alert, 0, sizeof(CollisionAlert));

                    alert.debris_id = debris_field[d].debris_id;
                    alert.sat_id = live_sats[s].sat_id;
                    alert.debris_x = debris_field[d].x;
                    alert.debris_y = debris_field[d].y;
                    alert.debris_z = debris_field[d].z;
                    alert.sat_x = live_sats[s].x;
                    alert.sat_y = live_sats[s].y;
                    alert.sat_z = live_sats[s].z;
                    alert.miss_distance_m = miss_m;
                    alert.time_to_impact_s = SCAN_INTERVAL_S + (rand() % 10);

                    compute_evasion(&debris_field[d], &live_sats[s], &alert.evade_dvx, &alert.evade_dvy, &alert.evade_dvz);

                    snprintf(alert.description, sizeof(alert.description), "Debris %d -> Sat %d, dist %dm", alert.debris_id, alert.sat_id, miss_m);

                    printf("[DM] COLLISION THREAT: %s\n", alert.description);

                    if(shm_write_alert(shm, &alert) != 0){
                        fprintf(stderr, "[DM] ERROR: shm_write_alert() failed.\n");
                    }
                    else{
                        printf("[DM] Alert written to shared memory. SIGUSR1 sent to PID %d.\n", shm->server_pid);
                    }
                }
            }
        }
        step_debris(SCAN_INTERVAL_S);
        sleep(SCAN_INTERVAL_S);
    }

    shm_detach(shm);
    printf("[DM] Debris monitor exiting.\n");
    exit(0);
}
