// This file declares the command dispatcher. It is the single entry point the server calls after receiving a CommandPacket from a client socket. It checks permissions, routes to the right handler, and fills in the ResponsePacket to send back.

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

/* Expose POSIX.1-2008 extensions */
#define _POSIX_C_SOURCE 200809L

#include "../common/protocol.h"
#include "auth.h"
#include "satellite.h"
#include "ground_station.h"

// Bundles every shared server resource into one struct. The server creates one of these at startup and passes a pointer to every worker thread.
typedef struct {
    SatelliteDB *sat_db; // the satellite state array + RW lock
    GroundStationDB *gs_db; // the ground station array + per-station mutexes
} ServerContext;

// The main dispatch function. Called once per incoming CommandPacket. Checks the session, enforces role-based permissions, executes the command, and writes the result into *resp. The caller is responsible for sending *resp back over the socket.
void handle_command(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp);

#endif