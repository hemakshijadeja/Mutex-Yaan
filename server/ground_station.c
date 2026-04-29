// implements all ground station functions

#include <stdio.h>    
#include <string.h>

#include "ground_station.h"

// searches the station array using station_id
static int findStation(GroundStationDB *db, int station_id){
    for(int i = 0; i < db->count; i++){
        if(db->stations[i].station_id == station_id) return i;
    }
    return -1;
}

// adds one ground station to the DB and initialises its mutex
static void addStation(GroundStationDB *db, int id, const char* name, int lat, int lon){
    if(db->count >= MAX_GROUND_STATIONS){
        fprintf(stderr, "[GS] ERROR: station table full, cannot add '%s'\n", name);
        return;
    }
    int i = db->count;
    db->stations[i].station_id = id;
    strcpy(db->stations[i].name, name);
    db->stations[i].latitude = lat;
    db->stations[i].longitude = lon;
    db->stations[i].busy = 0;

    // mutex for this station
    if(pthread_mutex_init(&db->stations[i].mutex, NULL) != 0){
        fprintf(stderr, "[GS] ERROR: mutex init failed for station '%s'\n", name);
        return;
    }

    db->count++;
}

// Loads demo ground stations and initialises a mutex for each one
void gs_db_init(GroundStationDB *db){
    memset(db, 0, sizeof(GroundStationDB)); 
    db->count = 0;

    addStation(db, 1, "Rajkot", 2233, 6456);
    addStation(db, 2, "Bengaluru", 1212, 1412);
    addStation(db, 3, "Mumbai", 2334, 1234);
    addStation(db, 4, "Delhi", 3445, 5443);
    addStation(db, 5, "Chennai", 2334, 3445);

    printf("[GS] Ground station database initialised with %d stations.\n", db->count);
}

// destroys all station's mutexes when the server shots down 
void gs_db_destroy(GroundStationDB *db){
    if (db == NULL) return;
    for(int i = 0; i < db->count; i++){
        pthread_mutex_destroy(&db->stations[i].mutex);
    }
    printf("[GS] Ground station database destroyed.\n");
}

// locks the mutex of the given station
int gs_lock(GroundStationDB *db, int station_id){
    if (db == NULL) {
        fprintf(stderr, "[GS] gs_lock: NULL db pointer\n");
        return 0;
    }
    int idx = findStation(db, station_id);
    if(idx < 0){
        fprintf(stderr, "[GS] gs_lock: station %d not found\n", station_id);
        return 0;
    }
    pthread_mutex_lock(&db->stations[idx].mutex);
    printf("[GS] Station %d ('%s') LOCKED by a thread — transmission starting.\n", station_id, db->stations[idx].name);
    return 1;
}

// releases the mutex 
int gs_unlock(GroundStationDB *db, int station_id){
    if(db == NULL) return 0;
    int index = findStation(db, station_id);
    if(index == -1){
        fprintf(stderr, "[GS] gs_unlock: station %d not found\n", station_id);
        return 0;
    }

    // pthread_mutex_unlock() releases the lock.
    pthread_mutex_unlock(&db->stations[index].mutex);

    printf("[GS] Station %d ('%s') UNLOCKED — transmission complete.\n",
           station_id, db->stations[index].name);

    return 1;
}

// copies one station's data into 'out' for display or logging
int gs_get(GroundStationDB *db, int station_id, GroundStation *out){
    if(db == NULL || out == NULL) return 0;
    int index = findStation(db, station_id);
    if(index == -1) return 0;
    *out = db->stations[index];
    return 1;
}

// updates the busy flag of a station
int gs_set_busy(GroundStationDB *db, int station_id, int busy){
    if (db == NULL) return 0;

    int index = findStation(db, station_id);
    if (index == -1) return 0;

    db->stations[index].busy = busy;
    return 1;
}

void gs_print_all(GroundStationDB *db){
    if (db == NULL) return;

    printf("\n+----+---------------------+------+-------+--------+\n");
    printf("| ID | Name                | Lat  | Long  | Status |\n");
    printf("+----+---------------------+------+-------+--------+\n");

    for(int i = 0; i < db->count; i++){
        printf("| %-2d | %-19s | %-4d | %-5d | %-6s |\n",
               db->stations[i].station_id,
               db->stations[i].name,
               db->stations[i].latitude,
               db->stations[i].longitude,
               db->stations[i].busy == 1 ? "BUSY" : "FREE");
    }
    printf("+----+---------------------+------+-------+--------+\n\n");
}