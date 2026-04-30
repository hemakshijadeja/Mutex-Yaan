#define _POSIX_C_SOURCE 200809L

#include "shared_mem.h"

SharedMemory *shm_setup(void){
    int fd;
    SharedMemory *shm;
    pthread_mutexattr_t attr;

    fd = shm_open(SHM_NAME, O_CREAT|O_RDWR, SHM_PERMS);
    if(fd == -1){
        perror("shm_setup: shm_open failed");
        return NULL;
    }
    if(ftruncate(fd, sizeof(SharedMemory)) == -1){
        perror("shm_setup: ftruncate failed");
        close(fd);
        return NULL;
    }
    shm = (SharedMemory*)mmap(NULL, sizeof(SharedMemory), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if(shm == MAP_FAILED){
        perror("shm_setup: mmap failed");
        close(fd);
        return NULL;
    }
    close(fd);

    memset(shm, 0, sizeof(SharedMemory));
    shm->alert_ready = 0;
    shm->alert_count = 0;
    shm->child_alive = 0;
    shm->server_pid = getpid();
    
    // initialise default demo debris
    shm->debris_count = 6;
    Debris default_debris[6] = {
        {101,  7000,    4,    0,    0,  7000,    0},
        {102,    -3, 8000, 1000,-8000,     0,  200},
        {103, -4995, 5003, 2001,-3010,  5995,   98},
        {104,  3005,-5995,-1498,  998, -6998,  499},
        {105, -1998,-3998, 6001,-4998,  2001,-2999},
        {106,  6500,  300,  100,  200,  7400,   50}
    };
    memcpy(shm->debris_field, default_debris, sizeof(default_debris));

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    
    return shm;
}

SharedMemory *shm_attach(void){
    int fd;
    SharedMemory *shm;

    fd = shm_open(SHM_NAME, O_RDWR, SHM_PERMS);
    if(fd == -1){
        perror("shm_attach: shm_open failed");
        return NULL;
    }
    shm = (SharedMemory *) mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if(shm == MAP_FAILED){
        perror("shm_attach: mmap_failed");
        close(fd);
        return NULL;
    }
    close(fd);
    return shm;
}

void shm_teardown(SharedMemory *shm){
    if(shm == NULL) return;

    pthread_mutex_destroy(&shm->lock);
    munmap(shm, sizeof(SharedMemory));
    shm_unlink(SHM_NAME);
}

void shm_detach(SharedMemory *shm){
    if(shm == NULL) return;
    munmap(shm, sizeof(SharedMemory));
}

int shm_write_alert(SharedMemory *shm, CollisionAlert *alert){
    int should_signal;
    pid_t pid;
    if(shm == NULL || alert == NULL) return -1;
    pthread_mutex_lock(&shm->lock);
    shm->alert = *alert;
    shm->alert_ready = 1;
    shm->alert_count++;
    should_signal = shm->child_alive;
    pid = shm->server_pid;
    pthread_mutex_unlock(&shm->lock);
    if(pid > 0 && should_signal){
        kill(pid, SIGUSR1);
    }
    return 0;
}

int shm_read_alert(SharedMemory *shm, CollisionAlert *out){
    int found = 0;
    if(shm == NULL || out == NULL) return 0;
    pthread_mutex_lock(&shm->lock);
    if(shm->alert_ready == 1){
        *out = shm->alert;
        shm->alert_ready = 0;
        found = 1;
    }
    pthread_mutex_unlock(&shm->lock);
    return found;
}

void shm_update_sat_positions(SharedMemory *shm, ShmSatSnapshot *snaps, int count){
    if(shm == NULL || snaps == NULL || count <= 0) return;
    if(count > MAX_SATELLITES) count = MAX_SATELLITES;

    pthread_mutex_lock(&shm->lock);
    memcpy(shm->sat_positions, snaps, count * sizeof(ShmSatSnapshot));
    shm->sat_count = count;
    pthread_mutex_unlock(&shm->lock);
}

int shm_add_debris(SharedMemory *shm, int x, int y, int z, int vx, int vy, int vz){
    if(shm == NULL) return 0;
    int success = 0;

    pthread_mutex_lock(&shm->lock);
    if(shm->debris_count < MAX_DEBRIS){
        int idx = shm->debris_count;
        shm->debris_field[idx].debris_id = 101 + idx; // sequential IDs
        shm->debris_field[idx].x = x;
        shm->debris_field[idx].y = y;
        shm->debris_field[idx].z = z;
        shm->debris_field[idx].vx = vx;
        shm->debris_field[idx].vy = vy;
        shm->debris_field[idx].vz = vz;
        shm->debris_count++;
        success = 1;
    }
    pthread_mutex_unlock(&shm->lock);
    
    return success;
}

#define APPEND(...) do { \
    int n = snprintf(buffer + offset, max_len - offset, __VA_ARGS__); \
    if (n > 0) offset += ((size_t)n < max_len - offset) ? (size_t)n : (max_len - offset); \
} while(0)

void shm_format_debris(SharedMemory *shm, char *buffer, size_t max_len){
    if(shm == NULL || buffer == NULL || max_len == 0) return;
    
    size_t offset = 0;
    APPEND("\n=== ACTIVE SPACE DEBRIS ===\n");
    APPEND("ID   | Pos (x,y,z) km         | Vel (vx,vy,vz) m/s\n");
    APPEND("---------------------------------------------------\n");

    pthread_mutex_lock(&shm->lock);
    if(shm->debris_count == 0){
        APPEND("No debris currently tracked.\n");
    } else {
        for(int i = 0; i < shm->debris_count; i++){
            Debris *d = &shm->debris_field[i];
            APPEND("%-4d | %6d, %6d, %6d | %5d, %5d, %5d\n", d->debris_id, d->x, d->y, d->z, d->vx, d->vy, d->vz);
        }
    }
    pthread_mutex_unlock(&shm->lock);
}