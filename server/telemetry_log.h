// This file declares the telemetry logging interface

#ifndef TELEMETRY_LOG_H
#define TELEMETRY_LOG_H

/* Expose POSIX.1-2008 extensions (fileno, fcntl locks) */
#define _POSIX_C_SOURCE 200809L

#include "../common/protocol.h"

// creates the CSV file and writes the header row if it doesn't exist yet
int telemetry_log_init(const char *filepath);

// appends one satellite telemetry row to the CSV under an fcntl write lock
int telemetry_log_write(const SatelliteTelemetry *t, const char *triggered_by);

#endif