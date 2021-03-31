#include "server.h"

int main(){
    int socket_fd = server_init();
}

int server_init(){
    int socket_fd;

    printf("-----------------------------------------\n"
           "Initialising server...\n");

    // Create socket
    if((socket_fd = socket(DOMAIN, TYPE, 0)) < 0){
        mrerror("Socket initialisation failed");
    }

    // Then (in darkness) bind it...
    if(bind(socket_fd, (struct sockaddr*) &sockaddrIn, sizeof(sockaddrIn)) < 0){
        mrerror("Socket binding failed");
    }

    // Finally, listen.
    if(listen(socket_fd, 5) < 0){
        mrerror("Listening on socket failed");
    }

    printf("Server started successfully.\n"
           "-----------------------------------------\n\n");

    return socket_fd;
}

/* ------ SERVER FUNCTIONS (sfunc) ------ */

void sfunc_leaderboard(int argc, char *argv[]){}
void sfunc_players(int argc, char *argv[]){}
void sfunc_playerstats(int argc, char *argv[]){}
void sfunc_battle(int argc, char *argv[]){}
void sfunc_quick(int argc, char *argv[]){}
void sfunc_chill(int argc, char *argv[]){}
void sfunc_go(int argc, char *argv[]){}
void sfunc_nickname(int argc, char *argv[]){}
void sfunc_help(int argc, char *argv[]){}

/* ----------- ERROR HANDLING ----------- */

/* Mr. Error: A simple function to handle errors (mostly a wrapper to perror), and terminate.*/
void mrerror(char* err_msg){
    red();
    perror(err_msg);
    reset();
    exit(EXIT_FAILURE);
}

// The following functions are used to highlight text outputted to the console, for reporting errors and warnings.

void red(){
    printf("\033[1;31m");
}

void reset(){
    printf("-----");
    printf("\033[0m\n");
}

