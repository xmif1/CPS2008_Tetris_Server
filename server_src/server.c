#include "server.h"

int main(){
    int socket_fd = server_init();

    // Initialise clients and games arrays to NULLs, as well as game session thread mutexes
    for(int i = 0; i < MAX_CLIENTS; i++){
        clients[i] = NULL;
        games[i] = NULL;
        gameMutexes[i] = PTHREAD_MUTEX_INITIALIZER;
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
        close(clients[client_idx]->client_fd);

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

    int break_loop = 0;
    msg recv_msg;
    while(recv(client_fd, (msg*) &recv_msg, sizeof(msg), 0) > 0){
        switch(recv_msg.msg_type){
            case CHAT: break_loop = handle_chat_msg(recv_msg.msg, client_idx); break;
            case SCORE_UPDATE: break_loop = handle_score_update_msg(recv_msg.msg, client_idx); break;
            case FINISHED_GAME: break_loop = handle_finished_game_msg(recv_msg.msg, client_idx); break;
            default: break_loop = 0;
        }

        if(break_loop){
            break;
        }
    }

    remove_client(client_idx);
    pthread_exit(NULL);
}

void* service_game_request(void* arg){
    game_session game = *((game_session*) arg);

    // TO--DO: ADD SENDING OF INVITATION

    sleep(INVITATION_EXP);

    int n_players = 1;
    pthread_mutex_lock(gameMutexes + game.game_idx);
    for(int i = 0; i < 8; i++){
        if(((games[game.game_idx]->opponents)[i] != NULL) && ((games[game.game_idx]->opponents)[i]->state == REJECTED)){
            free((games[game.game_idx]->opponents)[i]);
            (games[game.game_idx]->opponents)[i] = NULL;
        }
        else{
            n_players++;
        }
    }
    pthread_mutex_unlock(gameMutexes + game.game_idx);

    msg send_msg;
    send_msg.msg_type = CHAT;
    if(n_players == 1){
        strcpy(send_msg.msg, "Insufficient number of opponents have joined the game session.");
        client_msg(send_msg, game.host.client_idx);

        pthread_mutex_lock(&threadMutex);
        clients[game.host.client_idx]->game_idx = -1;
        free(games[game.game_idx]);
        games[game.game_idx] = NULL;
        pthread_mutex_unlock(&threadMutex);
    }
    // else: send game request to all etc etc...TO-DO.
    else{ // for now, simply print the players that will join the session
        pthread_mutex_lock(gameMutexes + game.game_idx);
        for(int i = 0; i < 8; i++){
            if((games[game.game_idx]->opponents)[i] != NULL){
                strcpy(send_msg.msg, "You have successfully joined the game session...starting soon...");
                client_msg(send_msg, (games[game.game_idx]->opponents)[i]->client_idx);
            }
        }
        pthread_mutex_unlock(gameMutexes + game.game_idx);
    }

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

void sfunc_battle(int argc, char* argv[], int client_idx){
    int parsed_correctly = 1;
    game_session new_game;
    msg send_msg;
    send_msg.msg_type = CHAT;

    pthread_mutex_lock(&threadMutex);
    if(clients[client_idx] != NULL){
        new_game.host.client_idx = client_idx;
        new_game.host.state = CONNECTED;
        new_game.host.score = 0;
        strcpy(new_game.host.nickname, clients[client_idx]->nickname);

        new_game.game_idx = -1;
        new_game.time = -1;
        new_game.n_winlines = -1;
        new_game.n_baselines = -1;
        for(int i = 0; i < 8; i++){
            new_game.opponents[i] = NULL;
        }

        if(0 <= clients[client_idx]->game_idx){
            pthread_mutex_unlock(&threadMutex);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
            client_msg(send_msg, client_idx);
        }
        else if(argc < 3){
            pthread_mutex_unlock(&threadMutex);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Insufficient number of arguments: must specify the game type and at least one opponent.");
            client_msg(send_msg, client_idx);
        }else{
            pthread_mutex_unlock(&threadMutex);

            int valid_game_mode = 0;
            if((strcmp(argv[1], "0") != 0) && (strcmp(argv[1], "1") != 0) && (strcmp(argv[1], "2") != 0)){
                parsed_correctly = 0;
                strcpy(send_msg.msg, "Invalid game mode selected.");
                client_msg(send_msg, client_idx);
            }else{
                new_game.game_type = strtol(argv[1], NULL, 10);
                valid_game_mode = 1;
            }

            if(valid_game_mode){
                int n_players = 0;

                for(int i = 2; i < argc; i++){
                    int opponent_idx = -1;

                    if(strncmp(argv[i], "time=", 5) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 < new_game.time){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: time has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break;
                        }
                        else if(rhs == NULL){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for time option must be provided.");
                            client_msg(send_msg, client_idx);
                            break;
                        }else{
                            int time = strtol(rhs, NULL, 10);
                            if(time < 1){
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: time must be at least 1 minute.");
                                client_msg(send_msg, client_idx);
                                break;
                            }else{
                                new_game.time = time;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "baselines=", 10) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= new_game.n_baselines){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: baselines has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break;
                        }
                        else if(rhs == NULL){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for baselines option must be provided.");
                            client_msg(send_msg, client_idx);
                            break;
                        }else{
                            int baselines = strtol(rhs, NULL, 10);
                            if(baselines < 0){
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: number of baselines must be a positive integer.");
                                client_msg(send_msg, client_idx);
                                break;
                            }else{
                                new_game.n_baselines = baselines;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "winlines=", 9) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= new_game.n_winlines){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: winlines has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break;
                        }
                        else if(rhs == NULL){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for winlines option must be provided.");
                            client_msg(send_msg, client_idx);
                            break;
                        }else{
                            int winlines = strtol(rhs, NULL, 10);
                            if(winlines < 0){
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: number of winlines must be a positive integer.");
                                client_msg(send_msg, client_idx);
                                break;
                            }else{
                                new_game.n_winlines = winlines;
                            }
                        }
                    }
                    else if(n_players == 7){
                        parsed_correctly = 0;
                        strcpy(send_msg.msg, "Invalid number of opponents: too many specified.");
                        client_msg(send_msg, client_idx);
                        break;
                    }else{
                        pthread_mutex_lock(&threadMutex);
                        for(int j = 0; j < MAX_CLIENTS; j++){
                            if(!clients[j]){
                                continue;
                            }
                            else if(strcmp(argv[i], clients[j]->nickname) == 0){
                                opponent_idx = j;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&threadMutex);

                        if(opponent_idx < 0){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid nickname: the nickname ");
                            strcat(send_msg.msg, argv[i]);
                            strcat(send_msg.msg, " does not belong to any active player.");
                            client_msg(send_msg, client_idx);
                            break;
                        }else{
                            int already_added = 0;
                            for(int k = 0; k < n_players; k++){
                                if(strcmp(argv[i], new_game.opponents[k]->nickname) == 0){
                                    already_added = 1;
                                    break;
                                }
                            }

                            if(already_added){
                                parsed_correctly  = 0;
                                strcpy(send_msg.msg, "Invalid nickname: the nickname ");
                                strcat(send_msg.msg, argv[i]);
                                strcat(send_msg.msg, " has been listed more than once.");
                                client_msg(send_msg, client_idx);
                                break;
                            }
                            else{
                                (new_game.opponents)[n_players] = malloc(sizeof(ingame_client));
                                (new_game.opponents)[n_players]->state = REJECTED;
                                (new_game.opponents)[n_players]->score = 0;
                                (new_game.opponents)[n_players]->client_idx = opponent_idx;
                                strcpy((new_game.opponents)[n_players]->nickname, argv[i]);
                            }
                        }
                    }
                }
            }
        }

        if(parsed_correctly){
            if(new_game.n_baselines < 0){
                new_game.n_baselines = BASELINES_DEFAULT;
            }

            if(new_game.n_winlines < 0){
                new_game.n_winlines = WINLINES_DEFAULT;
            }

            if(new_game.time < 0){
                new_game.time = TIME_DEFAULT;
            }

            // TO--DO: Create game session thread etc...
            // Remember to check nicknameQ and pthread_mutex release
        }
    }
}

void sfunc_quick(int argc, char* argv[], int client_idx){}
void sfunc_chill(int argc, char* argv[], int client_idx){}

void sfunc_go(int argc, char* argv[], int client_idx) {
    msg send_msg;
    send_msg.msg_type = CHAT;

    if(argc < 2){
        strcpy(send_msg.msg, "Please specify the game id.");
        client_msg(send_msg, client_idx);
    }else{
        int game_idx = strtol(argv[1], NULL, 10);
        if(game_idx < 0 || MAX_CLIENTS <= game_idx){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = 0;

            pthread_mutex_lock(gameMutexes + game_idx);
            for(int i = 0; i < 8; i++){
                if((games[game_idx]->opponents)[i]->client_idx == client_idx){
                    registered_in_game = 1;
                    pthread_mutex_lock(&threadMutex);
                    if((clients[client_idx] != NULL) && (clients[client_idx]->game_idx < 0)){
                        clients[client_idx]->game_idx = game_idx;
                        (games[game_idx]->opponents)[i]->state = CONNECTED;
                        pthread_mutex_unlock(&threadMutex);
                    }else{
                        pthread_mutex_unlock(&threadMutex);

                        strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
                        client_msg(send_msg, client_idx);
                    }
                }
            }
            pthread_mutex_unlock(gameMutexes + game_idx);

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
        int game_idx = strtol(argv[1], NULL, 10);
        if(game_idx < 0 || MAX_CLIENTS <= game_idx){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = -1;

            pthread_mutex_lock(gameMutexes + game_idx);
            for(int i = 0; i < 8; i++){
                if((games[game_idx]->opponents)[i]->client_idx == client_idx){
                    registered_in_game = i;
                }
            }

            if(registered_in_game < 0){
                strcpy(send_msg.msg, "Invalid game id: you are not registered to this game session.");
                client_msg(send_msg, client_idx);
            }
            else{
                strcpy(send_msg.msg, "Player ");
                strcat(send_msg.msg, (games[game_idx]->opponents)[registered_in_game]->nickname);
                strcat(send_msg.msg, " has declined to join game session ");
                strcat(send_msg.msg, argv[1]);
                strcat(send_msg.msg, ".");

                for(int i = 0; i < 8; i++){
                    if((games[game_idx]->opponents)[i]->client_idx != client_idx){
                        client_msg(send_msg, (games[game_idx]->opponents)[i]->client_idx);
                    }
                }
            }
            pthread_mutex_unlock(gameMutexes + game_idx);
        }
    }
}

void sfunc_nickname(int argc, char* argv[], int client_idx){}
void sfunc_help(int argc, char* argv[], int client_idx){}

void sfunc_msg(int argc, char* argv[], int client_idx){
    pthread_mutex_lock(&threadMutex);
    if(clients[client_idx] != NULL){
        char nickname[UNAME_LEN] = {0};
        strcpy(nickname, clients[client_idx]->nickname);

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

int handle_chat_msg(char chat_msg[MSG_SIZE], int client_idx){
    if(char_msg[0] == '!'){
        char *token = strtok(char_msg, " ");
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

                msg err_msg;
                err_msg.msg_type = CHAT;
                strcpy(err_msg.msg, "Server was unable to process your request. Please try again.")
                client_msg(err_msg, client_idx);

                return 1;
            }
        }

        if(strcmp(token_list[0], "!exit") == 0){
            return 0;
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
                sfunc_msg(1, (char *[]) {char_msg}, client_idx);
            }
        }
    }else{
        sfunc_msg(1, (char*[]) {char_msg}, client_idx);
    }

    return 1;
}

int handle_score_update_msg(char chat_msg[MSG_SIZE], int client_idx){
    return 1;
}
int handle_finished_game_msg(char chat_msg[MSG_SIZE], int client_idx){
    return 1;
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
