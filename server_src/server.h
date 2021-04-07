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
#define MSG_SIZE 1024
#define UNAME_LEN 32

// STRUCTS
typedef struct{
    struct sockaddr_in clientaddrIn;
    int client_fd;
    int client_idx;
    int game_idx;
    int n_wins;
    int n_losses;
    int high_score;
    char nickname[UNAME_LEN];
}client;

typedef struct{
    int client_idx;
    char nickname[UNAME_LEN];
    int state;
    int score;
}ingame_client;

typedef struct{
    ingame_client* players[8];
    int game_idx;
    int game_type;
    int n_baselines;
    int n_winlines;
    int time;
}game_session;

typedef struct{
    int msg_type;
    char msg[MSG_SIZE];
}msg;

// FUNC DEFNS
int server_init();
int nickname_uniqueQ(char nickname[UNAME_LEN]);
int handle_chat_msg(char chat_msg[MSG_SIZE], int client_idx);
int handle_score_update_msg(char chat_msg[MSG_SIZE], int client_idx);
int handle_finished_game_msg(char chat_msg[MSG_SIZE], int client_idx);
void* service_client(void* arg);
void* service_game_request(void* arg);
void gen_nickname(char nickname[UNAME_LEN]);
void add_client(int client_fd, struct sockaddr_in clientaddrIn);
void remove_client(int client_idx);
void sfunc_leaderboard(int argc, char* argv[], int client_idx);
void sfunc_players(int argc, char* argv[], int client_idx);
void sfunc_playerstats(int argc, char* argv[], int client_idx);
void sfunc_battle(int argc, char* argv[], int client_idx);
void sfunc_quick(int argc, char* argv[], int client_idx);
void sfunc_chill(int argc, char* argv[], int client_idx);
void sfunc_go(int argc, char* argv[], int client_idx);
void sfunc_ignore(int argc, char* argv[], int client_idx);
void sfunc_nickname(int argc, char* argv[], int client_idx);
void sfunc_help(int argc, char* argv[], int client_idx);
void sfunc_msg(int argc, char* argv[], int client_idx);
void client_msg(msg send_msg, int client_idx);
void mrerror(char* err_msg);
void smrerror(char* err_msg);
void red();
void yellow();
void reset();

// GLOBALS
client* clients[MAX_CLIENTS]; int n_clients = 0;
game_session* games[MAX_CLIENTS];
pthread_t service_threads[MAX_CLIENTS];
pthread_t game_threads[MAX_CLIENTS];
pthread_mutex_t clientMutexes[MAX_CLIENTS];
pthread_mutex_t gameMutexes[MAX_CLIENTS];

#define INVITATION_EXP 30 // seconds
#define BASELINES_DEFAULT 2
#define WINLINES_DEFAULT 12
#define TIME_DEFAULT 5 // minutes

#define N_SFUNCS 10
char* sfunc_dict[N_SFUNCS] = {"!leaderboard", "!players", "!playerstats", "!battle", "!quick", "!chill", "!go",
                              "!ignore", "!nickname", "!help"};
void (*sfunc[])(int argc, char *argv[], int client_idx) = {&sfunc_leaderboard, &sfunc_players, &sfunc_playerstats,
                                                           &sfunc_battle, &sfunc_quick, &sfunc_chill, &sfunc_go,
                                                           &sfunc_ignore, &sfunc_nickname, &sfunc_help};
enum MsgType {CHAT = 0, SCORE_UPDATE = 1, FINISHED_GAME = 2};
enum GameType {RISING_TIDE = 0, FAST_TRACK = 1, BOOMER = 2};
enum State {REJECTED = 0, CONNECTED = 1, FINISHED = 2};

#endif //CPS2008_TETRIS_SERVER_H
