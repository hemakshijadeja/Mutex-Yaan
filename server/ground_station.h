#ifndef GROUND_STATION_H
#define GROUND_STATION_H

/* Expose POSIX.1-2008 extensions */
#define _POSIX_C_SOURCE 200809L

#include "../common/protocol.h"

// has the array of all ground stations in the system
typedef struct {
    GroundStation stations[MAX_GROUND_STATIONS]; // the station array  
    int count; // how many are loaded 
} GroundStationDB;

// initialises the GroundStationDB and loads demo stations
void gs_db_init(GroundStationDB *db);

// destroys the mutex of every station in the DB
void gs_db_destroy(GroundStationDB *db);

// locks the mutex of the given station so that this thread has exclusive access to it (the critical section)
int gs_lock(GroundStationDB *db, int station_id);

// releases the mutex of the given station
int gs_unlock(GroundStationDB *db, int station_id);

// formats a table of all active ground stations
void gs_format_all(GroundStationDB *db, char *buffer, size_t max_len);

// Copies the data of one station into out
int gs_get(GroundStationDB *db, int station_id, GroundStation *out);

// marks a station as busy or free
int gs_set_busy(GroundStationDB *db, int station_id, int busy);

// Prints a formatted table of all stations to the terminal.
void gs_print_all(GroundStationDB *db);

#endif