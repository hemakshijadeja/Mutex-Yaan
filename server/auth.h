// This file declares everything related to user authentication and role-based authorization.
// Any other .c file that needs to log a user in or check permissions should include this file

#ifndef AUTH_H
#define AUTH_H

/* Expose POSIX.1-2008 extensions */
#define _POSIX_C_SOURCE 200809L

#include "../common/protocol.h"

// When a client connects and logs in, the server creates one of these to remember who is on the other end of that socket connection.
typedef struct{
    int logged_in;
    int role;
    char username[MAX_USERNAME_LEN];
} Session;

void auth_init(void); // call this once when the server starts. It sets up a mock database.

// Checks the given username and password against the user table.
int auth_login(const char *username, const char *password, Session *session); //returns 1 on success, 0 on failure

// Clears the session so the client is no longer considered logged in. Should be called when the client sends CMD_LOGOUT or disconnects.
void auth_logout(Session *session);

// Checks whether the role stored in the session is high enough to run the given command.
int auth_check_permission(Session *session, int cmd); // 1 if allowed, 0 if denied.

// Returns a human-readable string for the given role number.
char *auth_role_name(int role);

#endif