// implements the command dispatcher declared in command_handler.h.

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "command_handler.h"
#include "telemetry_log.h"

// Fills in a ResponsePacket quickly. Used by every handler below.
static void make_response(ResponsePacket *resp, int code, int sat_id, const char *msg){
    resp->code = code;
    resp->sat_id = sat_id;
    resp->timestamp = time(NULL);
    strncpy(resp->data, msg, sizeof(resp->data) - 1);
    resp->data[sizeof(resp->data) - 1] = '\0';
}

// CMD_LOGIN (1) — authenticate the client and populate the session
static void do_login(Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    if(auth_login(cmd->username, cmd->password, session)){
        char msg[MAX_RESPONSE_DATA_LEN];
        snprintf(msg, sizeof(msg), "Login successful. Welcome, %s. \nRole: %s.", session->username, auth_role_name(session->role));
        make_response(resp, RESP_AUTH_OK, 0, msg);
    } 
    else{
        make_response(resp, RESP_UNAUTHORIZED, 0, "Login failed. Invalid username or password.");
    }
}

// CMD_LOGOUT (2) — clear the session
static void do_logout(Session *session, ResponsePacket *resp){
    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Goodbye, %s.", session->username);
    auth_logout(session);
    make_response(resp, RESP_OK, 0, msg);
}

// CMD_GET_TELEMETRY (3) — read one satellite's telemetry (ROLE_GUEST+)
static void do_get_telemetry(ServerContext *ctx, const CommandPacket *cmd, ResponsePacket *resp){
    SatelliteTelemetry t;

    // sat_get_telemetry() acquires a READ lock internally, so concurrent reads from multiple clients are safe and never block each other
    if(!sat_get_telemetry(ctx->sat_db, cmd->sat_id, &t)){
        make_response(resp, RESP_NOT_FOUND, cmd->sat_id, "Satellite ID not found.");
        return;
    }

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "SAT %d | pos(%d,%d,%d) vel(%d,%d,%d) | bat:%d%% temp:%dC cpu:%d%%", t.sat_id,
             t.x, t.y, t.z,
             t.vx, t.vy, t.vz,
             t.battery_percent,
             t.temperature_c,
             t.cpu_usage_percent);
    make_response(resp, RESP_OK, t.sat_id, msg);
}

// CMD_GET_MAP (4) — snapshot of every active satellite's position (ROLE_GUEST+)
static void do_get_map(ServerContext *ctx, ResponsePacket *resp){
    SatelliteTelemetry all[MAX_SATELLITES];

    int n = sat_get_all(ctx->sat_db, all);

    if(n == 0){
        make_response(resp, RESP_OK, 0, "No active satellites.");
        return;
    }

    char msg[MAX_RESPONSE_DATA_LEN];
    int offset = 0;
    offset += snprintf(msg + offset, sizeof(msg) - offset, "%d active satellites: \n", n);

    for(int i = 0; i < n && offset < (int)sizeof(msg) - 1; i++){
        offset += snprintf(msg + offset, sizeof(msg) - offset, "[%d:(%d,%d,%d)]\n", all[i].sat_id, all[i].x, all[i].y, all[i].z);
    }
    make_response(resp, RESP_OK, 0, msg);
}

// CMD_LIST_SATS (5) — formats a table of satellites and sends it back (ROLE_GUEST+)
static void do_list_sats(ServerContext *ctx, ResponsePacket *resp){
    sat_format_all(ctx->sat_db, resp->data, sizeof(resp->data));
    resp->code = RESP_OK;
    resp->sat_id = 0;
    resp->timestamp = time(NULL);
}

// CMD_DUMP_TELEMETRY (6) — saves one satellite's telemetry to CSV under fcntl lock (ROLE_SPECIALIST+)
static void do_dump_telemetry(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    SatelliteTelemetry t;

    if(!sat_get_telemetry(ctx->sat_db, cmd->sat_id, &t)){
        make_response(resp, RESP_NOT_FOUND, cmd->sat_id, "Satellite ID not found — nothing to dump.");
        return;
    }

    // Lock the chosen ground station to simulate an exclusive transmission link
    int station_id = cmd->station_id;
    if(!gs_lock(ctx->gs_db, station_id)){
        make_response(resp, RESP_BAD_REQUEST, cmd->sat_id, "Invalid Ground Station ID. Please choose a valid station (1-5).");
        return;
    }

    // Simulate transmission time so the lock contention is visible
    sleep(2);

    // telemetry_log_write() acquires an fcntl F_SETLKW write lock (inter-process) before writing the CSV row
    if(!telemetry_log_write(&t, session->username)){
        gs_unlock(ctx->gs_db, station_id);
        make_response(resp, RESP_SERVER_ERROR, cmd->sat_id, "Failed to write telemetry to log file.");
        return;
    }

    gs_unlock(ctx->gs_db, station_id);

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Telemetry for satellite %d written to log via Station %d.", cmd->sat_id, station_id);
    make_response(resp, RESP_OK, cmd->sat_id, msg);
}

// CMD_ALTER_ORBIT (7) — reprograms a satellite's full state vector (ROLE_COMMANDER only)
static void do_alter_orbit(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    int x, y, z, vx, vy, vz;

    if(sscanf(cmd->payload, "%d %d %d %d %d %d", &x, &y, &z, &vx, &vy, &vz) != 6){
        make_response(resp, RESP_BAD_REQUEST, cmd->sat_id, "Bad payload. Expected: 'x y z vx vy vz'.");
        return;
    }

    if(!sat_alter_orbit(ctx->sat_db, cmd->sat_id, x, y, z, vx, vy, vz)){
        make_response(resp, RESP_NOT_FOUND, cmd->sat_id, "Satellite ID not found.");
        return;
    }

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Orbit altered for satellite %d by %s. New state: pos(%d,%d,%d) vel(%d,%d,%d).", cmd->sat_id, session->username, x, y, z, vx, vy, vz);
    make_response(resp, RESP_OK, cmd->sat_id, msg);
}

// CMD_FIRE_THRUSTERS (8) — applies a delta-V to a satellite (ROLE_COMMANDER only)
static void do_fire_thrusters(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    int dvx, dvy, dvz;

    if(sscanf(cmd->payload, "%d %d %d", &dvx, &dvy, &dvz) != 3){
        make_response(resp, RESP_BAD_REQUEST, cmd->sat_id, "Bad payload. Expected: 'dvx dvy dvz'.");
        return;
    }
    if(!sat_fire_thrusters(ctx->sat_db, cmd->sat_id, dvx, dvy, dvz)){
        make_response(resp, RESP_NOT_FOUND, cmd->sat_id, "Satellite ID not found.");
        return;
    }

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Thrusters fired on satellite %d by %s. Delta-V applied: (%d,%d,%d) m/s.", cmd->sat_id, session->username, dvx, dvy, dvz);
    make_response(resp, RESP_OK, cmd->sat_id, msg);
}

// CMD_REGISTER (9) — registers a new Guest user
static void do_register(Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    (void)session;
    if(auth_register(cmd->username, cmd->password)){
        make_response(resp, RESP_OK, 0, "Registration successful. You can now log in.");
    } else {
        make_response(resp, RESP_BAD_REQUEST, 0, "Registration failed. Username may be taken or table full.");
    }
}

// CMD_LIST_DEBRIS (10) — formats a table of debris and sends it back (ROLE_GUEST+)
static void do_list_debris(ServerContext *ctx, ResponsePacket *resp){
    shm_format_debris(ctx->shm, resp->data, sizeof(resp->data));
    resp->code = RESP_OK;
    resp->sat_id = 0;
    resp->timestamp = time(NULL);
}

// CMD_LIST_GS (11) — formats a table of ground stations and sends it back (ROLE_GUEST+)
static void do_list_gs(ServerContext *ctx, ResponsePacket *resp){
    gs_format_all(ctx->gs_db, resp->data, sizeof(resp->data));
    resp->code = RESP_OK;
    resp->sat_id = 0;
    resp->timestamp = time(NULL);
}

// CMD_ADD_SATELLITE (12) — adds a new satellite (ROLE_SPECIALIST+)
static void do_add_satellite(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    int x, y, z, vx, vy, vz;
    if(sscanf(cmd->payload, "%d %d %d %d %d %d", &x, &y, &z, &vx, &vy, &vz) != 6){
        make_response(resp, RESP_BAD_REQUEST, 0, "Bad payload. Expected: 'x y z vx vy vz'.");
        return;
    }
    if(sat_add(ctx->sat_db, x, y, z, vx, vy, vz)){
        char msg[MAX_RESPONSE_DATA_LEN];
        snprintf(msg, sizeof(msg), "Satellite added successfully by %s.", session->username);
        make_response(resp, RESP_OK, 0, msg);
    } else {
        make_response(resp, RESP_SERVER_ERROR, 0, "Failed to add satellite. Database full.");
    }
}

// CMD_ADD_DEBRIS (13) — adds new debris to the shared memory (ROLE_SPECIALIST+)
static void do_add_debris(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    int x, y, z, vx, vy, vz;
    if(sscanf(cmd->payload, "%d %d %d %d %d %d", &x, &y, &z, &vx, &vy, &vz) != 6){
        make_response(resp, RESP_BAD_REQUEST, 0, "Bad payload. Expected: 'x y z vx vy vz'.");
        return;
    }
    if(shm_add_debris(ctx->shm, x, y, z, vx, vy, vz)){
        char msg[MAX_RESPONSE_DATA_LEN];
        snprintf(msg, sizeof(msg), "Debris added successfully by %s.", session->username);
        make_response(resp, RESP_OK, 0, msg);
    } else {
        make_response(resp, RESP_SERVER_ERROR, 0, "Failed to add debris. Shared memory array full.");
    }
}

void handle_command(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    memset(resp, 0, sizeof(ResponsePacket));

    if(ctx == NULL || session == NULL || cmd == NULL || resp == NULL){
        fprintf(stderr, "[CMD] handle_command: received a NULL pointer — aborting.\n");
        make_response(resp, RESP_SERVER_ERROR, 0, "Internal server error.");
        return;
    }
    printf("[CMD] Received command %d from user '%s' (role: %s).\n", cmd->cmd, session->logged_in ? session->username : "<not logged in>", auth_role_name(session->role));

    if(cmd->cmd != CMD_LOGIN && cmd->cmd != CMD_REGISTER && !session->logged_in){
        make_response(resp, RESP_UNAUTHORIZED, 0, "Not logged in. Send CMD_LOGIN or CMD_REGISTER first.");
        return;
    }
    if(!auth_check_permission(session, cmd->cmd)){
        char msg[MAX_RESPONSE_DATA_LEN];
        snprintf(msg, sizeof(msg), "Access denied. Your role (%s) does not have permission for command %d.", auth_role_name(session->role), cmd->cmd);
        make_response(resp, RESP_FORBIDDEN, 0, msg);
        return;
    }
    switch(cmd->cmd){
        case CMD_LOGIN:
            do_login(session, cmd, resp);
            break;

        case CMD_LOGOUT:
            do_logout(session, resp);
            break;

        case CMD_GET_TELEMETRY:
            do_get_telemetry(ctx, cmd, resp);
            break;

        case CMD_GET_MAP:
            do_get_map(ctx, resp);
            break;

        case CMD_LIST_SATS:
            do_list_sats(ctx, resp);
            break;

        case CMD_DUMP_TELEMETRY:
            do_dump_telemetry(ctx, session, cmd, resp);
            break;

        case CMD_ALTER_ORBIT:
            do_alter_orbit(ctx, session, cmd, resp);
            break;

        case CMD_FIRE_THRUSTERS:
            do_fire_thrusters(ctx, session, cmd, resp);
            break;

        case CMD_REGISTER:
            do_register(session, cmd, resp);
            break;

        case CMD_LIST_DEBRIS:
            do_list_debris(ctx, resp);
            break;

        case CMD_LIST_GS:
            do_list_gs(ctx, resp);
            break;

        case CMD_ADD_SATELLITE:
            do_add_satellite(ctx, session, cmd, resp);
            break;

        case CMD_ADD_DEBRIS:
            do_add_debris(ctx, session, cmd, resp);
            break;

        default:
            fprintf(stderr, "[CMD] Unknown command code: %d\n", cmd->cmd);
            make_response(resp, RESP_BAD_REQUEST, 0, "Unknown command code.");
            break;
    }

    printf("[CMD] Response %d sent for command %d.\n", resp->code, cmd->cmd);
}