#ifndef SATELLITE_H
#define SATELLITE_H

/* Expose POSIX.1-2008 extensions (pthread_rwlock_t, etc.) */
#define _POSIX_C_SOURCE 200809L

#include <time.h> 
#include <pthread.h>    
#include "../common/protocol.h"

// central shared data structure of the entire server
typedef struct{
    SatelliteTelemetry satellites[MAX_SATELLITES]; // the satellite array
    int count; // how many are active
    pthread_rwlock_t rwlock; // protects the array
} SatelliteDB;

// initialises the SatelliteDB struct and loads demo satellites
void sat_db_init(SatelliteDB *db);

// destroys the rwlock inside the DB
void sat_db_destroy(SatelliteDB *db);

// reads the telemetry of one satellite by its ID
int sat_get_telemetry(SatelliteDB *db, int sat_id, SatelliteTelemetry *out);

// Copies all active satellite records into the 'out' array and acquires a READ lock
int sat_get_all(SatelliteDB *db, SatelliteTelemetry out[]);

// updates the XYZ position and velocity of one satellite and acquires a WRITE lock
int sat_update_position(SatelliteDB *db, int sat_id, int x, int y, int z, int vx, int vy, int vz);

// Updates the battery, temperature and CPU usage of one satellite and acquires a WRITE lock
int sat_update_telemetry(SatelliteDB *db, int sat_id, int battery_pct, int temp_c, int cpu_pct);

// applies an emergency velocity change to a satellite, called when a collision alert arrives from the debris monitor, acquires a WRITE lock
int sat_fire_thrusters(SatelliteDB *db, int sat_id, int dvx, int dvy, int dvz);

// directly sets a new position and velocity for a satellite, WRITE lock
int sat_alter_orbit(SatelliteDB *db, int sat_id, int x, int y, int z, int vx, int vy, int vz);

// formats a table of all satellites into a buffer, READ lock
void sat_format_all(SatelliteDB *db, char *buffer, size_t max_len);

#endif