#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <time.h>
#include <pthread.h>

#define MAX_SATELLITES 64
#define MAX_GROUND_STATIONS 8
#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 32
#define MAX_CMD_PAYLOAD_LEN 256
#define MAX_RESPONSE_DATA_LEN 512
#define MAX_USERS  64

#define SERVER_PORT 8080
#define SERVER_BACKLOG 10

#define ROLE_GUEST 0
#define ROLE_SPECIALIST 1
#define ROLE_COMMANDER 2

#define CMD_LOGIN 1
#define CMD_LOGOUT 2
#define CMD_GET_TELEMETRY 3
#define CMD_GET_MAP 4
#define CMD_LIST_SATS 5
#define CMD_DUMP_TELEMETRY 6
#define CMD_ALTER_ORBIT 7
#define CMD_FIRE_THRUSTERS 8

#define RESP_OK 200
#define RESP_AUTH_OK 201
#define RESP_BAD_REQUEST 400
#define RESP_NOT_FOUND 404
#define RESP_UNAUTHORIZED 401
#define RESP_FORBIDDEN 403
#define RESP_SERVER_ERROR 500

extern int min_role_for_cmd[9];

typedef struct {
    int cmd;
    int sat_id;
    int station_id;
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char payload[MAX_CMD_PAYLOAD_LEN];
} CommandPacket;

typedef struct {
    int code;
    int sat_id;
    time_t timestamp;
    char data[MAX_RESPONSE_DATA_LEN];
} ResponsePacket;

typedef struct {
    int sat_id;
    int x,  y,  z;
    int vx, vy, vz;
    int battery_percent;
    int temperature_c;
    int cpu_usage_percent;
    int active;
    time_t last_updated;
} SatelliteTelemetry;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int role;
    int active;
} UserRecord;

typedef struct {
    char name[32];
    int station_id;
    int latitude;
    int longitude;
    int busy;
    pthread_mutex_t mutex;
} GroundStation;

#endif