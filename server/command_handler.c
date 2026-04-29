// implements the command dispatcher declared in command_handler.h.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "command_handler.h"

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
        snprintf(msg, sizeof(msg), "Login successful. Welcome, %s. Role: %s.", session->username, auth_role_name(session->role));
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
    offset += snprintf(msg + offset, sizeof(msg) - offset, "%d active satellites: ", n);

    for(int i = 0; i < n && offset < (int)sizeof(msg) - 1; i++){
        offset += snprintf(msg + offset, sizeof(msg) - offset, "[%d:(%d,%d,%d)] ", all[i].sat_id, all[i].x, all[i].y, all[i].z);
    }
    make_response(resp, RESP_OK, 0, msg);
}

// CMD_LIST_SATS (5) — prints a formatted table server-side and sends a count (ROLE_GUEST+)
static void do_list_sats(ServerContext *ctx, ResponsePacket *resp){
    sat_print_all(ctx->sat_db);

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Satellite list printed to server terminal. %d total.", ctx->sat_db->count);
    make_response(resp, RESP_OK, 0, msg);
}

// CMD_DUMP_TELEMETRY (6) — triggers a telemetry save for one satellite (ROLE_SPECIALIST+)
static void do_dump_telemetry(ServerContext *ctx, const CommandPacket *cmd, ResponsePacket *resp){
    SatelliteTelemetry t;

    if(!sat_get_telemetry(ctx->sat_db, cmd->sat_id, &t)){
        make_response(resp, RESP_NOT_FOUND, cmd->sat_id, "Satellite ID not found — nothing to dump.");
        return;
    }

    char msg[MAX_RESPONSE_DATA_LEN];
    snprintf(msg, sizeof(msg), "Telemetry dump queued for satellite %d. (log write pending telemetry_log.c)", cmd->sat_id);
    make_response(resp, RESP_OK, cmd->sat_id, msg);
    printf("[CMD] Telemetry dump requested for satellite %d by '%s'.\n", cmd->sat_id, "session->username");
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


void handle_command(ServerContext *ctx, Session *session, const CommandPacket *cmd, ResponsePacket *resp){
    memset(resp, 0, sizeof(ResponsePacket));

    if(ctx == NULL || session == NULL || cmd == NULL || resp == NULL){
        fprintf(stderr, "[CMD] handle_command: received a NULL pointer — aborting.\n");
        make_response(resp, RESP_SERVER_ERROR, 0, "Internal server error.");
        return;
    }
    printf("[CMD] Received command %d from user '%s' (role: %s).\n", cmd->cmd, session->logged_in ? session->username : "<not logged in>", auth_role_name(session->role));

    if(cmd->cmd != CMD_LOGIN && !session->logged_in){
        make_response(resp, RESP_UNAUTHORIZED, 0, "Not logged in. Send CMD_LOGIN first.");
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
            do_dump_telemetry(ctx, cmd, resp);
            break;

        case CMD_ALTER_ORBIT:
            do_alter_orbit(ctx, session, cmd, resp);
            break;

        case CMD_FIRE_THRUSTERS:
            do_fire_thrusters(ctx, session, cmd, resp);
            break;

        default:
            fprintf(stderr, "[CMD] Unknown command code: %d\n", cmd->cmd);
            make_response(resp, RESP_BAD_REQUEST, 0, "Unknown command code.");
            break;
    }

    printf("[CMD] Response %d sent for command %d.\n", resp->code, cmd->cmd);
}