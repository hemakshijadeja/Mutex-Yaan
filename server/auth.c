// This file implements user authentication and role-based authorization.

#include <stdio.h>
#include <string.h>

#include "auth.h"

// the server's in-memory database of user accounts
static UserRecord user_table[MAX_USERS];
static int user_count = 0;

// this array defines the minimun role needed to run each command
int min_role_for_cmd[9] = {
    ROLE_GUEST,
    ROLE_GUEST,
    ROLE_GUEST,
    ROLE_GUEST,
    ROLE_GUEST,
    ROLE_SPECIALIST,
    ROLE_SPECIALIST,
    ROLE_COMMANDER,
    ROLE_COMMANDER
};

// private helper that adds one user account to the user table
// only called inside auth_init() below
static void add_user(char *username, char *password, int role){
    if(user_count >= MAX_USERS){
        fprintf(stderr, "add_user: user table is full, cannot add '%s'\n", username);
        return;
    }
    strcpy(user_table[user_count].username, username);
    strcpy(user_table[user_count].password, password);
    user_table[user_count].role = role;
    user_table[user_count].active = 1;
    user_count++;
}

// loads the user table with demo accounts at server startup
void auth_init(void){
    memset(user_table, 0, sizeof(user_table));
    user_count = 0;

    add_user("user1", "pass1", ROLE_COMMANDER);
    add_user("user2", "pass2", ROLE_COMMANDER);
    add_user("user3", "pass3", ROLE_SPECIALIST);
    add_user("user4", "pass4", ROLE_SPECIALIST);
    add_user("user5", "pass5", ROLE_GUEST);
    add_user("user6", "pass6", ROLE_GUEST);

    printf("[AUTH] User table loaded with %d accounts.\n", user_count);
}

// searches the user table for a matching username + password.
int auth_login(const char *username, const char *password, Session *session){
    if(username == NULL || password == NULL || session == NULL){
        return 0;
    }
    for(int i=0; i<user_count; i++){
        if(strcmp(user_table[i].username, username) == 0 && user_table[i].active == 1){
            if(strcmp(user_table[i].password, password) == 0){
                session->logged_in = 1;
                session->role = user_table[i].role;
                strcpy(session->username, user_table[i].username);
                printf("[AUTH] Login SUCCESS - user: '%s' role: %s\n", session->username, auth_role_name(session->role));
                return 1;

            }
            else{
                printf("[AUTH] Login FAILED - user: '%s' (wrong password)\n", username);
                return 0;
            }
        }
    }
    printf("[AUTH] Login FAILED - user: '%s' (username not found)\n", username);
    return 0;
}

// clears the session struct so the client is no longer logged in
void auth_logout(Session *session){
    if(session == NULL) return;
    printf("[AUTH] User '%s' logged out.\n", session->username);
    memset(session, 0, sizeof(Session));
    session->logged_in = 0;
    session->role = ROLE_GUEST;
}

// before the server runs any command, it calls this function, basically the core of role-based auth
int auth_check_permission(Session *session, int cmd){
    int required_role;
    if(session == NULL) return 0;
    if(cmd == CMD_LOGIN) return 1;
    if(session->logged_in == 0){
        printf("[AUTH] Permission DENIED - not logged in (cmd=%d)\n", cmd);
        return 0;
    }
    if(cmd < 1 || cmd > 8){
        printf("[AUTH] Permission DENIED - unknown command code %d\n", cmd);
        return 0;
    }

    required_role = min_role_for_cmd[cmd];
    if(session->role >= required_role){
        return 1;
    }
    else{
        printf("[AUTH] Permission DENIED — user '%s' has role '%s' but cmd %d requires '%s'\n", session->username, auth_role_name(session->role), cmd, auth_role_name(required_role));
        return 0;
    }
}

// converts a role integer into a readable string
char *auth_role_name(int role){
    switch (role){
        case ROLE_COMMANDER: return "Mission Commander";
        case ROLE_SPECIALIST: return "Payload Specialist";
        case ROLE_GUEST: return "Guest";
        default: return "Unknown Role";
    }
}