// implements the SIGUSR1 handler for collision avoidance

/* Must appear before any system header to expose POSIX extensions */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "signal_handler.h"

static SatelliteDB  *g_sat_db = NULL;
static SharedMemory *g_shm    = NULL;

// registers the SIGUSR1 handler and stores the pointers the handler will need
void signal_handler_init(SatelliteDB *db, SharedMemory *shm){
    if(db == NULL || shm == NULL){
        fprintf(stderr, "[SIG] ERROR: signal_handler_init called with NULL pointer.\n");
        return;
    }

    g_sat_db = db;
    g_shm    = shm;

    // sigaction() is POSIX-safe and lets us control exactly which flags are set (SA_RESTART restores interrupted syscalls)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // restart interrupted system calls automatically

    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        fprintf(stderr, "[SIG] ERROR: sigaction() failed — SIGUSR1 handler not registered.\n");
        return;
    }

    printf("[SIG] SIGUSR1 handler registered. Server PID: %d\n", getpid());
}

// called asynchronously by the OS the moment SIGUSR1 arrives from the debris monitor
void handle_sigusr1(int sig){
    (void)sig;

    const char *msg = "[SIG] SIGUSR1 received — collision alert incoming!\n";
    write(STDOUT_FILENO, msg, strlen(msg));

    if(g_sat_db == NULL || g_shm == NULL){
        const char *err = "[SIG] ERROR: handler fired before init — pointers are NULL.\n";
        write(STDERR_FILENO, err, strlen(err));
        return;
    }

    // read the CollisionAlert out of shared memory
    // shm_read_alert() acquires the process-shared mutex inside SharedMemory
    CollisionAlert alert;
    memset(&alert, 0, sizeof(CollisionAlert));

    if(!shm_read_alert(g_shm, &alert)){
        const char *err = "[SIG] ERROR: shm_read_alert() failed — no action taken.\n";
        write(STDERR_FILENO, err, strlen(err));
        return;
    }

    printf("[SIG] Alert read — debris %d will hit satellite %d in %ds (miss dist: %dm).\n", alert.debris_id, alert.sat_id, alert.time_to_impact_s, alert.miss_distance_m);
    printf("[SIG] Recommended evasion delta-V: (%d, %d, %d) m/s.\n", alert.evade_dvx, alert.evade_dvy, alert.evade_dvz);

    // fire the thrusters, sat_fire_thrusters() acquires the WRITE rwlock internally
    if(!sat_fire_thrusters(g_sat_db, alert.sat_id, alert.evade_dvx, alert.evade_dvy, alert.evade_dvz)){
        printf("[SIG] WARNING: sat_fire_thrusters() failed for satellite %d — satellite not found.\n", alert.sat_id);
        return;
    }

    printf("[SIG] Satellite %d thrusters fired successfully. Collision averted.\n", alert.sat_id);
}