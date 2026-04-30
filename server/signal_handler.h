// This file declares the SIGUSR1 signal handler for the server.

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#define _POSIX_C_SOURCE 200809L

#include "common/shared_mem.h"
#include "satellite.h"

// call once after the SatelliteDB and SharedMemory are both ready
void signal_handler_init(SatelliteDB *db, SharedMemory *shm);

// the actual SIGUSR1 handler
void handle_sigusr1(int sig);

#endif
