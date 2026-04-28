// Implements all satellite state functions declared in satellite.h

#include <stdio.h>    
#include <string.h>
#include <time.h>
#include "satellite.h"

// searches the satellite array for a given sat_id
static int find_satellite(SatelliteDB *db, int sat_id){
    for(int i = 0; i < MAX_SATELLITES; i++){
        if(db->satellites[i].active == 1 && db->satellites[i].sat_id == sat_id) return i;
    }
    return -1;
}

void sat_db_init(SatelliteDB *db){
    int i;
    memset(db, 0, sizeof(SatelliteDB));

    // initialise the read-write lock
    if(pthread_rwlock_init(&db->rwlock, NULL) != 0){
        fprintf(stderr, "[SAT] ERROR: failed to initialise rwlock\n");
        return;
    }

    // Satellite 1
    i = 0;
    db->satellites[i].sat_id = 1;
    db->satellites[i].x = 7000; // km from Earth centre
    db->satellites[i].y = 0;
    db->satellites[i].z = 0;
    db->satellites[i].vx = 0; 
    db->satellites[i].vy = 7500;
    db->satellites[i].vz = 0;
    db->satellites[i].battery_pct = 95;
    db->satellites[i].temperature_c = 22;
    db->satellites[i].cpu_usage_pct = 30;
    db->satellites[i].active = 1;
    db->satellites[i].last_updated = time(NULL);

    // Satellite 2
    i = 1;
    db->satellites[i].sat_id = 2;
    db->satellites[i].x = 0;
    db->satellites[i].y = 8000;
    db->satellites[i].z = 1000;
    db->satellites[i].vx = 7200; 
    db->satellites[i].vy = 0;
    db->satellites[i].vz = 200;
    db->satellites[i].battery_pct = 80;
    db->satellites[i].temperature_c = 18;
    db->satellites[i].cpu_usage_pct = 55;
    db->satellites[i].active = 1;
    db->satellites[i].last_updated = time(NULL);

    // Satellite 3
    i = 2;
    db->satellites[i].sat_id = 3;
    db->satellites[i].x = -5000;
    db->satellites[i].y = 5000;
    db->satellites[i].z = 2000;
    db->satellites[i].vx = -3000; 
    db->satellites[i].vy = 6000;
    db->satellites[i].vz = 100;
    db->satellites[i].battery_pct = 60;
    db->satellites[i].temperature_c = 35;
    db->satellites[i].cpu_usage_pct = 70;
    db->satellites[i].active = 1;
    db->satellites[i].last_updated = time(NULL);

    // Satellite 4
    i = 3;
    db->satellites[i].sat_id = 4;
    db->satellites[i].x = 3000;
    db->satellites[i].y = -6000;
    db->satellites[i].z = -1500;
    db->satellites[i].vx = 1000; 
    db->satellites[i].vy = -7000;
    db->satellites[i].vz = 500;
    db->satellites[i].battery_pct = 45;
    db->satellites[i].temperature_c = 10;
    db->satellites[i].cpu_usage_pct = 85;
    db->satellites[i].active = 1;
    db->satellites[i].last_updated = time(NULL);

    // Satellite 5
    i = 4;
    db->satellites[i].sat_id = 5;
    db->satellites[i].x = -2000;
    db->satellites[i].y = -4000;
    db->satellites[i].z = 6000;
    db->satellites[i].vx = -5000; 
    db->satellites[i].vy = 2000;
    db->satellites[i].vz = -3000;
    db->satellites[i].battery_pct = 100;
    db->satellites[i].temperature_c = 28;
    db->satellites[i].cpu_usage_pct = 15;
    db->satellites[i].active = 1;
    db->satellites[i].last_updated = time(NULL);

    db->count = 5;

    printf("[SAT] Satellite database initialised with %d satellites.\n", db->count);
}

void sat_db_destroy(SatelliteDB *db){
    if(db == NULL) return;
    pthread_rwlock_destroy(&db->rwlock);
    printf("[SAT] Satellite database destroyed.\n");
}

int sat_get_telemetry(SatelliteDB *db, int sat_id, SatelliteTelemetry *out){
    int index;
    if(db == NULL || out == NULL) return 0;
    
    // acquire READ lock, multiple threads can hold this simultaneously
    pthread_rwlock_rdlock(&db->rwlock);

    index = find_satellite(db, sat_id);
    if(index == -1){
        pthread_rwlock_unlock(&db->rwlock);
        return 0; //satellite not found
    }

    // Copy the data out before releasing the lock
    *out = db->satellites[index];

    pthread_rwlock_unlock(&db->rwlock);
    return 1;
}

int sat_get_all(SatelliteDB *db, SatelliteTelemetry out[]){
    int copied = 0;
    if(db == NULL || out == NULL) return 0;

    pthread_rwlock_rdlock(&db->rwlock);

    for(int i = 0; i < MAX_SATELLITES; i++){
        if(db->satellites[i].active == 1){
            out[copied] = db->satellites[i];
            copied++;
        }
    }

    pthread_rwlock_unlock(&db->rwlock);
    return copied;
}

int sat_update_position(SatelliteDB *db, int sat_id, int x, int y, int z, int vx, int vy, int vz){
    int index;
    if(db == NULL) return 0;

    // acquire WRITE lock, it blocks all readers and other writers
    pthread_rwlock_wrlock(&db->rwlock);

    index = find_satellite(db, sat_id);
    if(index == -1){
        pthread_rwlock_unlock(&db->rwlock);
        return 0;
    }

    // Update all six values atomically under the write lock
    // no reader can see a partial update
    db->satellites[index].x = x;
    db->satellites[index].y = y;
    db->satellites[index].z = z;
    db->satellites[index].vx = vx;
    db->satellites[index].vy = vy;
    db->satellites[index].vz = vz;
    db->satellites[index].last_updated = time(NULL);

    pthread_rwlock_unlock(&db->rwlock);

    printf("[SAT] Satellite %d position updated to (%d, %d, %d)\n", sat_id, x, y, z);
    return 1;
}

int sat_update_telemetry(SatelliteDB *db, int sat_id, int battery_pct, int temp_c, int cpu_pct){
    if(db == NULL) return 0;

    pthread_rwlock_wrlock(&db->rwlock);

    int index = find_satellite(db, sat_id);
    if(index == -1){
        pthread_rwlock_unlock(&db->rwlock);
        return 0;
    }

    db->satellites[index].battery_pct = battery_pct;
    db->satellites[index].temperature_c = temp_c;
    db->satellites[index].cpu_usage_pct = cpu_pct;
    db->satellites[index].last_updated = time(NULL);

    pthread_rwlock_unlock(&db->rwlock);

    printf("[SAT] Satellite %d telemetry updated — battery:%d%% temp:%dC cpu:%d%%\n", sat_id, battery_pct, temp_c, cpu_pct);
    return 1;
}

int sat_fire_thrusters(SatelliteDB *db, int sat_id, int dvx, int dvy, int dvz){
    if(db == NULL) return 0;

    pthread_rwlock_wrlock(&db->rwlock);

    int index = find_satellite(db, sat_id);
    if(index == -1){
        pthread_rwlock_unlock(&db->rwlock);
        return 0;
    }

    db->satellites[index].vx = db->satellites[index].vx + dvx;
    db->satellites[index].vy = db->satellites[index].vy + dvy;
    db->satellites[index].vz = db->satellites[index].vz + dvz;
    db->satellites[index].last_updated = time(NULL);

    pthread_rwlock_unlock(&db->rwlock);

    printf("[SAT] Satellite %d thrusters fired — delta-V applied (%d, %d, %d) m/s\n", sat_id, dvx, dvy, dvz);
    return 1;
}

int sat_alter_orbit(SatelliteDB *db, int sat_id, int x, int y, int z, int vx, int vy, int vz){
    if(db == NULL) return 0;

    pthread_rwlock_wrlock(&db->rwlock);

    int index = find_satellite(db, sat_id);
    if(index == -1){
        pthread_rwlock_unlock(&db->rwlock);
        return 0;
    }

    db->satellites[index].x = x;
    db->satellites[index].y = y;
    db->satellites[index].z = z;
    db->satellites[index].vx = vx;
    db->satellites[index].vy = vy;
    db->satellites[index].vz = vz;
    db->satellites[index].last_updated = time(NULL);

    pthread_rwlock_unlock(&db->rwlock);

    printf("[SAT] Satellite %d orbit altered — new pos (%d, %d, %d) new vel (%d, %d, %d)\n", sat_id, x, y, z, vx, vy, vz);
    return 1;
}

// to be changed
void sat_print_all(SatelliteDB *db){
    int found = 0;
    if(db == NULL) return;

    pthread_rwlock_rdlock(&db->rwlock);

    printf("\n+------+----------+----------+----------+-------+-------+-------+\n");
    printf("| ID   | X (km)   | Y (km)   | Z (km)   | Bat%% | Temp  | CPU%% |\n");
    printf("+------+----------+----------+----------+-------+-------+-------+\n");

    for(int i = 0; i < MAX_SATELLITES; i++){
        if(db->satellites[i].active == 1){
            printf("| %-4d | %-8d | %-8d | %-8d | %-5d | %-5d | %-5d |\n",
                   db->satellites[i].sat_id,
                   db->satellites[i].x,
                   db->satellites[i].y,
                   db->satellites[i].z,
                   db->satellites[i].battery_pct,
                   db->satellites[i].temperature_c,
                   db->satellites[i].cpu_usage_pct);
            found = 1;
        }
    }

    if(found == 0){
        printf("| No active satellites in the database.                         |\n");
    }

    printf("+------+----------+----------+----------+-------+-------+-------+\n\n");

    pthread_rwlock_unlock(&db->rwlock);
}