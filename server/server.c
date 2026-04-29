// server.c — Satellite Network Server
// ties together every subsystem

/* Must appear before any system header to expose POSIX extensions */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "../common/protocol.h"
#include "../common/shared_mem.h"
#include "auth.h"
#include "satellite.h"
#include "ground_station.h"
#include "command_handler.h"
#include "telemetry_log.h"
#include "signal_handler.h"

// defined in ipc/debris_monitor.c — the child process entry point
extern void run_debris_monitor(void);

// initialised once in main(), then read-only except where their own locks apply
static SatelliteDB g_sat_db;
static GroundStationDB g_gs_db;
static SharedMemory *g_shm = NULL;
static int g_listen_fd = -1;

// set to 0 by SIGINT to break the accept loop
static volatile sig_atomic_t g_server_running = 1;

// reads the live satellite positions out of g_sat_db under a READ lock and pushes them into shared memory 
static void sync_positions(void){
    ShmSatSnapshot snaps[MAX_SATELLITES];
    int n = 0;

    pthread_rwlock_rdlock(&g_sat_db.rwlock);
    for(int i = 0; i < MAX_SATELLITES && n < MAX_SATELLITES; i++){
        if(g_sat_db.satellites[i].active == 1){
            snaps[n].sat_id = g_sat_db.satellites[i].sat_id;
            snaps[n].x = g_sat_db.satellites[i].x;
            snaps[n].y = g_sat_db.satellites[i].y;
            snaps[n].z = g_sat_db.satellites[i].z;
            snaps[n].active = 1;
            n++;
        }
    }
    pthread_rwlock_unlock(&g_sat_db.rwlock);

    if(g_shm != NULL) shm_update_sat_positions(g_shm, snaps, n);
}

// SIGINT (Ctrl+C) stop the accept loop and trigger clean shutdown
static void handle_sigint(int sig){
    (void)sig;
    g_server_running = 0;
    if(g_listen_fd >= 0) close(g_listen_fd);
}

// SIGCHLD reap zombie child processes without blocking
static void handle_sigchld(int sig){
    (void)sig;
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

typedef struct{
    int client_fd;
    struct sockaddr_in client_addr;
} ClientThreadArgs;

static void *client_thread(void *arg){
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    int client_fd = args->client_fd;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &args->client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(args->client_addr.sin_port);
    free(args);

    printf("[SERVER] New connection from %s:%d (fd=%d)\n", client_ip, client_port, client_fd);

    // each connection gets its own unauthenticated session
    Session session;
    memset(&session, 0, sizeof(Session));
    session.logged_in = 0;
    session.role      = ROLE_GUEST;

    // the server context points at the global shared databases
    ServerContext ctx;
    ctx.sat_db = &g_sat_db;
    ctx.gs_db  = &g_gs_db;

    CommandPacket  cmd;
    ResponsePacket resp;

    while(1){
        memset(&cmd,  0, sizeof(cmd));
        memset(&resp, 0, sizeof(resp));

        // block until a full CommandPacket arrives — MSG_WAITALL prevents partial reads
        ssize_t received = recv(client_fd, &cmd, sizeof(CommandPacket), MSG_WAITALL);
        if(received == 0){
            printf("[SERVER] Client %s:%d disconnected.\n", client_ip, client_port);
            break;
        }
        if(received != (ssize_t)sizeof(CommandPacket)){
            perror("[SERVER] recv() error");
            break;
        }

        // the entire permission check and business logic is inside handle_command()
        handle_command(&ctx, &session, &cmd, &resp);

        // after any command that moves a satellite, push the new positions into shared memory so the debris monitor child works with fresh coordinates
        if(cmd.cmd == CMD_ALTER_ORBIT || cmd.cmd == CMD_FIRE_THRUSTERS){
            if(resp.code == RESP_OK) sync_positions();
        }

        ssize_t sent = send(client_fd, &resp, sizeof(ResponsePacket), 0);
        if(sent != (ssize_t)sizeof(ResponsePacket)){
            perror("[SERVER] send() error");
            break;
        }

        // CMD_LOGOUT closes this connection
        if(cmd.cmd == CMD_LOGOUT) break;
    }

    close(client_fd);
    printf("[SERVER] Connection with %s:%d closed.\n", client_ip, client_port);
    return NULL;
}

int main(void){
    printf("\n[SERVER] ╔══════════════════════════════════════════╗\n");
    printf("[SERVER] ║   Satellite Network Server starting...   ║\n");
    printf("[SERVER] ╚══════════════════════════════════════════╝\n\n");

    auth_init();
    sat_db_init(&g_sat_db);
    gs_db_init(&g_gs_db);

    if(!telemetry_log_init("logs/telemetry.csv")){
        fprintf(stderr, "[SERVER] ERROR: telemetry_log_init() failed. Exiting.\n");
        return 1;
    }

    // create the POSIX shared memory segment
    g_shm = shm_setup();
    if(g_shm == NULL){
        fprintf(stderr, "[SERVER] ERROR: shm_setup() failed. Exiting.\n");
        sat_db_destroy(&g_sat_db);
        gs_db_destroy(&g_gs_db);
        return 1;
    }

    // register the SIGUSR1 collision handler before forking
    signal_handler_init(&g_sat_db, g_shm);

    // push the initial satellite positions into shared memory now so the child has valid data from its very first scan cycle
    sync_positions();

    // fork the debris monitor child process
    pid_t child_pid = fork();
    if(child_pid < 0){
        perror("[SERVER] fork() failed");
        shm_teardown(g_shm);
        sat_db_destroy(&g_sat_db);
        gs_db_destroy(&g_gs_db);
        return 1;
    }

    if(child_pid == 0){
        // child process reset SIGUSR1 to default
        signal(SIGUSR1, SIG_DFL);
        run_debris_monitor();
        exit(0);
    }

    printf("[SERVER] Debris monitor child spawned (PID %d).\n", child_pid);

    // install SIGINT and SIGCHLD handlers (parent only)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = handle_sigint;
    sa.sa_flags   = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = handle_sigchld;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    // create and bind the listening TCP socket
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(g_listen_fd < 0){
        perror("[SERVER] socket() failed");
        kill(child_pid, SIGTERM);
        shm_teardown(g_shm);
        return 1;
    }

    // SO_REUSEADDR lets us restart the server immediately without "address already in use"
    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if(bind(g_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("[SERVER] bind() failed");
        close(g_listen_fd);
        kill(child_pid, SIGTERM);
        shm_teardown(g_shm);
        return 1;
    }

    if(listen(g_listen_fd, SERVER_BACKLOG) < 0){
        perror("[SERVER] listen() failed");
        close(g_listen_fd);
        kill(child_pid, SIGTERM);
        shm_teardown(g_shm);
        return 1;
    }

    printf("[SERVER] Listening on port %d. Ready for connections.\n\n", SERVER_PORT);

    // accept loop — one pthread per client
    while(g_server_running){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if(client_fd < 0){
            if(!g_server_running) break; // SIGINT closed the listen socket
            perror("[SERVER] accept() error");
            continue;
        }

        // allocate thread args on the heap
        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        if(args == NULL){
            fprintf(stderr, "[SERVER] ERROR: malloc() failed for thread args.\n");
            close(client_fd);
            continue;
        }
        args->client_fd = client_fd;
        args->client_addr = client_addr;

        pthread_t tid;
        if(pthread_create(&tid, NULL, client_thread, args) != 0){
            perror("[SERVER] pthread_create() failed");
            free(args);
            close(client_fd);
            continue;
        }
        // detach so threads free their own resources when they exit
        pthread_detach(tid);
    }

    // clean shutdown
    printf("\n[SERVER] Shutting down...\n");

    kill(child_pid, SIGTERM);
    waitpid(child_pid, NULL, 0);
    printf("[SERVER] Debris monitor child (PID %d) terminated.\n", child_pid);

    shm_teardown(g_shm);
    sat_db_destroy(&g_sat_db);
    gs_db_destroy(&g_gs_db);

    printf("[SERVER] All resources released. Goodbye.\n");
    return 0;
}