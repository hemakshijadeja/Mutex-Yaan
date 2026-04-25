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