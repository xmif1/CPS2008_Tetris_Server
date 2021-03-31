//
// Created by Xandru Mifsud on 31/03/2021.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#ifndef CPS2008_TETRIS_SERVER_H
#define CPS2008_TETRIS_SERVER_H

// NETWORKING CONFIG
#define PORT 6666
#define SDOMAIN AF_INET // or AF_INET6, correspondingly using netinet/in.h
#define TYPE SOCK_STREAM
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024

// STRUCTS
typedef struct{
    struct sockaddr_in clientaddrIn;
    int client_fd;
    char* nickname;
}client;

// FUNC DEFNS
int server_init();
int nickname_uniqueQ(char* nickname);
char* gen_nickname();
void* service_client(void* arg);
void add_client(int client_fd, struct sockaddr_in clientaddrIn, pthread_t* service_threads);
void sfunc_leaderboard(int argc, char *argv[]);
void sfunc_players(int argc, char *argv[]);
void sfunc_playerstats(int argc, char *argv[]);
void sfunc_battle(int argc, char *argv[]);
void sfunc_quick(int argc, char *argv[]);
void sfunc_chill(int argc, char *argv[]);
void sfunc_go(int argc, char *argv[]);
void sfunc_nickname(int argc, char *argv[]);
void sfunc_help(int argc, char *argv[]);
void mrerror(char* err_msg);
void red();
void reset();

// GLOBALS
client clients[MAX_CLIENTS]; pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int n_clients = 0; pthread_mutex_t n_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

char* sfunc_dict[] = {"!leaderboard", "!players", "!playerstats", "!battle", "!quick", "!chill", "!go", "!nickname",
                      "!help"};
void (*sfunc[])(int argc, char *argv[]) = {&sfunc_leaderboard, &sfunc_players, &sfunc_playerstats, &sfunc_battle,
                                           &sfunc_quick, &sfunc_chill, &sfunc_go, &sfunc_nickname, &sfunc_help};
struct sockaddr_in sockaddrIn = {.sin_family = SDOMAIN, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT)};


#endif //CPS2008_TETRIS_SERVER_H
