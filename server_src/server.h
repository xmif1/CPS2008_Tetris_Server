//
// Created by Xandru Mifsud on 31/03/2021.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
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
#define MSG_LEN_DIGITS 4
#define HEADER_SIZE (MSG_LEN_DIGITS + 6) // '<msg_len>::<msg_type>::\0' where msg_len is of size MSG_LEN_DIGITS chars and msg_type is 1 char
#define UNAME_LEN 32

// GAME SESSION CONFIGS
#define INVITATION_EXP 30 // seconds
#define N_SESSION_PLAYERS 8
#define BASELINES_DEFAULT 2
#define WINLINES_DEFAULT 12
#define TIME_DEFAULT 5 // minutes

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
    char ip[INET_ADDRSTRLEN];
    int client_idx;
    char nickname[UNAME_LEN];
    int state;
    int score;
}ingame_client;

typedef struct{
    ingame_client* players[N_SESSION_PLAYERS];
    pthread_cond_t p2p_ready;
    int n_players_p2p_ready;
    int top_three[3];
    int game_idx;
    int game_type;
    int n_players;
    int n_baselines;
    int n_winlines;
    int time;
    int seed;
}game_session;

typedef struct{
    char nickname[UNAME_LEN];
    int score;
}leaderboard_entry;

typedef struct{
    leaderboard_entry top_three[3];
}leaderboard;

typedef struct{
    int msg_type;
    char* msg;
}msg;

// FUNC DEFNS
int server_init();
int nickname_uniqueQ(char nickname[UNAME_LEN]);
int handle_chat_msg(char* chat_msg, int client_idx);
int handle_score_update_msg(char* chat_msg, int client_idx);
int handle_finished_game_msg(char* chat_msg, int client_idx);
int handle_p2p_read_msg(char* chat_msg, int client_idx);
void* service_client(void* arg);
void* service_game_request(void* arg);
void gameFinishedQ(int game_idx, int remove_client_flag);
void sig_handler();
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
void sfunc_gamestats(int argc, char* argv[], int client_idx);
void sfunc_msg(int argc, char* argv[], int client_idx);
void client_msg(msg send_msg, int client_idx);
void mrerror(char* err_msg);
void smrerror(char* err_msg);
void red();
void yellow();
void reset();

// GLOBALS

int sig_raised = 0;

client* clients[MAX_CLIENTS]; int n_clients = 0;
game_session* games[MAX_CLIENTS];
pthread_t service_threads[MAX_CLIENTS];
pthread_t game_threads[MAX_CLIENTS];
pthread_mutex_t clientMutexes[MAX_CLIENTS];
pthread_mutex_t gameMutexes[MAX_CLIENTS];

leaderboard leaderboards[4];
pthread_mutex_t leaderboardMutex = PTHREAD_MUTEX_INITIALIZER;

#define N_SFUNCS 10
char* sfunc_dict[N_SFUNCS] = {"!leaderboard", "!players", "!playerstats", "!battle", "!quick", "!chill", "!go",
                              "!ignore", "!nickname", "!gamestats"};
void (*sfunc[])(int argc, char *argv[], int client_idx) = {&sfunc_leaderboard, &sfunc_players, &sfunc_playerstats,
                                                           &sfunc_battle, &sfunc_quick, &sfunc_chill, &sfunc_go,
                                                           &sfunc_ignore, &sfunc_nickname, &sfunc_gamestats};
enum MsgType {INVALID = -2, EMPTY = -1, CHAT = 0, SCORE_UPDATE = 1, NEW_GAME = 2, FINISHED_GAME = 3, P2P_READY = 4,
              CLIENTS_CONNECTED = 5, START_GAME = 6};
enum GameType {RISING_TIDE = 0, FAST_TRACK = 1, BOOMER = 2, CHILL = 3};
enum State {WAITING = 0, CONNECTED = 1, FINISHED = 2, DISCONNECTED = 3};

#endif //CPS2008_TETRIS_SERVER_H
