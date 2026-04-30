// Ground Station Client — connects to the Satellite Network Server, logs in, and lets the operator send commands interactively.

/* Must appear before any system header to expose POSIX extensions */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../common/protocol.h"

#define SERVER_IP "127.0.0.1"

// opens a TCP connection to the server and returns the socket fd, or -1 on failure
static int connect_to_server(void){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        perror("[CLIENT] socket() failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);

    if(inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0){
        perror("[CLIENT] inet_pton() failed");
        close(sockfd);
        return -1;
    }

    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("[CLIENT] connect() failed");
        close(sockfd);
        return -1;
    }

    printf("[CLIENT] Connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);
    return sockfd;
}

// sends a CommandPacket and blocks until a ResponsePacket comes back
static int send_command(int sockfd, CommandPacket *cmd, ResponsePacket *resp){
    ssize_t sent = send(sockfd, cmd, sizeof(CommandPacket), 0);
    if(sent != sizeof(CommandPacket)){
        perror("[CLIENT] send() failed");
        return 0;
    }

    ssize_t received = recv(sockfd, resp, sizeof(ResponsePacket), MSG_WAITALL);
    if(received != sizeof(ResponsePacket)){
        if(received == 0)
            fprintf(stderr, "[CLIENT] Server closed the connection.\n");
        else
            perror("[CLIENT] recv() failed");
        return 0;
    }
    return 1;
}

static void print_response(ResponsePacket *resp){
    char time_str[32];
    struct tm *tm_info = localtime(&resp->timestamp);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    printf("\nServer Response: \n");
    printf("Code : %d", resp->code);
    switch(resp->code){
        case RESP_OK: printf(" (OK)"); break;
        case RESP_AUTH_OK: printf(" (AUTH OK)"); break;
        case RESP_BAD_REQUEST: printf(" (BAD REQUEST)"); break;
        case RESP_UNAUTHORIZED: printf(" (UNAUTHORIZED)"); break;
        case RESP_FORBIDDEN: printf(" (FORBIDDEN)"); break;
        case RESP_NOT_FOUND: printf(" (NOT FOUND)"); break;
        case RESP_SERVER_ERROR: printf(" (SERVER ERROR)"); break;
        default: printf(" (UNKNOWN)"); break;
    }
    printf("\nTime : %s\n", time_str);
    if(resp->sat_id > 0) printf("│ Sat  : %d\n", resp->sat_id);
    printf("Data : %s\n", resp->data);
    printf("\n");
}

// prompts for credentials, sends CMD_LOGIN, returns 1 on success
static int do_login(int sockfd, char *username_out){
    CommandPacket cmd;
    ResponsePacket resp;
    memset(&cmd, 0, sizeof(cmd));

    printf("\n=== Ground Station Login ===\n");
    printf("Username: ");
    if(fgets(cmd.username, sizeof(cmd.username), stdin) == NULL) return 0;
    cmd.username[strcspn(cmd.username, "\n")] = '\0';

    printf("Password: ");
    if(fgets(cmd.password, sizeof(cmd.password), stdin) == NULL) return 0;
    cmd.password[strcspn(cmd.password, "\n")] = '\0';

    cmd.cmd = CMD_LOGIN;

    if(!send_command(sockfd, &cmd, &resp)) return 0;
    print_response(&resp);

    if(resp.code == RESP_AUTH_OK){
        strncpy(username_out, cmd.username, MAX_USERNAME_LEN - 1);
        return 1;
    }
    return 0;
}

static int do_register(int sockfd){
    CommandPacket cmd;
    ResponsePacket resp;
    memset(&cmd, 0, sizeof(cmd));

    printf("\n=== Guest Registration ===\n");
    printf("Username: ");
    if(fgets(cmd.username, sizeof(cmd.username), stdin) == NULL) return 0;
    cmd.username[strcspn(cmd.username, "\n")] = '\0';

    printf("Password: ");
    if(fgets(cmd.password, sizeof(cmd.password), stdin) == NULL) return 0;
    cmd.password[strcspn(cmd.password, "\n")] = '\0';

    cmd.cmd = CMD_REGISTER;

    if(!send_command(sockfd, &cmd, &resp)) return 0;
    print_response(&resp);
    return (resp.code == RESP_OK);
}

static void print_menu(int role){
    printf("╔══════════════════════════════════════════╗\n");
    printf("║                MUTEX-YAAN                ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║  1  GET_TELEMETRY                        ║\n");
    printf("║  2  GET_MAP                              ║\n");
    printf("║  3  LIST_SATS                            ║\n");
    printf("║  4  LIST_DEBRIS                          ║\n");
    printf("║  5  LIST_GROUND_STATIONS                 ║\n");
    if(role >= ROLE_SPECIALIST){
        printf("║  6  ADD_SATELLITE                        ║\n");
        printf("║  7  ADD_DEBRIS                           ║\n");
        printf("║  8  DUMP_TELEMETRY                       ║\n");
    }
    if(role >= ROLE_COMMANDER){
        printf("║  9  ALTER_ORBIT                          ║\n");
        printf("║ 10  FIRE_THRUSTERS                       ║\n");
    }
    printf("║  0  QUIT                                 ║\n");
    printf("╚══════════════════════════════════════════╝\n");
}

// reads a single integer from stdin, returns -1 on bad input
static int read_int(const char *prompt){
    char buf[64];
    printf("%s", prompt);
    if(fgets(buf, sizeof(buf), stdin) == NULL) return -1;
    return atoi(buf);
}

static void cmd_get_telemetry(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_GET_TELEMETRY;
    cmd.sat_id = read_int("Satellite ID: ");
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_get_map(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_GET_MAP;
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_list_sats(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_LIST_SATS;
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_list_debris(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_LIST_DEBRIS;
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_list_gs(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_LIST_GS;
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_add_satellite(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_ADD_SATELLITE;
    printf("Enter new satellite state (x y z vx vy vz): ");
    if(fgets(cmd.payload, sizeof(cmd.payload), stdin) == NULL) return;
    cmd.payload[strcspn(cmd.payload, "\n")] = '\0';
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_add_debris(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_ADD_DEBRIS;
    printf("Enter new debris state (x y z vx vy vz): ");
    if(fgets(cmd.payload, sizeof(cmd.payload), stdin) == NULL) return;
    cmd.payload[strcspn(cmd.payload, "\n")] = '\0';
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_dump_telemetry(int sockfd, int station_id){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_DUMP_TELEMETRY;
    cmd.sat_id = read_int("Satellite ID to dump: ");
    cmd.station_id = station_id;
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_alter_orbit(int sockfd, int station_id){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_ALTER_ORBIT;
    cmd.sat_id = read_int("Satellite ID: ");
    cmd.station_id = station_id;
    printf("Enter new state vector (x y z vx vy vz): ");
    if(fgets(cmd.payload, sizeof(cmd.payload), stdin) == NULL) return;
    cmd.payload[strcspn(cmd.payload, "\n")] = '\0';
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static void cmd_fire_thrusters(int sockfd, int station_id){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_FIRE_THRUSTERS;
    cmd.sat_id = read_int("Satellite ID: ");
    cmd.station_id = station_id;
    printf("Enter delta-V (dvx dvy dvz) in m/s: ");
    if(fgets(cmd.payload, sizeof(cmd.payload), stdin) == NULL) return;
    cmd.payload[strcspn(cmd.payload, "\n")] = '\0';
    if(!send_command(sockfd, &cmd, &resp)) return;
    print_response(&resp);
}

static int cmd_logout(int sockfd){
    CommandPacket cmd; ResponsePacket resp; memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CMD_LOGOUT;
    if(!send_command(sockfd, &cmd, &resp)) return 0;
    print_response(&resp);
    return 1;
}

int main(void){
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║           Welcome to Mutex-Yaan          ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    int sockfd = connect_to_server();
    if(sockfd < 0){
        fprintf(stderr, "[CLIENT] Could not connect to server. Is it running on port %d?\n", SERVER_PORT);
        return 1;
    }

    int role = read_int("Role (0=Guest, 1=Specialist, 2=Commander): ");

    int logged_in = 0;
    char username[MAX_USERNAME_LEN] = {0};

    while(!logged_in){
        if(role == ROLE_GUEST){
            printf("\n[1] Login\n[2] Register\n[0] Quit\n");
            int choice = read_int("Choice: ");
            if(choice == 1){
                logged_in = do_login(sockfd, username);
            } else if(choice == 2){
                do_register(sockfd);
            } else if(choice == 0){
                close(sockfd);
                return 0;
            } else {
                printf("Invalid choice.\n");
            }
        } else {
            printf("\n[1] Login\n[0] Quit\n");
            int choice = read_int("Choice: ");
            if(choice == 1){
                logged_in = do_login(sockfd, username);
            } else if(choice == 0){
                close(sockfd);
                return 0;
            } else {
                printf("Invalid choice.\n");
            }
        }
    }

    int my_station_id = 0;
    if(role >= ROLE_SPECIALIST){
        my_station_id = read_int("\nEnter Ground Station ID (1-5): ");
    }

    // main interactive loop
    int running = 1;
    while(running){
        print_menu(role);
        int choice = read_int("Choice: ");

        switch(choice){
            case 1: cmd_get_telemetry(sockfd); break;
            case 2: cmd_get_map(sockfd); break;
            case 3: cmd_list_sats(sockfd); break;
            case 4: cmd_list_debris(sockfd); break;
            case 5: cmd_list_gs(sockfd); break;
            case 6: 
                if(role >= ROLE_SPECIALIST) cmd_add_satellite(sockfd); 
                else printf("Invalid choice.\n"); 
                break;
            case 7: 
                if(role >= ROLE_SPECIALIST) cmd_add_debris(sockfd); 
                else printf("Invalid choice.\n"); 
                break;
            case 8: 
                if(role >= ROLE_SPECIALIST) cmd_dump_telemetry(sockfd, my_station_id); 
                else printf("Invalid choice.\n"); 
                break;
            case 9: 
                if(role >= ROLE_COMMANDER) cmd_alter_orbit(sockfd, my_station_id); 
                else printf("Invalid choice.\n"); 
                break;
            case 10: 
                if(role >= ROLE_COMMANDER) cmd_fire_thrusters(sockfd, my_station_id); 
                else printf("Invalid choice.\n"); 
                break;
            case 0:
                printf("[CLIENT] Logging out and quitting...\n");
                cmd_logout(sockfd);
                running = 0;
                break;
            default:
                printf("[CLIENT] Unknown option. Enter a number from the menu.\n");
                break;
        }
    }

    close(sockfd);
    printf("[CLIENT] Connection closed. Goodbye.\n");
    return 0;
}
