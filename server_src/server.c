#include "server.h"

int main(){
    int socket_fd = server_init();

    // Initialise clients array to NULLs
    for(int i = 0; i < MAX_CLIENTS; i++){
        clients[i] = NULL;
    }

    // Main loop listening for client connections, ready to accept them as sufficient resources become available.
    while(1){
        struct sockaddr_in clientaddrIn;
        socklen_t sizeof_clientaddrIn = sizeof(struct sockaddr_in);

        int client_fd = accept(socket_fd, (struct sockaddr*) &clientaddrIn, &sizeof_clientaddrIn);
        if(client_fd < 0){
            mrerror("Error on attempt to accept client connection");
        }

        add_client(client_fd, clientaddrIn);
    }

    return 0;
    // How to gracefully close connections when 'server shuts down'?
}

int server_init(){
    int socket_fd;

    printf("-----------------------------------------\n"
           "Initialising server...\n");

    // Create socket
    socket_fd = socket(SDOMAIN, TYPE, 0);
    if(socket_fd < 0){
        mrerror("Socket initialisation failed");
    }

    struct sockaddr_in sockaddrIn = {.sin_family = SDOMAIN, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(8080)};

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

void add_client(int client_fd, struct sockaddr_in clientaddrIn){
    if(n_clients < MAX_CLIENTS - 1){ // if further resource constraints exist, add them here
        pthread_mutex_lock(&threadMutex);
        int i = 0;
        for(; i < MAX_CLIENTS; i++){
            if(clients[i] == NULL){
                break;
            }
        }

        clients[i] = malloc(sizeof(client));
        clients[i]->client_fd = client_fd;
        clients[i]->client_idx = i;
        clients[i]->clientaddrIn = clientaddrIn;
        clients[i]->game_idx = -1;
        clients[i]->high_score = 0;
        clients[i]->n_wins = 0;
        clients[i]->n_losses = 0;

        char nickname[UNAME_LEN] = {0}; gen_nickname(nickname);
        strcpy(clients[i]->nickname, nickname);

        if(pthread_create(service_threads + i, NULL, service_client, (void*) clients[i]) != 0){
            mrerror("Error while creating thread to service newly connected client");
        }

        n_clients++;

        pthread_mutex_unlock(&threadMutex);
    }else{
        msg err_msg;
        err_msg.msg_type = CHAT;
        strcpy(err_msg.msg, "Maximum number of clients acheived: unable to connect at the moment.");

        if(send(client_fd, (void*) &err_msg, sizeof(msg), 0) < 0){
            smrerror("Error encountered while communicating with new client");
        }

        close(client_fd);
    }
}

void remove_client(int client_idx){
    pthread_mutex_lock(&threadMutex);
    if(clients[client_idx] != NULL){
        close(clients[client_idx].client_fd);

        free(clients[client_idx]);
        clients[client_idx] = NULL;
        n_clients--;
    }
    pthread_mutex_unlock(&threadMutex);
}

/* -------- THREADED FUNCTIONS -------- */

void* service_client(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int client_fd = ((client*) arg)->client_fd;
    int client_idx = ((client*) arg)->client_idx;

    msg recv_msg;
    while(recv(client_fd, (msg*) &recv_msg, sizeof(msg), 0) > 0){
        if(recv_msg.msg[0] == '!'){
            char *token = strtok(recv_msg.msg, " ");
            char **token_list = malloc(0);
            int n_tokens = 0;

            while(token != NULL){
                token_list = realloc(token_list, (n_tokens+1)*sizeof(char*));

                if(token_list != NULL){
                    token_list[n_tokens] = token;
                    token = strtok(NULL, " ");
                    n_tokens++;
                }else{
                    smrerror("Error during tokenisation of client message");
                }
            }

            if(strcmp(token_list[0], "!exit") == 0){
                break;
            }else{
                int msg_flag = 1;

                for(int i = 0; i < N_SFUNCS; i++){
                    if(strcmp(token_list[0], sfunc_dict[i]) == 0){
                        (*sfunc)(n_tokens, token_list, client_idx);
                        msg_flag = 0;
                        break;
                    }
                }

                if(msg_flag){
                    sfunc_msg(1, (char *[]) {recv_msg.msg}, client_idx);
                }
            }
        }else{
            sfunc_msg(1, (char*[]){recv_msg.msg}, client_idx);
        }
    }

    remove_client(client_idx);
    pthread_exit(NULL);
}

void* service_game_request(void* arg){
    game_session game = *((game_session*) arg);
    sleep(INVITATION_EXP);

    int n_players = 1;
    pthread_mutex_lock(&(gameMutexes + game.game_idx));
    for(int i = 0; i < 8; i++){
        if(((games[game.game_idx].opponents)[i] != NULL) && ((games[game.game_idx].opponents)[i].state == REJECTED)){
            free((games[game.game_idx].opponents)[i]);
            (games[game.game_idx].opponents)[i] = NULL;
        }
        else{
            n_players++;
        }
    }
    pthread_mutex_unlock(&(gameMutexes + game.game_idx));

    if(n_players == 1){
        msg send_msg;
        send_msg.msg_type = CHAT;
        strcpy(send_msg.msg, "Insufficient number of opponents have joined the game session.");
        client_msg(send_msg, game.host.client_idx);

        pthread_mutex_lock(&threadMutex);
        clients[game.host.client_idx]->game_idx = -1;
        free(games[game.game_idx])
        games[game.game_idx] = NULL;
        pthread_mutex_unlock(&threadMutex);
    }
    // else: send game request to all etc etc...TO-DO.

    pthread_exit(NULL);
}

/* ------ SERVER FUNCTIONS (sfunc) ------ */

void sfunc_leaderboard(int argc, char* argv[], int client_idx){}

void sfunc_players(int argc, char* argv[], int client_idx){
    msg send_msg;
    send_msg.msg_type = CHAT;
    strcpy(send_msg.msg, "Waiting Players:");

    pthread_mutex_lock(&threadMutex);
    for(int i = 0; i < MAX_CLIENTS; i++){
        if((clients[i] != NULL) && (clients[i]->game_idx < 0)){
            strcat(send_msg.msg, "\n\t");
            strcat(send_msg.msg, clients[i]->nickname);
        }
    }
    pthread_mutex_unlock(&threadMutex);

    client_msg(send_msg, client_idx);
}

void sfunc_playerstats(int argc, char* argv[], int client_idx){}
void sfunc_battle(int argc, char* argv[], int client_idx){}
void sfunc_quick(int argc, char* argv[], int client_idx){}
void sfunc_chill(int argc, char* argv[], int client_idx){}

void sfunc_go(int argc, char* argv[], int client_idx) {
    msg send_msg;
    send_msg.msg_type = CHAT;

    if(argc < 2){
        strcpy(send_msg.msg, "Please specify the game id.");
        client_msg(send_msg, client_idx);
    }else{
        int game_idx = strtol(argv[1]);
        if(game_idx < 0 || MAX_CLIENTS <= game_idx){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = 0;

            pthread_mutex_lock(&(gameMutexes + game_idx));
            for(int i = 0; i < 8; i++){
                if((games[game_idx]->opponents)[i].client_idx == client_idx){
                    registered_in_game = 1;
                    pthread_mutex_lock(&threadMutex);
                    if((clients[client_idx] != NULL) && (clients[client_idx]->game_idx < 0)){
                        clients[client_idx]->game_idx = game_idx;
                        (games[game_idx]->opponents)[i].state = CONNECTED;
                        pthread_mutex_unlock(&threadMutex);
                    }else{
                        pthread_mutex_unlock(&threadMutex);

                        strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
                        client_msg(send_msg, client_idx);
                    }
                }
            }
            pthread_mutex_unlock(&(gameMutexes + game_idx));

            if(!registered_in_game){
                strcpy(send_msg.msg, "Invalid game id: you are not registered to this game session.");
                client_msg(send_msg, client_idx);
            }
        }
    }
}

void sfunc_ignore(int argc, char* argv[], int client_idx){
    msg send_msg;
    send_msg.msg_type = CHAT;

    if(argc < 2){
        strcpy(send_msg.msg, "Please specify the game id.");
        client_msg(send_msg, client_idx);
    }else{
        int game_idx = strtol(argv[1]);
        if(game_idx < 0 || MAX_CLIENTS <= game_idx){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = -1;

            pthread_mutex_lock(&(gameMutexes + game_idx));
            for(int i = 0; i < 8; i++){
                if((games[game_idx]->opponents)[i].client_idx == client_idx){
                    registered_in_game = i;
                }
            }

            if(registered_in_game < 0){
                strcpy(send_msg.msg, "Invalid game id: you are not registered to this game session.");
                client_msg(send_msg, client_idx);
            }
            else{
                strcpy(send_msg.msg, "Player ");
                strcat(send_msg.msg, (games[game_idx]->opponents)[registered_in_game].nickname);
                strcat(send_msg.msg, " has declined to join game session ");
                strcat(send_msg.msg, argv[1]);
                strcat(send_msg.msg, ".");

                for(int i = 0; i < 8; i++){
                    if((games[game_idx]->opponents)[i].client_idx != client_idx){
                        client_msg(send_msg, (games[game_idx]->opponents)[i].client_idx);
                    }
                }
            }
            pthread_mutex_unlock(&(gameMutexes + game_idx));
        }
    }
}

void sfunc_nickname(int argc, char* argv[], int client_idx){}
void sfunc_help(int argc, char* argv[], int client_idx){}

void sfunc_msg(int argc, char* argv[], int client_idx){
    pthread_mutex_lock(&threadMutex);
    if(clients[client_idx] != NULL){
        char nickname[UNAME_LEN] = {0};
        strcpy(nickname, clients[client_idx].nickname);

        for(int i = 0; i < MAX_CLIENTS; i++){
            if(clients[i] != NULL){
                msg send_msg;
                send_msg.msg_type = CHAT;
                strcpy(send_msg.msg, nickname);
                strcat(send_msg.msg, ">\t");
                strcat(send_msg.msg, argv[0]);

                if(send(clients[i]->client_fd, (void *) &send_msg, sizeof(msg), 0) < 0){
                    pthread_cancel(service_threads[i]);
                    pthread_mutex_unlock(&threadMutex);
                    remove_client(i);
                    pthread_mutex_lock(&threadMutex);
                }
            }
        }
    }
    pthread_mutex_unlock(&threadMutex);
}

/* --------- UTILITY FUNCTIONS --------- */

void gen_nickname(char nickname[UNAME_LEN]){
    char* keywords1[6] = {"Big", "Little", "Cool", "Lame", "Happy", "Sad"};
    char* keywords2[5] = {"Mac", "Muppet", "Hobbit", "Wizard", "Elf"};
    int not_unique = 1;

    while(not_unique){
        int i = rand() % 6, j = rand() % 5, k = (rand() % MAX_CLIENTS) + 1;
        char str_k[(int) floor(log10(k))+2];
        sprintf(str_k, "%d", k);

        strcpy(nickname, keywords1[i]);
        strcat(nickname, keywords2[j]);
        strcat(nickname, str_k);

        not_unique = nickname_uniqueQ(nickname);
    }
}

// Returns 1 if the nickname is not unique, 0 otherwise.
int nickname_uniqueQ(char nickname[UNAME_LEN]){
    for(int i = 0; i < MAX_CLIENTS; i++){
       if(!clients[i]){
           continue;
       }
       else if(strcmp(nickname, clients[i]->nickname) == 0){
           return 1;
       }
    }

    return 0;
}

void client_msg(msg send_msg, int client_idx){
    pthread_mutex_lock(&threadMutex);
    if((clients[client_idx] != NULL) && (send(clients[client_idx]->client_fd, (void*) &send_msg, sizeof(msg), 0) < 0)){
        pthread_cancel(service_threads[client_idx]);
        pthread_mutex_unlock(&threadMutex);
        remove_client(client_idx);
    }else{
        pthread_mutex_unlock(&threadMutex);
    }
}

/* ----------- ERROR HANDLING ----------- */

/* Mr. Error: A simple function to handle errors (mostly a wrapper to perror), and terminate.*/
void mrerror(char* err_msg){
    red();
    perror(err_msg);
    reset();
    exit(EXIT_FAILURE);
}

/* Silent Mr. Error: A simple function to handle errors (mostly a wrapper to perror), without termination.*/
void smrerror(char* err_msg){
    yellow();
    perror(err_msg);
    reset();
}

// The following functions are used to highlight text outputted to the console, for reporting errors and warnings.

void red(){
    printf("\033[1;31m");
}

void yellow(){
    printf("\033[1;33m");
}

void reset(){
    printf("-----");
    printf("\033[0m\n");
}
