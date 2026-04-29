// implements fcntl-based telemetry logging to a CSV file

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "telemetry_log.h"

// path set by telemetry_log_init()
static char log_filepath[256];

// pthread_mutex guards intra-process thread safety
// fcntl F_SETLKW guards inter-process safety
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// CSV column header
#define CSV_HEADER "timestamp,sat_id,x,y,z,vx,vy,vz,battery_pct,temp_c,cpu_pct,triggered_by\n"

int telemetry_log_init(const char *filepath){
    if(filepath == NULL){
        fprintf(stderr, "[TLOG] ERROR: telemetry_log_init called with NULL filepath.\n");
        return 0;
    }

    strncpy(log_filepath, filepath, sizeof(log_filepath) - 1);
    log_filepath[sizeof(log_filepath) - 1] = '\0';

    // open in read mode first, if the file already exists we leave it alone
    FILE *fp = fopen(log_filepath, "r");
    if(fp != NULL){
        fclose(fp);
        printf("[TLOG] Log file '%s' already exists — appending.\n", log_filepath);
        return 1;
    }

    // file does not exist yet, create it and write the header row
    fp = fopen(log_filepath, "w");
    if(fp == NULL){
        fprintf(stderr, "[TLOG] ERROR: could not create '%s'.\n", log_filepath);
        return 0;
    }

    fprintf(fp, CSV_HEADER);
    fclose(fp);
    printf("[TLOG] Log file '%s' created with CSV header.\n", log_filepath);
    return 1;
}

int telemetry_log_write(const SatelliteTelemetry *t, const char *triggered_by){
    if(t == NULL){
        fprintf(stderr, "[TLOG] ERROR: telemetry_log_write called with NULL telemetry pointer.\n");
        return 0;
    }
    if(log_filepath[0] == '\0'){
        fprintf(stderr, "[TLOG] ERROR: telemetry_log_init() was never called.\n");
        return 0;
    }

    pthread_mutex_lock(&log_mutex);

    // open in append mode so the file pointer always starts at EOF
    FILE *fp = fopen(log_filepath, "a");
    if(fp == NULL){
        fprintf(stderr, "[TLOG] ERROR: could not open '%s' for appending.\n", log_filepath);
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;  // exclusive write lock
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    if(fcntl(fileno(fp), F_SETLKW, &fl) == -1){
        fprintf(stderr, "[TLOG] ERROR: fcntl F_SETLKW failed for '%s'.\n", log_filepath);
        fclose(fp);
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    // format the timestamp
    char timestamp_str[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // write the CSV row — one atomic write under the lock
    fprintf(fp, "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
            timestamp_str,
            t->sat_id,
            t->x, t->y, t->z,
            t->vx, t->vy, t->vz,
            t->battery_percent,
            t->temperature_c,
            t->cpu_usage_percent,
            triggered_by ? triggered_by : "system");

    // release the OS-level lock before closing
    fl.l_type = F_UNLCK;
    fcntl(fileno(fp), F_SETLK, &fl);

    fclose(fp);
    pthread_mutex_unlock(&log_mutex);

    printf("[TLOG] Satellite %d telemetry written to '%s' by '%s'.\n", t->sat_id, log_filepath, triggered_by ? triggered_by : "system");
    return 1;
}
