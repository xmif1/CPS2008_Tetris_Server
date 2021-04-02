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
#define PORT 8080
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
void sfunc_leaderboard(int argc, char* argv[], char* client_id);
void sfunc_players(int argc, char* argv[], char* client_id);
void sfunc_playerstats(int argc, char* argv[], char* client_id);
void sfunc_battle(int argc, char* argv[], char* client_id);
void sfunc_quick(int argc, char* argv[], char* client_id);
void sfunc_chill(int argc, char* argv[], char* client_id);
void sfunc_go(int argc, char* argv[], char* client_id);
void sfunc_nickname(int argc, char* argv[], char* client_id);
void sfunc_help(int argc, char* argv[], char* client_id);
void sfunc_msg(int argc, char* argv[], char* client_id);
void mrerror(char* err_msg);
void smrerror(char* err_msg);
void red();
void yellow();
void reset();

// GLOBALS
client* clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int n_clients = 0;

#define N_SFUNCS 9
char* sfunc_dict[N_SFUNCS] = {"!leaderboard", "!players", "!playerstats", "!battle", "!quick", "!chill", "!go",
                              "!nickname", "!help"};
void (*sfunc[])(int argc, char *argv[], char* client_id) = {&sfunc_leaderboard, &sfunc_players, &sfunc_playerstats,
                                                            &sfunc_battle, &sfunc_quick, &sfunc_chill, &sfunc_go,
                                                            &sfunc_nickname, &sfunc_help};

#endif //CPS2008_TETRIS_SERVER_H
