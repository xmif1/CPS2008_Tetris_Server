#include "server.h"

int main(){
    int socket_fd = server_init();

    // Initialise clients and games arrays to NULLs, as well as atomic clients and game session thread mutexes
    for(int i = 0; i < MAX_CLIENTS; i++){
        clients[i] = NULL;
        pthread_mutex_init(&clientMutexes[i], NULL);

        games[i] = NULL;
        pthread_mutex_init(&gameMutexes[i], NULL);
    }

    struct sigaction sint;
    sint.sa_handler = sig_handler;
    sigemptyset(&(sint.sa_mask));
    sigaddset(&(sint.sa_mask), SIGINT);
    sigaction(SIGINT, &sint, NULL);

    struct sigaction sterm;
    sint.sa_handler = sig_handler;
    sigemptyset(&(sterm.sa_mask));
    sigaddset(&(sterm.sa_mask), SIGTERM);
    sigaction(SIGTERM, &sterm, NULL);

    // Main loop listening for client connections, ready to accept them as sufficient resources become available.
    while(1){
        struct sockaddr_in clientaddrIn;
        socklen_t sizeof_clientaddrIn = sizeof(struct sockaddr_in);

        int client_fd = accept(socket_fd, (struct sockaddr*) &clientaddrIn, &sizeof_clientaddrIn);
        if(sig_raised < 1 && client_fd < 0){
            mrerror("Error on attempt to accept client connection");
        }

        add_client(client_fd, clientaddrIn);
    }
}

void sig_handler(){
    if(sig_raised < 1){
        sig_raised++;

        printf("\nServer is shutting down...disconnecting clients...\n");

        for(int i = 0; i < MAX_CLIENTS; i++){
            if(clients[i] != NULL){
                pthread_cancel(service_threads[i]);

                close(clients[i]->client_fd);

                free(clients[i]);
                clients[i] = NULL;
                n_clients--;
            }
        }

        printf("All clients disconnected...goodbye!\n");

	exit(1);
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
        char nickname[UNAME_LEN] = {0}; gen_nickname(nickname);

        int i = 0;
        for(; i < MAX_CLIENTS; i++){
            pthread_mutex_lock(clientMutexes + i);
            if(clients[i] == NULL){
                break;
            }
            else{
                pthread_mutex_unlock(clientMutexes + i);
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
        strcpy(clients[i]->nickname, nickname);

        if(pthread_create(service_threads + i, NULL, service_client, (void*) clients[i]) != 0){
            mrerror("Error while creating thread to service newly connected client");
        }

        n_clients++;

        pthread_mutex_unlock(clientMutexes + i);

        msg joined_msg;
        joined_msg.msg = malloc(32 + UNAME_LEN);
        if(joined_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        joined_msg.msg_type = CHAT;
        strcpy(joined_msg.msg, "Connected...your nickname is ");
        strcat(joined_msg.msg, nickname);
        strcat(joined_msg.msg, ".");
        client_msg(joined_msg, i);
    }else{
        msg err_msg;
        err_msg.msg = malloc(80);
        if(err_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        err_msg.msg_type = CHAT;
        strcpy(err_msg.msg, "Maximum number of clients acheived: unable to connect at the moment.");

        if(send(client_fd, (void*) &err_msg, sizeof(msg), 0) < 0){
            smrerror("Error encountered while communicating with new client");
        }

        close(client_fd);
    }
}

void remove_client(int client_idx){
    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        int game_idx = clients[client_idx]->game_idx;
        pthread_mutex_lock(gameMutexes + game_idx);
        if(game_idx >= 0 && games[game_idx] != NULL){
            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL && (games[game_idx]->players)[i]->client_idx == client_idx){
                    (games[game_idx]->players)[i]->state = DISCONNECTED;
                }
            }
        }
        pthread_mutex_unlock(gameMutexes + game_idx);

        msg send_msg;
        send_msg.msg = malloc(32 + UNAME_LEN);
        if(send_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        send_msg.msg_type = CHAT;
        strcpy(send_msg.msg, "Player ");
        strcat(send_msg.msg, clients[client_idx]->nickname);
        strcat(send_msg.msg, " has disconnected.");

        close(clients[client_idx]->client_fd);

        free(clients[client_idx]);
        clients[client_idx] = NULL;
        n_clients--;

        pthread_mutex_unlock(clientMutexes + client_idx);

        for(int i = 0; i < MAX_CLIENTS; i++){
       	    client_msg(send_msg, i);
        }

    }else{
        pthread_mutex_unlock(clientMutexes + client_idx);
    }
}

/* -------- THREADED FUNCTIONS -------- */

void* service_client(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    int client_fd = ((client*) arg)->client_fd;
    int client_idx = ((client*) arg)->client_idx;

    int break_while_loop = 0;
    int msg_type, tbr, recv_bytes, recv_str_len;
    char header[HEADER_SIZE]; header[HEADER_SIZE - 1] = '\0';

    while((recv_bytes = recv(client_fd, (void*) &header, HEADER_SIZE - 1, 0)) > 0){
        for(tbr = recv_bytes; tbr < HEADER_SIZE - 1; tbr += recv_bytes){
            if((recv_bytes = recv(client_fd, (void*) (&header + tbr), HEADER_SIZE - tbr - 1, 0)) < 0){
                break_while_loop= 1;
                break;
            }
        }

        if(break_while_loop){
            break;
        }

        char* token = strtok(header, "::");
        recv_str_len = strtol(token, NULL, 10);

        token = strtok(NULL, "::");
        msg_type = strtol(token, NULL, 10);

        char* recv_str = malloc(recv_str_len);
        if(recv_str == NULL){
            mrerror("Error while allocating memory");
        }

        for(tbr = 0; tbr < recv_str_len; tbr += recv_bytes){
            if((recv_bytes = recv(client_fd, (void*) recv_str + tbr, recv_str_len - tbr, 0)) < 0){
                break_while_loop = 1;
                break;
            }
        }

        if(break_while_loop){
            break;
        }

        switch(msg_type){
            case CHAT: break_while_loop = handle_chat_msg(recv_str, client_idx); break;
            case SCORE_UPDATE: break_while_loop = handle_score_update_msg(recv_str, client_idx); break;
            case FINISHED_GAME: break_while_loop = handle_finished_game_msg(recv_str, client_idx); break;
            case P2P_READY: break_while_loop = handle_p2p_read_msg(recv_str, client_idx); break;
            default: break_while_loop = 0;
        }

        free(recv_str);

        if(break_while_loop){
            break;
        }
    }

    remove_client(client_idx);
    pthread_exit(NULL);
}

void* service_game_request(void* arg){
    int game_idx = *((int*) arg);

    pthread_mutex_lock(gameMutexes + game_idx);
    if(games[game_idx] != NULL){
        msg request_msg;
        request_msg.msg = malloc(256 + (N_SESSION_PLAYERS + 1)*UNAME_LEN);
        if(request_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        request_msg.msg_type = CHAT;
        strcpy(request_msg.msg, "Player ");
        strcat(request_msg.msg, (games[game_idx]->players)[0]->nickname);
        strcat(request_msg.msg, " has invited you to a game of Super Battle Tetris: ");
        if(games[game_idx]->game_type == RISING_TIDE){
            strcat(request_msg.msg, "Rising Tide!\n\nThe invited players are:");
        }
        else if(games[game_idx]->game_type == FAST_TRACK){
            strcat(request_msg.msg, "Fast Track!\n");

            strcat(request_msg.msg, "The number of baselines is ");
            char n_baselines[4]; sprintf(n_baselines, "%d", games[game_idx]->n_baselines);
            strcat(request_msg.msg, n_baselines);

            strcat(request_msg.msg, " and the number of winning lines is ");
            char n_winlines[4]; sprintf(n_winlines, "%d", games[game_idx]->n_winlines);
            strcat(request_msg.msg, n_winlines);
            strcat(request_msg.msg, ".\n\nThe invited players are:");
        }
        else{
            strcat(request_msg.msg, "Boomer!\n");
            strcat(request_msg.msg, "The match duration is ");
            char time[4]; sprintf(time, "%d", games[game_idx]->time);
            strcat(request_msg.msg, time);
            strcat(request_msg.msg, " minutes.\n\nThe invited players are:");
        }

        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL){
                strcat(request_msg.msg, "\n\t");
                strcat(request_msg.msg, (games[game_idx]->players)[i]->nickname);
            }
        }

        char game_idx_str[(int) floor(log10(MAX_CLIENTS))+2]; sprintf(game_idx_str, "%d", game_idx);
        strcat(request_msg.msg, "\n\nThe game id is: ");
        strcat(request_msg.msg, game_idx_str);

        for(int i = 1; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL){
                client_msg(request_msg, (games[game_idx]->players)[i]->client_idx);
            }
        }
    }
    pthread_mutex_unlock(gameMutexes + game_idx);

    sleep(INVITATION_EXP);

    int n_players = 0;

    pthread_mutex_lock(gameMutexes + game_idx);
    if(games[game_idx] != NULL){
        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if(((games[game_idx]->players)[i] != NULL) && ((games[game_idx]->players)[i]->state == WAITING ||
            (games[game_idx]->players)[i]->state == DISCONNECTED)){
                free((games[game_idx]->players)[i]);
                (games[game_idx]->players)[i] = NULL;
            }
            else if((games[game_idx]->players)[i] != NULL){
                n_players++;
            }
        }

        msg send_msg;
        send_msg.msg = malloc(80);
        if(send_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        send_msg.msg_type = CHAT;
        if(n_players == 0){
            free(games[game_idx]);
            games[game_idx] = NULL;
        }
        else if(n_players == 1){
            int client_idx;
            int i = 0;
            for(; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    client_idx = (games[game_idx]->players)[i]->client_idx;
                }
            }
            strcpy(send_msg.msg, "Insufficient number of players have joined the game session.");
            client_msg(send_msg, client_idx);

            pthread_mutex_lock(clientMutexes + client_idx);
            if(clients[client_idx] != NULL){
                clients[client_idx]->game_idx = -1;
            }
            pthread_mutex_unlock(clientMutexes + client_idx);

            free(games[game_idx]);
            games[game_idx] = NULL;
        }
        else if(n_players > 1){
            games[game_idx]->n_players = n_players;

            char new_game_msg_header[64];

            sprintf(new_game_msg_header, "%d", games[game_idx]->game_type);
            strcat(new_game_msg_header, "::");

            char baselines_str[4];
            sprintf(baselines_str, "%d", games[game_idx]->n_baselines);
            strcat(new_game_msg_header, baselines_str);
            strcat(new_game_msg_header, "::");

            char winlines_str[4];
            sprintf(winlines_str, "%d", games[game_idx]->n_winlines);
            strcat(new_game_msg_header, winlines_str);
            strcat(new_game_msg_header, "::");

            char time_str[4];
            sprintf(time_str, "%d", games[game_idx]->time);
            strcat(new_game_msg_header, time_str);

            char new_game_msg_tail[n_players*UNAME_LEN]; strcpy(new_game_msg_tail, "\0");

            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    strcat(new_game_msg_tail, "::");
                    strcat(new_game_msg_tail, (games[game_idx]->players)[i]->ip);
                }
            }

            int port_block_offset = 0;
            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    port_block_offset++;

                    msg new_game_msg;
                    new_game_msg.msg = malloc(256 + (n_players - 1)*UNAME_LEN);
                    if(new_game_msg.msg == NULL){
                        mrerror("Error encountered while allocating memory");
                    }

                    new_game_msg.msg_type = NEW_GAME;
                    strcpy(new_game_msg.msg, new_game_msg_header);
                    strcat(new_game_msg.msg, "::");

                    char port_block_offset_str[4];
                    sprintf(port_block_offset_str, "%d", port_block_offset);
                    strcat(new_game_msg.msg, port_block_offset_str);

                    strcat(new_game_msg.msg, new_game_msg_tail);

                    client_msg(new_game_msg, (games[game_idx]->players)[i]->client_idx);
                }
            }

            games[game_idx]->n_players_p2p_ready = 0;
            while(games[game_idx]->n_players_p2p_ready < n_players){
                pthread_cond_wait(&(games[game_idx]->p2p_ready), gameMutexes + game_idx);
            }

            msg start_game_msg;
            start_game_msg.msg_type = START_GAME;

            start_game_msg.msg = malloc(1);
            strcpy(start_game_msg.msg, "");

            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    client_msg(start_game_msg, (games[game_idx]->players)[i]->client_idx);
                }
            }
        }
    }
    pthread_mutex_unlock(gameMutexes + game_idx);
    pthread_exit(NULL);
}

/* ------ SERVER FUNCTIONS (sfunc) ------ */

void sfunc_leaderboard(int argc, char* argv[], int client_idx){}

void sfunc_players(int argc, char* argv[], int client_idx){
    msg send_msg;
    send_msg.msg = malloc(16 + MAX_CLIENTS*UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;
    strcpy(send_msg.msg, "Waiting Players:");

    for(int i = 0; i < MAX_CLIENTS; i++){
        pthread_mutex_lock(clientMutexes + i);
        if((clients[i] != NULL) && (clients[i]->game_idx < 0)){
            strcat(send_msg.msg, "\n\t");
            strcat(send_msg.msg, clients[i]->nickname);
        }
        pthread_mutex_unlock(clientMutexes + i);
    }

    client_msg(send_msg, client_idx);
}

void sfunc_playerstats(int argc, char* argv[], int client_idx){}

void sfunc_battle(int argc, char* argv[], int client_idx){
    int parsed_correctly = 1;

    msg send_msg;
    send_msg.msg = malloc(128 + UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;

    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL && clients[client_idx]->game_idx >= 0){
        strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
        client_msg(send_msg, client_idx);
    }
    else if(clients[client_idx] != NULL){
        int game_idx = 0;
        for(; game_idx < MAX_CLIENTS; game_idx++){
            pthread_mutex_lock(gameMutexes + game_idx);
            if(games[game_idx] == NULL){
                break;
            }
            else{
                pthread_mutex_unlock(gameMutexes + game_idx);
            }
        }

        games[game_idx] = malloc(sizeof(game_session));

        (games[game_idx]->players)[0] = malloc(sizeof(ingame_client));
        (games[game_idx]->players)[0]->client_idx = client_idx;
        (games[game_idx]->players)[0]->state = CONNECTED;
        (games[game_idx]->players)[0]->score = 0;
        strcpy((games[game_idx]->players)[0]->nickname, clients[client_idx]->nickname);
        inet_ntop(AF_INET, &(clients[client_idx]->clientaddrIn.sin_addr), (games[game_idx]->players)[0]->ip, INET_ADDRSTRLEN);

        games[game_idx]->game_idx = game_idx;
        games[game_idx]->time = -1;
        games[game_idx]->n_winlines = -1;
        games[game_idx]->n_baselines = -1;
        for(int i = 1; i < N_SESSION_PLAYERS; i++){
            games[game_idx]->players[i] = NULL;
        }

        if(0 <= clients[client_idx]->game_idx){
            pthread_mutex_unlock(clientMutexes + client_idx);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Cannot join another game while another one is in progress.");
            client_msg(send_msg, client_idx);
        }
        else if(argc < 3){
            pthread_mutex_unlock(clientMutexes + client_idx);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Insufficient number of arguments: must specify the game type and at least one opponent.");
            client_msg(send_msg, client_idx);
        }else{
            pthread_mutex_unlock(clientMutexes + client_idx);

            int valid_game_mode = 0;
            if((strcmp(argv[1], "0") != 0) && (strcmp(argv[1], "1") != 0) && (strcmp(argv[1], "2") != 0)){
                parsed_correctly = 0;
                strcpy(send_msg.msg, "Invalid game mode selected.");
                client_msg(send_msg, client_idx);
            }else{
                games[game_idx]->game_type = strtol(argv[1], NULL, 10);
                valid_game_mode = 1;
            }

            if(valid_game_mode){
                int n_players = 1;

                for(int i = 2; i < argc; i++){
                    int opponent_idx = -1;

                    if(strncmp(argv[i], "time=", 5) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 < games[game_idx]->time){
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
                                games[game_idx]->time = time;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "baselines=", 10) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= games[game_idx]->n_baselines){
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
                                games[game_idx]->n_baselines = baselines;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "winlines=", 9) == 0){
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= games[game_idx]->n_winlines){
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
                                games[game_idx]->n_winlines = winlines;
                            }
                        }
                    }
                    else if(n_players > N_SESSION_PLAYERS){
                        parsed_correctly = 0;
                        strcpy(send_msg.msg, "Invalid number of opponents: too many specified.");
                        client_msg(send_msg, client_idx);
                        break;
                    }else{
                        for(int j = 0; j < MAX_CLIENTS; j++){
                            pthread_mutex_lock(clientMutexes + j);
                            if(!clients[j]){
                                pthread_mutex_unlock(clientMutexes + i);
                            }
                            else if(strcmp(argv[i], clients[j]->nickname) == 0){
                                opponent_idx = j;
                                pthread_mutex_unlock(clientMutexes + j);
                                break;
                            }
                            pthread_mutex_unlock(clientMutexes + j);
                        }

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
                                if(strcmp(argv[i], games[game_idx]->players[k]->nickname) == 0){
                                    already_added = 1;
                                    break;
                                }
                            }

                            if(already_added){
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid nickname: the nickname ");
                                strcat(send_msg.msg, argv[i]);
                                strcat(send_msg.msg, " has been listed more than once.");
                                client_msg(send_msg, client_idx);
                                break;
                            }
                            else{
                                (games[game_idx]->players)[n_players] = malloc(sizeof(ingame_client));
                                (games[game_idx]->players)[n_players]->state = WAITING;
                                (games[game_idx]->players)[n_players]->score = 0;
                                (games[game_idx]->players)[n_players]->client_idx = opponent_idx;
                                strcpy((games[game_idx]->players)[n_players]->nickname, argv[i]);

                                n_players++;
                            }
                        }
                    }
                }
            }
        }

        if(parsed_correctly){
            if(games[game_idx]->n_baselines < 0){
                games[game_idx]->n_baselines = BASELINES_DEFAULT;
            }

            if(games[game_idx]->n_winlines < 0){
                games[game_idx]->n_winlines = WINLINES_DEFAULT;
            }

            if(games[game_idx]->time < 0){
                games[game_idx]->time = TIME_DEFAULT;
            }

            (games[game_idx]->top_three)[0] = (games[game_idx]->top_three)[1] = (games[game_idx]->top_three)[2] = -1;

            pthread_mutex_lock(clientMutexes + client_idx);
            clients[client_idx]->game_idx = game_idx;
            pthread_mutex_unlock(clientMutexes + client_idx);

            if(pthread_create(game_threads + game_idx, NULL, service_game_request, (void*) &(clients[client_idx]->game_idx)) != 0){
                mrerror("Error while creating thread to service newly created game session");
            }

            pthread_mutex_unlock(gameMutexes + game_idx);

            strcpy(send_msg.msg, "Game invite sent to the other players...waiting for their response....");
            client_msg(send_msg, client_idx);
        }
        else{
            free(games[game_idx]);
            games[game_idx] = NULL;

            pthread_mutex_unlock(gameMutexes + game_idx);
        }
    }
}

void sfunc_quick(int argc, char* argv[], int client_idx){
    msg send_msg;
    send_msg.msg = malloc(128 + UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }
    send_msg.msg_type = CHAT;

    if(argc == 1){
        strcpy(send_msg.msg, "Invalid option: Please specify the number of opponents you wish to join the session.");
    }else{
        int n_req_opponents = strtol(argv[1], NULL, 10);

        int n_available_opponents = 0;
        for(int i = 0; i < MAX_CLIENTS; i++){
            pthread_mutex_lock(clientMutexes + i);
            if(i != client_idx && clients[i] != NULL && clients[i]->game_idx < 0){
                n_available_opponents++;
            }
        }

        if(clients[client_idx] != NULL && clients[client_idx]->game_idx >= 0){
            strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
        }
        else if(N_SESSION_PLAYERS <= n_req_opponents){
            strcpy(send_msg.msg, "Invalid option: Too many opponents specified.");
        }
        else if(n_req_opponents < 1){
            strcpy(send_msg.msg, "Invalid option: Too few opponents specified.");
        }
        else if(n_available_opponents < n_req_opponents){
            strcpy(send_msg.msg, "Invalid option: Not enough players are available to join the game session.");
        }
        else{
            int game_idx = 0;
            for(; game_idx < MAX_CLIENTS; game_idx++){
                pthread_mutex_lock(gameMutexes + game_idx);
                if(games[game_idx] == NULL){
                    break;
                }
                else{
                    pthread_mutex_unlock(gameMutexes + game_idx);
                }
            }

            games[game_idx] = malloc(sizeof(game_session));

            (games[game_idx]->players)[0] = malloc(sizeof(ingame_client));
            (games[game_idx]->players)[0]->client_idx = client_idx;
            (games[game_idx]->players)[0]->state = CONNECTED;
            (games[game_idx]->players)[0]->score = 0;
            strcpy((games[game_idx]->players)[0]->nickname, clients[client_idx]->nickname);
            inet_ntop(AF_INET, &(clients[client_idx]->clientaddrIn.sin_addr), (games[game_idx]->players)[0]->ip, INET_ADDRSTRLEN);

            games[game_idx]->game_idx = game_idx;
            games[game_idx]->time = (rand() % TIME_DEFAULT) + 1;
            games[game_idx]->n_winlines = (rand() % WINLINES_DEFAULT) + 1;
            games[game_idx]->n_baselines = (rand() % BASELINES_DEFAULT) + 1;

            for(int i = 1; i <= n_req_opponents; i++){
                int opponent_idx, added_player;
                opponent_idx = added_player = 0;

                while(!added_player){
                    opponent_idx = rand() % MAX_CLIENTS;

                    if(clients[opponent_idx] == NULL || clients[opponent_idx]->game_idx >= 0){
                        continue;
                    }else{
                        added_player = 1;
                        for(int j = 0; j < i; j++){
                            if((games[game_idx]->players)[j]->client_idx == opponent_idx){
                                added_player = 0;
                                break;
                            }
                        }
                    }
                }

                (games[game_idx]->players)[i] = malloc(sizeof(ingame_client));
                (games[game_idx]->players)[i]->state = WAITING;
                (games[game_idx]->players)[i]->score = 0;
                (games[game_idx]->players)[i]->client_idx = opponent_idx;
                strcpy((games[game_idx]->players)[i]->nickname, clients[opponent_idx]->nickname);
            }

            for(int i = n_req_opponents + 1; i < N_SESSION_PLAYERS; i++){
                games[game_idx]->players[i] = NULL;
            }

            (games[game_idx]->top_three)[0] = (games[game_idx]->top_three)[1] = (games[game_idx]->top_three)[2] = -1;

            clients[client_idx]->game_idx = game_idx;

            if(pthread_create(game_threads + game_idx, NULL, service_game_request, (void*) &(clients[client_idx]->game_idx)) != 0){
                mrerror("Error while creating thread to service newly created game session");
            }

            pthread_mutex_unlock(gameMutexes + game_idx);

            strcpy(send_msg.msg, "Game invite sent to the other players...waiting for their response....");
        }

        for(int i = 0; i < MAX_CLIENTS; i++){
            pthread_mutex_unlock(clientMutexes + i);
        }
    }

    client_msg(send_msg, client_idx);
}

void sfunc_chill(int argc, char* argv[], int client_idx){}

void sfunc_go(int argc, char* argv[], int client_idx) {
    msg send_msg;
    send_msg.msg = malloc(64);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;

    if(argc < 2){
        strcpy(send_msg.msg, "Please specify the game id.");
        client_msg(send_msg, client_idx);
    }else{
        int game_idx = strtol(argv[1], NULL, 10);

        pthread_mutex_lock(gameMutexes + game_idx);

        if(game_idx < 0 || MAX_CLIENTS <= game_idx || games[game_idx] == NULL){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = 0;

            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if(((games[game_idx]->players)[i] != NULL) && ((games[game_idx]->players)[i]->client_idx == client_idx)){
                    registered_in_game = 1;
                    pthread_mutex_lock(clientMutexes + client_idx);
                    if((games[game_idx]->players)[i]->state != WAITING){
                        pthread_mutex_unlock(clientMutexes + client_idx);

                        strcpy(send_msg.msg, "You have already joined this game session.");
                        client_msg(send_msg, client_idx);
                    }
                    else if((clients[client_idx] != NULL) && (clients[client_idx]->game_idx < 0)){
                        clients[client_idx]->game_idx = game_idx;
                        (games[game_idx]->players)[i]->state = CONNECTED;
                        inet_ntop(AF_INET, &(clients[client_idx]->clientaddrIn.sin_addr), (games[game_idx]->players)[i]->ip, INET_ADDRSTRLEN);
                        pthread_mutex_unlock(clientMutexes + client_idx);

                        strcpy(send_msg.msg, "You have successfully joined the game session.");
                        client_msg(send_msg, client_idx);
                    }else{
                        pthread_mutex_unlock(clientMutexes + client_idx);

                        strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
                        client_msg(send_msg, client_idx);
                    }
                }
            }

            if(!registered_in_game){
                strcpy(send_msg.msg, "Invalid game id: you are not registered to this game session.");
                client_msg(send_msg, client_idx);
            }
        }

        pthread_mutex_unlock(gameMutexes + game_idx);
    }
}

void sfunc_ignore(int argc, char* argv[], int client_idx){
    msg send_msg;
    send_msg.msg = malloc(64 + UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;

    if(argc < 2){
        strcpy(send_msg.msg, "Please specify the game id.");
        client_msg(send_msg, client_idx);
    }else{
        int game_idx = strtol(argv[1], NULL, 10);

        pthread_mutex_lock(gameMutexes + game_idx);

        if(game_idx < 0 || MAX_CLIENTS <= game_idx || games[game_idx] == NULL){
            strcpy(send_msg.msg, "Invalid game id: does not exist.");
            client_msg(send_msg, client_idx);
        }else{
            int registered_in_game = -1;

            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if(((games[game_idx]->players)[i] != NULL) && ((games[game_idx]->players)[i]->client_idx == client_idx)){
                    registered_in_game = i;
                }
            }

            if(registered_in_game < 0){
                strcpy(send_msg.msg, "Invalid game id: you are not registered to this game session.");
                client_msg(send_msg, client_idx);
            }
            else if((games[game_idx]->players)[registered_in_game]->state != WAITING){
                strcpy(send_msg.msg, "Cannot decline a game request after accepting to join.");
                client_msg(send_msg, client_idx);
            }else{
                strcpy(send_msg.msg, "Player ");
                strcat(send_msg.msg, (games[game_idx]->players)[registered_in_game]->nickname);
                strcat(send_msg.msg, " has declined to join game session ");
                strcat(send_msg.msg, argv[1]);
                strcat(send_msg.msg, ".");

                for(int i = 0; i < N_SESSION_PLAYERS; i++){
                    if(((games[game_idx]->players)[i] != NULL) && ((games[game_idx]->players)[i]->client_idx != client_idx)
                    && ((games[game_idx]->players)[i]->state != DISCONNECTED)){
                        client_msg(send_msg, (games[game_idx]->players)[i]->client_idx);
                    }
                }

                msg send_to_client;
                send_to_client.msg = malloc(64);
                if(send_msg.msg == NULL){
                    mrerror("Error encountered while allocating memory");
                }

                send_to_client.msg_type = CHAT;
                strcpy(send_to_client.msg, "You have successfully declined to join the game session.");
                client_msg(send_to_client, client_idx);
            }
        }

        pthread_mutex_unlock(gameMutexes + game_idx);
    }
}

void sfunc_nickname(int argc, char* argv[], int client_idx){}
void sfunc_help(int argc, char* argv[], int client_idx){}

void sfunc_msg(int argc, char* argv[], int client_idx){
    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        int msg_len = strlen(clients[client_idx]->nickname) + strlen(argv[0]) + 3;

        if(MSG_LEN_DIGITS < ((int) floor(log10(msg_len)) + 1)){
            msg err_msg;
            err_msg.msg = malloc(64);
            if(err_msg.msg == NULL){
                mrerror("Error encountered while allocating memory");
            }

            err_msg.msg_type = CHAT;
            strcpy(err_msg.msg, "The text input you have provided is too long to be processed.");

            pthread_mutex_unlock(clientMutexes + client_idx);
            client_msg(err_msg, client_idx);

        }else{
            int str_to_send_len = HEADER_SIZE + msg_len - 1;
            char header[HEADER_SIZE];
            char* str_to_send = malloc(str_to_send_len);

            if(str_to_send == NULL){
                mrerror("Failed to allocate memory for message send");
            }

            int i = 0;
            for(; i < MSG_LEN_DIGITS - ((int) floor(log10(msg_len)) + 1); i++){
                header[i] = '0';
            }

            sprintf(header + i, "%d", msg_len);
            strcat(header, "::");
            sprintf(header + MSG_LEN_DIGITS + 2, "%d", CHAT);
            strcat(header, "::");

            strcpy(str_to_send, header);
            strcat(str_to_send, clients[client_idx]->nickname);
            strcat(str_to_send, ">\t");
            strcat(str_to_send, argv[0]);
            str_to_send[str_to_send_len-1] = '\0'; // ensure null terminated

            for(int j = 0; j < MAX_CLIENTS; j++){
                if(j != client_idx){
                    pthread_mutex_lock(clientMutexes + j);
                }

                int fail_flag = 0;
                if(clients[j] != NULL){
                    int tbs; // tbs = total bytes sent
                    int sent_bytes;

                    for(tbs = 0; tbs < str_to_send_len; tbs += sent_bytes){
                        if((sent_bytes = send(clients[j]->client_fd, (void*) str_to_send + tbs, str_to_send_len - tbs, 0)) < 0){
                            pthread_cancel(service_threads[j]);
                            pthread_mutex_unlock(clientMutexes + j);
                            remove_client(j);
                            fail_flag = 1;
                            break;
                        }
                    }
                }

                if(j != client_idx && fail_flag != 1){
                    pthread_mutex_unlock(clientMutexes + j);
                }
            }

            free(str_to_send);

            pthread_mutex_unlock(clientMutexes + client_idx);
        }
    }else{
        pthread_mutex_unlock(clientMutexes + client_idx);
    }
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
        pthread_mutex_lock(clientMutexes + i);
        if(clients[i] == NULL){
            pthread_mutex_unlock(clientMutexes + i);
        }
        else if(strcmp(nickname, clients[i]->nickname) == 0){
            pthread_mutex_unlock(clientMutexes + i);
            return 1;
        }else{
            pthread_mutex_unlock(clientMutexes + i);
        }
    }

    return 0;
}

void client_msg(msg send_msg, int client_idx){
    int msg_len = strlen(send_msg.msg) + 1;
    int str_to_send_len = HEADER_SIZE + msg_len - 1;
    char header[HEADER_SIZE];
    char* str_to_send = malloc(str_to_send_len);

    if(str_to_send == NULL){
        mrerror("Failed to allocate memory for message send");
    }

    int i = 0;
    for(; i < MSG_LEN_DIGITS - ((int) floor(log10(msg_len)) + 1); i++){
        header[i] = '0';
    }

    sprintf(header + i, "%d", msg_len);
    strcat(header, "::");
    sprintf(header + MSG_LEN_DIGITS + 2, "%d", send_msg.msg_type);
    strcat(header, "::");

    strcpy(str_to_send, header);
    strcat(str_to_send, send_msg.msg);
    str_to_send[str_to_send_len-1] = '\0'; // ensure null terminated

    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        int tbs; // tbs = total bytes sent
        int sent_bytes;
        int fail_flag = 0;

        for(tbs = 0; tbs < str_to_send_len; tbs += sent_bytes){
            if((sent_bytes = send(clients[client_idx]->client_fd, (void*) str_to_send + tbs, str_to_send_len - tbs, 0)) < 0){
                pthread_cancel(service_threads[client_idx]);
                pthread_mutex_unlock(clientMutexes + client_idx);
                remove_client(client_idx);
                fail_flag = 1;
                break;
            }
        }

        if(!fail_flag){
            pthread_mutex_unlock(clientMutexes + client_idx);
        }
    }else{
        pthread_mutex_unlock(clientMutexes + client_idx);
    }

    free(str_to_send);
}

int handle_chat_msg(char* chat_msg, int client_idx){
    if(chat_msg[0] == '!'){
        char *token = strtok(chat_msg, " ");
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
                err_msg.msg = malloc(64);
                if(err_msg.msg == NULL){
                    mrerror("Error encountered while allocating memory");
                }

                err_msg.msg_type = CHAT;
                strcpy(err_msg.msg, "Server was unable to process your request. Please try again.");
                client_msg(err_msg, client_idx);

                return 0;
            }
        }

        if(strcmp(token_list[0], "!exit") == 0){
            return 1;
        }else{
            int msg_flag = 1;

            for(int i = 0; i < N_SFUNCS; i++){
                if(strcmp(token_list[0], sfunc_dict[i]) == 0){
                    (*sfunc[i])(n_tokens, token_list, client_idx);
                    msg_flag = 0;
                    break;
                }
            }

            if(msg_flag){
                sfunc_msg(1, (char *[]) {chat_msg}, client_idx);
            }
        }

        free(token_list);
    }else{
        sfunc_msg(1, (char*[]) {chat_msg}, client_idx);
    }

    return 0;
}

int handle_score_update_msg(char* chat_msg, int client_idx){
    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        pthread_mutex_lock(gameMutexes + clients[client_idx]->game_idx);
        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[clients[client_idx]->game_idx]->players)[i] != NULL
                && (games[clients[client_idx]->game_idx]->players)[i]->client_idx == client_idx
                && (games[clients[client_idx]->game_idx]->players)[i]->state == CONNECTED){

                (games[clients[client_idx]->game_idx]->players)[i]->score = strtol(chat_msg, NULL, 10);
            }
        }
        pthread_mutex_unlock(gameMutexes + clients[client_idx]->game_idx);
    }
    pthread_mutex_unlock(clientMutexes + client_idx);

    return 0;
}

int handle_finished_game_msg(char* chat_msg, int client_idx){
    int game_idx = -1;
    int player_idx = 0;
    int game_finished = 0;

    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        game_idx = clients[client_idx]->game_idx;
    }
    pthread_mutex_unlock(clientMutexes + client_idx);

    if(game_idx >= 0){
        pthread_mutex_lock(gameMutexes + game_idx);

        for(; player_idx < N_SESSION_PLAYERS; player_idx++){
            if((games[game_idx]->players)[player_idx] != NULL && (games[game_idx]->players)[player_idx]->client_idx == client_idx){
                (games[game_idx]->players)[player_idx]->state = FINISHED;
            }
        }

        if(games[game_idx]->game_type == RISING_TIDE){
            // shift down rankings; last player standing gets first place, etc
            (games[game_idx]->top_three)[2] = (games[game_idx]->top_three)[1];
            (games[game_idx]->top_three)[1] = (games[game_idx]->top_three)[0];
            (games[game_idx]->top_three)[0] = player_idx;
        }
        else if(games[game_idx]->game_type == FAST_TRACK){
            for(int i = 0; i < 3; i++){
                if((games[game_idx]->top_three)[i] < 0){
                    (games[game_idx]->top_three)[i] = player_idx;
                    break;
                }
            }
        }else{
            int player_score = (games[game_idx]->players)[player_idx]->score;

            for(int i = 0; i < 3; i++){
                if((games[game_idx]->top_three)[i] < 0){
                    (games[game_idx]->top_three)[i] = player_idx;
                    break;
                }
                else if((games[game_idx]->players)[(games[game_idx]->top_three)[i]]->score < player_score){
                    for(int j = i; j < 2; j++){
                        (games[game_idx]->top_three)[j+1] = (games[game_idx]->top_three)[j];
                    }

                    (games[game_idx]->top_three)[i] = player_idx;
                    break;
                }
            }
        }

        int n_completed_players  = 0;

        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL){
                if((games[game_idx]->players)[i]->state == FINISHED || (games[game_idx]->players)[i]->state == DISCONNECTED){
                    n_completed_players++;
                }
            }
        }

        if(n_completed_players == games[game_idx]->n_players){
            game_finished = 1;

            msg finished_msg;
            finished_msg.msg_type = CHAT;
            finished_msg.msg = malloc(256 + 3*UNAME_LEN);
            if(finished_msg.msg == NULL){
                mrerror("Error encountered while allocating memory");
            }
            strcpy(finished_msg.msg, "All players have completed the game! The top players are, in highest ranking order:");

            for(int i = 0; i < 3; i++){
                if((games[game_idx]->top_three)[i] >= 0 && i < games[game_idx]->n_players){
                    strcat(finished_msg.msg, "\n\t");
                    strcat(finished_msg.msg, (games[game_idx]->players)[(games[game_idx]->top_three)[i]]->nickname);
                    strcat(finished_msg.msg, " with a score of ");
		    printf("finished game called by %d\n", i); //debug

		    char score[7];
                    sprintf(score, "%d", (games[game_idx]->players)[(games[game_idx]->top_three)[i]]->score);
		    strcat(finished_msg.msg, score);
		    printf("finished game called by %d\n", i); //debug

                    strcat(finished_msg.msg, " points.");
                }
            }

	    printf("%s\n", finished_msg.msg); //debug
            int winner_idx = (games[game_idx]->players)[(games[game_idx]->top_three)[0]]->client_idx;

            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL && (games[game_idx]->players)[i]->state != DISCONNECTED){
                    int curr_client_idx = (games[game_idx]->players)[i]->client_idx;

                    pthread_mutex_lock(clientMutexes + curr_client_idx);
                    clients[curr_client_idx]->game_idx = -1;

                    if(curr_client_idx != winner_idx && clients[curr_client_idx] != NULL){
                        clients[curr_client_idx]->n_losses++;
                    }
                    else if(curr_client_idx == winner_idx && clients[curr_client_idx] != NULL){
                        clients[curr_client_idx]->n_wins++;
                    }
                    pthread_mutex_unlock(clientMutexes + curr_client_idx);

                    client_msg(finished_msg, (games[game_idx]->players)[i]->client_idx);
                }
            }
        }

        pthread_mutex_unlock(gameMutexes + game_idx);
    }

    if(game_finished){
        free(games[game_idx]);
        games[game_idx] = NULL;
    }

    return 0;
}

int handle_p2p_read_msg(char* chat_msg, int client_idx){
    pthread_mutex_lock(clientMutexes + client_idx);
    if(clients[client_idx] != NULL){
        pthread_mutex_lock(gameMutexes + clients[client_idx]->game_idx);
        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[clients[client_idx]->game_idx]->players)[i] != NULL &&
                (games[clients[client_idx]->game_idx]->players)[i]->client_idx == client_idx){

		(games[clients[client_idx]->game_idx]->n_players_p2p_ready)++;
                pthread_cond_broadcast(&(games[clients[client_idx]->game_idx]->p2p_ready));
            }
        }
        pthread_mutex_unlock(gameMutexes + clients[client_idx]->game_idx);
    }
    pthread_mutex_unlock(clientMutexes + client_idx);

    return 0;
}

/* ----------- ERROR HANDLING ----------- */

/* Mr. Error: A simple function to handle errors (mostly a wrapper to perror), and terminate.*/
void mrerror(char* err_msg){
    red();
    perror(err_msg);
    reset();
    raise(SIGTERM);
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
