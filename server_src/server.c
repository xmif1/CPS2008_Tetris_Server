#include "server.h"

/* Main function, which is responsible to initialising the server instance. First, a call to server_init() is made,
 * which initialises a socket for accepting client connections. The corresponding file descriptor is returned and held.
 *
 * The the clients and game session arrays of structs along with their associated mutexes are initialised, for atomic
 * thread--safe access to the respective array elements.
 *
 * After which, signal handlers for SIGINT and SIGTERM are installed. Termination of the server can be done by
 * interrupting the program (since the main loop is indefinite). The signal handlers ensure graceful termination and are
 * described in detail later on.
 *
 * The main loop is then executed, which simply accepts new client connections and calls add_client as necessary.
 */
int main(){
    // initialise socket for accepting client connections
    int socket_fd = server_init();

    // Initialise clients and games arrays to NULLs, as well as atomic clients and game session thread mutexes
    for(int i = 0; i < MAX_CLIENTS; i++){
        clients[i] = NULL;
        pthread_mutex_init(&clientMutexes[i], NULL);

        games[i] = NULL;
        pthread_mutex_init(&gameMutexes[i], NULL);
    }

    // install SIGINT handler
    struct sigaction sint;
    sint.sa_handler = sig_handler;
    sigemptyset(&(sint.sa_mask));
    sigaddset(&(sint.sa_mask), SIGINT);
    sigaction(SIGINT, &sint, NULL);

    // install SIGTERM handler
    struct sigaction sterm;
    sint.sa_handler = sig_handler;
    sigemptyset(&(sterm.sa_mask));
    sigaddset(&(sterm.sa_mask), SIGTERM);
    sigaction(SIGTERM, &sterm, NULL);

    // Main loop listening for client connections, ready to accept them as sufficient resources become available.
    while(1){
        // initialise struct for maintaining networking details of client
        struct sockaddr_in clientaddrIn;
        socklen_t sizeof_clientaddrIn = sizeof(struct sockaddr_in);

        // call accept (in blocking mode) on socket_fd
        int client_fd = accept(socket_fd, (struct sockaddr*) &clientaddrIn, &sizeof_clientaddrIn);

        // if signal has NOT been raised and in return client_fd is invalid on attempt to accept client connection
        if(sig_raised < 1 && client_fd < 0){
            // display error and terminate erroneously
            mrerror("Error on attempt to accept client connection");
        }

        // otherwise if client_fd is valid, call add_client
        add_client(client_fd, clientaddrIn);
    }
}

// A simple signal handler to in particular disconnect clients gracefully.
void sig_handler(){
    // if signal has not been raised apriori (to prevent multiple threads trying to handle the signal)
    if(sig_raised < 1){
        sig_raised++;

        printf("\nServer is shutting down...disconnecting clients...\n");

        // gracefully disconnect clients connected
        for(int i = 0; i < MAX_CLIENTS; i++){
            if(clients[i] != NULL){
                // first cancel the thread that handles communication with the client
                pthread_cancel(service_threads[i]);

                close(clients[i]->client_fd); // then close the connection by closing the file descriptor
                // as a result, on the next attempt by the client to send or recv from the server, the call would return
                // erroneously, and the client library has mechanisms to handle this and disconnect from their end

                // we then free the associated memory
                free(clients[i]);
                clients[i] = NULL;
                n_clients--;
            }
        }

        printf("All clients disconnected...goodbye!\n");

	    exit(1); // finally, we can exit
    }
}

/* Convenience function for initialising a socket on which we can accept client connections. We shall see this pattern of
 * socket initialisation a number of times throughout the project. Based on the parameters defined in the header, we
 * create a new socket, attempt to bind it, if successful we attempt to listen on the socket, and in turn if that is
 * successful we return the file descriptor of the socket.
 */
int server_init(){
    int socket_fd;

    printf("-----------------------------------------\n"
           "Initialising server...\n");

    // Create socket
    socket_fd = socket(SDOMAIN, TYPE, 0);
    if(socket_fd < 0){ // check if valid
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

/* Convenience function for adding a client to the server, which in particular ensures that the maximum number of clients
 * currently connected to the server has not been exceeded (in which case we disconnect the client). If the client can be
 * connected, we allocate a random nickname to the client, find the first available free index in the clients array and
 * allocate a new client struct at that index, along with an associated mutex.
 */
void add_client(int client_fd, struct sockaddr_in clientaddrIn){
    // first check if maximum number of connected clients has not been exceeded
    if(n_clients < MAX_CLIENTS - 1){ // if further resource constraints exist, add them here
        // generate random nickname using gen_nickname convenience function
        char nickname[UNAME_LEN] = {0}; gen_nickname(nickname);

        // in a thread--safe manner, find the next available free index in the clients array
        int i = 0;
        for(; i < MAX_CLIENTS; i++){
            pthread_mutex_lock(clientMutexes + i);
            if(clients[i] == NULL){
                break; // i corresponds to a free slot; note that in this case the mutex lock is not released
                // (i.e. slot cannot be populated by some other thread)
            }
            else{
                pthread_mutex_unlock(clientMutexes + i);
            }
        }

        // initialise the client struct at the i^th index
        clients[i] = malloc(sizeof(client)); // create new client struct
        clients[i]->client_fd = client_fd; // set to fd obtained during connection
        clients[i]->client_idx = i;
        clients[i]->clientaddrIn = clientaddrIn; // maintain in particular the IPv4 address of the client, for P2P use later
        clients[i]->game_idx = -1; // game_idx = -1 indicates the client is not joined to a game_session
        clients[i]->high_score = 0;
        clients[i]->n_wins = 0;
        clients[i]->n_losses = 0;
        strcpy(clients[i]->nickname, nickname);

        // create new thread to handle communication with the client
        if(pthread_create(service_threads + i, NULL, service_client, (void*) clients[i]) != 0){
            mrerror("Error while creating thread to service newly connected client");
        }

        // increment by 1 (used to keep track if the max. no. of connected clients has been achieved)
        n_clients++;

        // release mutex lock corresponding to the client
        pthread_mutex_unlock(clientMutexes + i);

        // initialise new msg instance and allocate enough memory for data part
        msg joined_msg;
        joined_msg.msg = malloc(32 + UNAME_LEN);
        if(joined_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        // send message to the client, informing them that they have successfully join the server, and specify their nickname
        joined_msg.msg_type = CHAT;
        strcpy(joined_msg.msg, "Connected...your nickname is ");
        strcat(joined_msg.msg, nickname);
        strcat(joined_msg.msg, ".");
        client_msg(joined_msg, i); // client_msg is a convience function outlined later on to handle sending msgs to clients
    }else{ // if maximum number of clients has been exceeded, send an appropriate message to the client and disconnect
        msg err_msg;
        err_msg.msg = malloc(80);
        if(err_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        // inform the client that the maximum number of clients has been achieved 
        err_msg.msg_type = CHAT;
        strcpy(err_msg.msg, "Maximum number of clients achieved: unable to connect at the moment.");

        // attempt to send to the client
        if(send(client_fd, (void*) &err_msg, sizeof(msg), 0) < 0){
            smrerror("Error encountered while communicating with new client");
        }

        // and then disconnect by closing the file descriptor
        close(client_fd);
    }
}

/* Convenience function for removing a client in a thread--safe manner. Note that it is important, before any call to
 * remove_client, to ensure that the corresponding mutex is released (as otherwise the calling thread would hang
 * indefinitely, which was a bug experienced during development).
 *
 * Removal of a client involves:
 * (i)   If the client is in a game session, the game struct is changed to reflect client disconnection (which is required
 *       when tallying up the final scores). Note that the game session continues.
 * (ii)  We then close the connection to the client and free any associated memory.
 * (iii) We send a message to all remaining clients informing them of the disconnection.
 */
void remove_client(int client_idx){
    pthread_mutex_lock(clientMutexes + client_idx); // obtain lock corresponding to client

    if(clients[client_idx] != NULL){ // if valid client struct at client_idx
        int game_idx = clients[client_idx]->game_idx; // hold reference to game_idx

        if(game_idx >= 0){ // if client is in some game session (recall game_idx == -1 iff not in game session)
            pthread_mutex_lock(gameMutexes + game_idx); // obtain mutex for corresponding game session
            if(games[game_idx] != NULL){ // if game struct at game_idx is valid
                for(int i = 0; i < N_SESSION_PLAYERS; i++){ // find corresponding player entry in struct and mark as DISCONNECTED
                    if((games[game_idx]->players)[i] != NULL && (games[game_idx]->players)[i]->client_idx == client_idx){
                        (games[game_idx]->players)[i]->state = DISCONNECTED;
                    }
                }
            }
            pthread_mutex_unlock(gameMutexes + game_idx); // release mutex lock for corresponding game session

            if(games[game_idx] != NULL){
                /* If on disconnection, no more players remain playing the game, then handle game finish. This prevents
                 * 'zombie' game instances which never finish due to abrupt disconnection of the final player in the game.
                 * This was a bug which was being experienced and is mitigated by this change.
                 *
                 * Setting remove_client_flag to client_idx ensures no message is sent to the client instance, resulting
                 * in never--ending recursive calls to remove_client by client_msg (which is...bad...very bad).
                 */
                gameFinishedQ(game_idx, client_idx);
            }
        }

        // initialise new msg instance and allocate enough memory for data part
        msg send_msg;
        send_msg.msg = malloc(32 + UNAME_LEN);
        if(send_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        // prepare message to send to remaining clients after disconnection, informing them client X has disconnected
        send_msg.msg_type = CHAT;
        strcpy(send_msg.msg, "Player ");
        strcat(send_msg.msg, clients[client_idx]->nickname);
        strcat(send_msg.msg, " has disconnected.");

        close(clients[client_idx]->client_fd); // close connection with client

        // free memory are necessary, and decrement n_clients (to allows other clients to connect instead)
        free(clients[client_idx]);
        clients[client_idx] = NULL;
        n_clients--;

        pthread_mutex_unlock(clientMutexes + client_idx); // release lock corresponding to now free client entry

        // make a call to client_msg for each possible client (if no clients[i] == NULL, client_msg handles this accordingly)
        for(int i = 0; i < MAX_CLIENTS; i++){
       	    client_msg(send_msg, i);
        }

    }else{
        pthread_mutex_unlock(clientMutexes + client_idx);
    }
}

/* -------- THREADED FUNCTIONS -------- */

/* This threaded function handles communication with the client, by first fetching and decoding a recieved message and
 * then handling the message accordingly, depending on the contents of the data part.
 *
 * Decoding of messages is outlined in further detail in the project report. Note that since we are using the TCP protocol,
 * data is streamed (and hence received) by the server in the same order as that sent by the client.
 */
void* service_client(void* arg){
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // extract data passed to the thread
    int client_fd = ((client*) arg)->client_fd;
    int client_idx = ((client*) arg)->client_idx;

    int break_while_loop = 0; // flag to break main loop for recieving messages

    // variables used to maintain the msg type, total bytes read (tbr), number of bytes received from last call to recv,
    // and the expected length of the next data part
    int msg_type, tbr, recv_bytes, recv_str_len;

    // initialise char array to keep header of message
    char header[HEADER_SIZE]; header[HEADER_SIZE - 1] = '\0';

    // loop indefinitely until break_while_loop is 1 OR recv return erroneously OR signal is raised
    // initial call to recv on each iteration attempts to fetch the header of the message first
    while((recv_bytes = recv(client_fd, (void*) &header, HEADER_SIZE - 1, 0)) > 0){
        // ensure that the header is recieved entirely (keep on looping until tbr == HEADER_SIZE - 1)
        for(tbr = recv_bytes; tbr < HEADER_SIZE - 1; tbr += recv_bytes){
            if((recv_bytes = recv(client_fd, (void*) (&header + tbr), HEADER_SIZE - tbr - 1, 0)) < 0){
                break_while_loop = 1; // break if erroneous i.e. we have not managed to fetch a complete message header
                break;
            }
        }

        if(break_while_loop){
            break;
        }

	    printf("client %d: %s", client_idx, header); // print header contents to terminal, handy for testing purposes

	    // decode the header by extracting the expected length of the data part and the message type
	    char str_len_part[5]; strncpy(str_len_part, header, 4); str_len_part[4] = '\0';
        recv_str_len = strtol(str_len_part, NULL, 10);
        msg_type = header[6] - '0';

        // initialise array of decoded data part length, in which data part will be stored
        char* recv_str = malloc(recv_str_len);
        if(recv_str == NULL){
            mrerror("Error while allocating memory");
        }

        // reset tbr to 0, loop until the successive calls to recv yield the entire data part
        for(tbr = 0; tbr < recv_str_len; tbr += recv_bytes){
            if((recv_bytes = recv(client_fd, (void*) recv_str + tbr, recv_str_len - tbr, 0)) < 0){
                break_while_loop = 1; // break if erroneous i.e. we have not managed to fetch the complete data part
                break;
            }
        }

        if(break_while_loop){
            break;
        }

	    printf("%s\n", recv_str); // print data part contents to terminal, handy for testing purposes

	    // depending on the decoded message type from the header, call the respective handler function
	    // each of which, and any further functions called by them, must be thread--safe
        switch(msg_type){
            case CHAT: break_while_loop = handle_chat_msg(recv_str, client_idx); break;
            case SCORE_UPDATE: break_while_loop = handle_score_update_msg(recv_str, client_idx); break;
            case FINISHED_GAME: break_while_loop = handle_finished_game_msg(recv_str, client_idx); break;
            case P2P_READY: break_while_loop = handle_p2p_read_msg(recv_str, client_idx); break;
            default: break_while_loop = 0;
        }

        free(recv_str); // free memory as necessary

        if(break_while_loop){
            break;
        }
    }

    // once main loop terminates, then we cannot recieve further message from the client and hence we disconnect
    remove_client(client_idx);

    // we can then exit the service_client thread
    pthread_exit(NULL);
}

/* This threaded function is responsible for the creation of a new game session and ensuring the clients in a game session
 * are ready to accept peer-to-peer connections. It is NOT responsible for game option validation (this must be done by
 * the calling function) however it does send the game options in the invite sent to all invited clients, in the case of
 * multiplayer mode.
 *
 * A flow--chart of the interaction between the service_game_request thread, the service_client thread, as well as the
 * client library, is given in the project report. The procedure for setting up a game session, as handled by this thread,
 * if as follows:
 *
 * (i)   Send an invite to all clients invited to the game session, with the game details
 * (ii)  Sleep for n seconds (default n = 30), during which players may accept or decline (sending a !go or !ignore chat msg)
 * (iii) For all those clients that accepted the invite, we send a NEW_GAME message with the game details and IPv4 address
 *       for each client in the session, as well as a port offset that allocates a unique port number to each client. This
 *       allows for P2P setup.
 * (iv)  Wait until all clients send a P2P_READY message (indicating that they are ready to accept P2P connections)
 * (v)   Send a START_GAME message to all clients in the session, and terminate the thread.
 */
void* service_game_request(void* arg){
    int game_idx = *((int*) arg); // obtain game session index from the passed arguments

    pthread_mutex_lock(gameMutexes + game_idx); // obtain the mutex lock for the game session struct at game_idx
    int game_type = games[game_idx]->game_type; // hold reference to the game type

    // if game struct at index is valid and game is a multiplayer mode
    // in case of a single player mode (CHILL), there is no need to send any invitation
    if(games[game_idx] != NULL && game_type != CHILL){
        // initialise new msg instance and allocate enough memory for data part
        msg request_msg;
        request_msg.msg = malloc(256 + (N_SESSION_PLAYERS + 1)*UNAME_LEN);
        if(request_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        // Depending on game type, include in the invite the game_type, chosen options, and the list of invited players
        request_msg.msg_type = CHAT;
        strcpy(request_msg.msg, "Player ");
        strcat(request_msg.msg, (games[game_idx]->players)[0]->nickname); // include nickname of player which sent the invite
        strcat(request_msg.msg, " has invited you to a game of Super Battle Tetris: ");

        // Include game options depending on the game_type specified
        if(game_type == RISING_TIDE){
            // In case of RISING_TIDE, there are no options to set and hence none to be specified
            strcat(request_msg.msg, "Rising Tide!\n\nThe invited players are:");
        }
        else if(game_type == FAST_TRACK){
            strcat(request_msg.msg, "Fast Track!\n");

            // In this game mode, the number of baselines and winning lines can be changed and hence must be displayed
            // If not changed when creating the invite, the default values are used (and hence displayed in the invite)
            strcat(request_msg.msg, "The number of baselines is ");
            char n_baselines[4]; sprintf(n_baselines, "%d", games[game_idx]->n_baselines);
            strcat(request_msg.msg, n_baselines);

            strcat(request_msg.msg, " and the number of winning lines is ");
            char n_winlines[4]; sprintf(n_winlines, "%d", games[game_idx]->n_winlines);
            strcat(request_msg.msg, n_winlines);
            strcat(request_msg.msg, ".\n\nThe invited players are:");
        }
        else{
            // In this game mode, the number minutes the game lasts can be changed and hence must be displayed
            // If not changed when creating the invite, the default value is used (and hence displayed in the invite)

            strcat(request_msg.msg, "Boomer!\n");
            strcat(request_msg.msg, "The match duration is ");
            char time[4]; sprintf(time, "%d", games[game_idx]->time);
            strcat(request_msg.msg, time);
            strcat(request_msg.msg, " minutes.\n\nThe invited players are:");
        }

        // For every invited player, include their nickname in the invitation message
        // Note that the game struct maintains a *copy* of the nickname, and hence no need to obtain the client's mutex lock
        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL){
                strcat(request_msg.msg, "\n\t");
                strcat(request_msg.msg, (games[game_idx]->players)[i]->nickname);
            }
        }

        // allocate sufficient memory for a textual representation of the game index
        char game_idx_str[(int) floor(log10(MAX_CLIENTS))+2]; sprintf(game_idx_str, "%d", game_idx);
        // include the game index in the message (must be used with !go or !ignore commands, since a client may recieve
        // multiple game invites in a short period of time
        strcat(request_msg.msg, "\n\nThe game id is: ");
        strcat(request_msg.msg, game_idx_str);

        // Finally, send the invite to each invited player (i.e. client instance) using the client_msg convenience function
        for(int i = 1; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL){
                client_msg(request_msg, (games[game_idx]->players)[i]->client_idx);
            }
        }
    }
    pthread_mutex_unlock(gameMutexes + game_idx); // release the mutex lock for the game session struct at game_idx

    // if game struct at index is valid and game is a multiplayer mode
    // in case of a single player mode (CHILL), there is no need to wait for players to accept or reject the game invite
    if(games[game_idx] != NULL && game_type != CHILL){
        sleep(INVITATION_EXP); // INVITATION_EXP specifies the number of seconds after which accept or rejects are ignored
    }

    int n_players = 0; // used to store the number of players that have accepted the game invite

    pthread_mutex_lock(gameMutexes + game_idx); // obtain the mutex lock for the game session struct at game_idx
    if(games[game_idx] != NULL){
        // determine the number of players that have not DISCONNECTED and have accepted the game invite (i.e. not WAITING)
        for(int i = 0; i < N_SESSION_PLAYERS; i++){

            if(((games[game_idx]->players)[i] != NULL) && ((games[game_idx]->players)[i]->state == WAITING ||
            (games[game_idx]->players)[i]->state == DISCONNECTED)){
                // if DISCONNECTED or still WAITING (i.e. not accepted the game invite), free memory and deinitialise
                free((games[game_idx]->players)[i]);
                (games[game_idx]->players)[i] = NULL;
            }
            else if((games[game_idx]->players)[i] != NULL){
                n_players++; // else player is connected and accepted the invite i.e. increment n_players by 1
            }
        }

        // initialise new msg instance and allocate enough memory for data part
        msg send_msg;
        send_msg.msg = malloc(80);
        if(send_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        send_msg.msg_type = CHAT;
        if(n_players == 0){
            // if no players in the end have joined the game session, simply free memory and exit thread
            free(games[game_idx]);
            games[game_idx] = NULL;
        }
        // else if the game mode is multiplayer but there is a single player only, inform the player that game is cancelled and exit thread
        else if(n_players == 1 && game_type != CHILL){
            int client_idx;
            int i = 0;

            // find the *only* client which is still in the game session
            for(; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    client_idx = (games[game_idx]->players)[i]->client_idx;
                }
            }

            // send a message (using client_msg) to the client informing them that the game is cancelled
            strcpy(send_msg.msg, "Insufficient number of players have joined the game session.");
            client_msg(send_msg, client_idx);

            // de-register client from the game session by setting their game_idx to -1 in the respective struct
            // we do this in a thread--safe manner by first obtaining the client's mutex lock
            pthread_mutex_lock(clientMutexes + client_idx);
            if(clients[client_idx] != NULL){
                clients[client_idx]->game_idx = -1;
            }
            pthread_mutex_unlock(clientMutexes + client_idx);

            // lastly, we free memory and exit thread
            free(games[game_idx]);
            games[game_idx] = NULL;
        }
        // otherwise we communicate the necessary information for the client(s) to start a new game session
        // (to setup the front end, P2P network, etc)
        else if(n_players > 1 || game_type == CHILL){
            // update the game session struct with the number of players that are connected and have accepted the invite
            games[game_idx]->n_players = n_players;

            /* This section deals with the construction of the data part of the NEW_GAME message, which is discussed in
             * detail in the project report. In essence, the message takes the following form:
             *
             * <game_type>::<n_baselines>::<n_winlines>::<time>::<seed>::<port_block_offset>::<client_1_ipv4>::
             * <client_2_ipv4>:: . . . ::<client_n_ipv4>
             *
             * where n is the number of clients in the game session, the seed is a randomly generated integer by the server
             * to be used a seed to an LCG implemented in the front-end (used for all clients to generate the same sequence
             * of tetrominoes), and the port_block_offset i is an integer between 1 <= i <= n, which assigns the unique port
             * 8080 + i to the i^th client.
             *
             * Note that if a particular game type does not allow for a parameter to be changed, we still include that
             * parameter in the message -- we simply include the default value. This allows for a standardised message
             * across the different game types, which can be decoded in the same manner by the client library.
             *
             * The port_block_offset is unique to each client in the session, i.e. each client recieves a unique message.
             * However, the game options (new_game_msg_header) + the client IPv4 list (new_game_msg_tail) is the same in
             * all messages. Hence we first construct these as a string once, then simply concatenate with the unique
             * port_block_offset: new_game_msg_header + port_block_offset_str + new_game_msg_tail.
             */

            char new_game_msg_header[64]; // we begin by first constructing the game options part of the messages

            sprintf(new_game_msg_header, "%d", games[game_idx]->game_type); // convert from int and copy the game type
            strcat(new_game_msg_header, "::");

            char baselines_str[4];
            sprintf(baselines_str, "%d", games[game_idx]->n_baselines); // convert from int the # of baselines
            strcat(new_game_msg_header, baselines_str); // concat # of baselines to message
            strcat(new_game_msg_header, "::"); // concat the separation token

            char winlines_str[4];
            sprintf(winlines_str, "%d", games[game_idx]->n_winlines); // convert from int the # of winlines
            strcat(new_game_msg_header, winlines_str); // concat # of winlines to message
            strcat(new_game_msg_header, "::"); // concat the separation token

            char time_str[4];
            sprintf(time_str, "%d", games[game_idx]->time); // convert from int the # of minutes of gameplay
            strcat(new_game_msg_header, time_str); // concat # of minutes of gameplay to message
            strcat(new_game_msg_header, "::"); // concat the separation token

            char seed_str[4];
            sprintf(seed_str, "%d", games[game_idx]->seed);  // convert from int the random seed
            strcat(new_game_msg_header, seed_str); // concat the random seed to message

            // we now construct the tail of message with the list of IPv4 addresses of the clients in the game session
            // observe that we ensure null termination
            char new_game_msg_tail[n_players*UNAME_LEN]; strcpy(new_game_msg_tail, "\0");

            // simply loop through all references of clients in the game session struct and concat the IPv4 address
            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    strcat(new_game_msg_tail, "::"); // concat the separation token
                    strcat(new_game_msg_tail, (games[game_idx]->players)[i]->ip); // concat IPv4 address
                }
            }

            // lastly, we determine the port_block_offset for each client, generate the unique NEW_GAME message, and send it
            int port_block_offset = 0;
            for(int i = 0; i < N_SESSION_PLAYERS; i++){
                if((games[game_idx]->players)[i] != NULL){
                    port_block_offset++; // increment by 1 to allocate the next port

                    // initialise new msg instance and allocate enough memory for data part
                    msg new_game_msg;
                    new_game_msg.msg = malloc(256 + (n_players - 1)*UNAME_LEN);
                    if(new_game_msg.msg == NULL){
                        mrerror("Error encountered while allocating memory");
                    }

                    new_game_msg.msg_type = NEW_GAME;
                    strcpy(new_game_msg.msg, new_game_msg_header); // copy the header with game options first
                    strcat(new_game_msg.msg, "::"); // concat the separation token

                    char port_block_offset_str[4];
                    sprintf(port_block_offset_str, "%d", port_block_offset); // convert from int the port_block_offset of the client
                    strcat(new_game_msg.msg, port_block_offset_str); // concat port_block_offset to the message

                    strcat(new_game_msg.msg, new_game_msg_tail); // lastly, concat the tail with the IPv4 addresses

                    // send to the client using the client_msg convenience function
                    client_msg(new_game_msg, (games[game_idx]->players)[i]->client_idx);
                }
            }

            /* In the case of a multi--player game session, we must wait for all the clients to send a P2P_READY message,
             * i.e. we wait until n_players_p2p_ready == n_players. The variable n_players_p2p_ready must be incremented
             * in a thread--safe manner by each service_client thread corresponding to a client which sent a P2P_READY
             * message. This is discussed in detail in the project report.
             *
             * In short however, we make use of a conditional variable (p2p_ready) of type pthread_cond_t, along with a
             * sequence of calls to pthread_cond_wait from the service_game_request thread and pthread_cond_broadcast
             * from the service_client threads modifying n_players_p2p_ready held in the game session struct in question.
             */
            if(game_type != CHILL){
                games[game_idx]->n_players_p2p_ready = 0;
                while(games[game_idx]->n_players_p2p_ready < n_players){
                    pthread_cond_wait(&(games[game_idx]->p2p_ready), gameMutexes + game_idx);
                }
            }

            // initialise new msg instance and allocate enough memory for data part
            msg start_game_msg;
            start_game_msg.msg_type = START_GAME;

            start_game_msg.msg = malloc(1);
            strcpy(start_game_msg.msg, "");

            // lastly, we send a START_GAME message to all clients in the game session
            // in the context of a multiplayer game, this is only done after all clients have sent a P2P_READY message
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

/* This section outlines a number of handler functions corresponding to different tagged messages and commands sent to
 * the server by a client. Note that these execute on the calling thread, i.e. multiple simultaneous calls can be made
 * to the same function from different threads, accessing the same data etc. Hence each of these functions MUST be
 * thread--safe.
 */

void sfunc_leaderboard(int argc, char* argv[], int client_idx){}

// Simple function that sends a message to the calling client with the list of nicknames for players available to join a game
void sfunc_players(int argc, char* argv[], int client_idx){
    // initialise new msg instance and allocate enough memory for data part
    msg send_msg;
    send_msg.msg = malloc(16 + MAX_CLIENTS*UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;
    strcpy(send_msg.msg, "Waiting Players:");

    for(int i = 0; i < MAX_CLIENTS; i++){
        // in a thread--safe manner, if a client is not in a game (game_idx == -1), obtain the nickname and concat
        pthread_mutex_lock(clientMutexes + i);
        if((clients[i] != NULL) && (clients[i]->game_idx < 0)){ // check if not in game
            strcat(send_msg.msg, "\n\t");
            strcat(send_msg.msg, clients[i]->nickname);
        }
        pthread_mutex_unlock(clientMutexes + i);
    }

    // send message to client using client_msg
    client_msg(send_msg, client_idx);
}

void sfunc_playerstats(int argc, char* argv[], int client_idx){}

/* Responsible for handling the !battle command received from a client, which is reponsible for:
 * (i)   Verifying that the passed parameters are all valid.
 * (ii)  Verifying that the invited players are, at the time of invitation, not in a game already.
 * (iii) If (i) and (ii) are successful, initialise a new service_game_request thread.
 */
void sfunc_battle(int argc, char* argv[], int client_idx){
    int parsed_correctly = 1; // flag that maintains whether the command and options have been specified correctly

    // initialise new msg instance and allocate enough memory for data part
    msg send_msg;
    send_msg.msg = malloc(128 + UNAME_LEN);
    if(send_msg.msg == NULL){
        mrerror("Error encountered while allocating memory");
    }

    send_msg.msg_type = CHAT;

    pthread_mutex_lock(clientMutexes + client_idx); // obtain mutex lock for client that initiated the invite

    // if client that initiated the invite is already in a game session, then they cannot join another game session and
    // in particular cannot initiate a new one either
    if(clients[client_idx] != NULL && clients[client_idx]->game_idx >= 0){
        strcpy(send_msg.msg, "Cannot join another game while one is in progress.");
        client_msg(send_msg, client_idx);
    }
    else if(clients[client_idx] != NULL){
        int game_idx = 0;

        // we begin by searching for the next available free slot in the games array
        for(; game_idx < MAX_CLIENTS; game_idx++){
            pthread_mutex_lock(gameMutexes + game_idx); // obtain mutex lock
            if(games[game_idx] == NULL){ // check if free; if true, then break -- in which case game_idx will be set to this slot
                break; // note that when we break, we still have hold of the mutex lock
            }
            else{
                pthread_mutex_unlock(gameMutexes + game_idx); // release mutex lock if not free
            }
        }

        games[game_idx] = malloc(sizeof(game_session)); // allocate memory for a new game_session struct

        /* initialise game_session struct; we begin by initialising the struct representing the client which initiated the
         * session we store certain meta--data, eg. the score is initially 0, we keep a copy of the nickname, as well as
         * a textual representation of the IPv4 address.
         */
        (games[game_idx]->players)[0] = malloc(sizeof(ingame_client));
        (games[game_idx]->players)[0]->client_idx = client_idx;
        (games[game_idx]->players)[0]->state = CONNECTED;
        (games[game_idx]->players)[0]->score = 0;
        strcpy((games[game_idx]->players)[0]->nickname, clients[client_idx]->nickname);
        // textual representation of IPv4 address
        inet_ntop(AF_INET, &(clients[client_idx]->clientaddrIn.sin_addr), (games[game_idx]->players)[0]->ip, INET_ADDRSTRLEN);

        // continue initialising game struct...
        games[game_idx]->game_idx = game_idx;
        // initially these three options are set to -1, so that if they have been set already during parsing we can detect this and report it
        games[game_idx]->time = -1;
        games[game_idx]->n_winlines = -1;
        games[game_idx]->n_baselines = -1;
        for(int i = 1; i < N_SESSION_PLAYERS; i++){
            games[game_idx]->players[i] = NULL;
        }

        if(0 <= clients[client_idx]->game_idx){ // technically unreachable since we check this prior, but we include it for completeness' sake
            pthread_mutex_unlock(clientMutexes + client_idx);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Cannot join another game while another one is in progress.");
            client_msg(send_msg, client_idx);
        }
        else if(argc < 3){ // in the case that an insufficient number of arguments has been provided...
            pthread_mutex_unlock(clientMutexes + client_idx);

            parsed_correctly = 0;
            strcpy(send_msg.msg, "Insufficient number of arguments: must specify the game type and at least one opponent.");
            client_msg(send_msg, client_idx);
        }else{ // otherwise, we being checking each of the arguments...
            pthread_mutex_unlock(clientMutexes + client_idx);

            int valid_game_mode = 0; // flag to check if game mode is valid (RISING_TIDE, BOOMER, etc...)
            if((strcmp(argv[1], "0") != 0) && (strcmp(argv[1], "1") != 0) && (strcmp(argv[1], "2") != 0)){
                // if not, send an appropriate error message
                parsed_correctly = 0;
                strcpy(send_msg.msg, "Invalid game mode selected.");
                client_msg(send_msg, client_idx);
            }else{
                // otherwise, convert to an integer and maintain in game_session struct
                games[game_idx]->game_type = strtol(argv[1], NULL, 10);
                valid_game_mode = 1;
            }

            if(valid_game_mode){ // in the case that the game mode specified is valid, continue parsing
                int n_players = 1; // number of players in the game session (the invitee + the invited, hence why >= 1)

                for(int i = 2; i < argc; i++){ // iterate through the remaining arguments
                    int opponent_idx = -1;

                    if(strncmp(argv[i], "time=", 5) == 0){ // if argument is of the form "time="
                        // extract right hand side value
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 < games[game_idx]->time){ // if option has already been set, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: time has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }
                        else if(rhs == NULL){ // if right hand side is not specified, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for time option must be provided.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }else{
                            int time = strtol(rhs, NULL, 10); // convert rhs from string to int
                            if(time < 1){ // check that time is at least one minute; if not, send an appropriate error message
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: time must be at least 1 minute.");
                                client_msg(send_msg, client_idx);
                                break; // stop parsing
                            }else{ // otherwise if all is valid, maintain in game_session struct
                                games[game_idx]->time = time;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "baselines=", 10) == 0){ // if argument is of the form "baselines="
                        // extract right hand side value
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= games[game_idx]->n_baselines){ // if option has already been set, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: baselines has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }
                        else if(rhs == NULL){ // if right hand side is not specified, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for baselines option must be provided.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }else{
                            int baselines = strtol(rhs, NULL, 10); // convert rhs from string to int
                            if(baselines < 1 || baselines > 18){ // check if rhs value is within bounds; if not, send an appropriate error message
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: number of baselines must be between 1 and 18.");
                                client_msg(send_msg, client_idx);
                                break; // stop parsing
                            }else{ // otherwise if all is valid, maintain in game_session struct
                                games[game_idx]->n_baselines = baselines;
                            }
                        }
                    }
                    else if(strncmp(argv[i], "winlines=", 9) == 0){ // if argument is of the form "winlines="
                        // extract right hand side value
                        char* rhs = strtok(argv[i], "=");
                        rhs = strtok(NULL, "=");

                        if(0 <= games[game_idx]->n_winlines){ // if option has already been set, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: winlines has been defined more than once. Please specify options once.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }
                        else if(rhs == NULL){ // if right hand side is not specified, send an appropriate error message
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid option: right-hand-side for winlines option must be provided.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }else{
                            int winlines = strtol(rhs, NULL, 10);  // convert rhs from string to int
                            if(winlines < 1){ // the number of winning lines must be at least 1; if not, send an appropriate error message
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid option: number of winlines must be at least 1.");
                                client_msg(send_msg, client_idx);
                                break; // stop parsing
                            }else{ // otherwise if all is valid, maintain in game_session struct
                                games[game_idx]->n_winlines = winlines;
                            }
                        }
                    }
                    // otherwise, the rest of the arguments are treated as nicknames to players

                    // if current argument is potentially a player nickname, but the number of valid nicknames specified
                    // exceeds the number of maximum players in a game session, send an appropriate error message
                    else if(n_players > N_SESSION_PLAYERS){
                        parsed_correctly = 0;
                        strcpy(send_msg.msg, "Invalid number of opponents: too many specified.");
                        client_msg(send_msg, client_idx);
                        break;
                    }else{
                        // in a thread--safe manner, search the array of client instances, and check if there is one with
                        // a nickname matching the argument value
                        for(int j = 0; j < MAX_CLIENTS; j++){
                            pthread_mutex_lock(clientMutexes + j);
                            if(!clients[j]){
                                pthread_mutex_unlock(clientMutexes + i);
                            }
                            else if(strcmp(argv[i], clients[j]->nickname) == 0){
                                // in the case a match is found, opponent_idx becomes a non-negative value (the client_idx of the opponent)
                                opponent_idx = j;
                                pthread_mutex_unlock(clientMutexes + j);
                                break;
                            }
                            pthread_mutex_unlock(clientMutexes + j);
                        }

                        // if no match is found, send an appropriate error message, specifying the invalid nickname given
                        if(opponent_idx < 0){
                            parsed_correctly = 0;
                            strcpy(send_msg.msg, "Invalid nickname: the nickname ");
                            strcat(send_msg.msg, argv[i]);
                            strcat(send_msg.msg, " does not belong to any active player.");
                            client_msg(send_msg, client_idx);
                            break; // stop parsing
                        }else{
                            // otherwise, before registering the client at opponent_idx in the game, we ensure that they
                            // have not been already been added, i.e. no duplicate argument of the same nickname

                            int already_added = 0; // flag which maintains whether opponent has already been added

                            // loop across all registered player instances in the game session and check for uniqueness
                            for(int k = 0; k < n_players; k++){
                                if(strcmp(argv[i], games[game_idx]->players[k]->nickname) == 0){
                                    already_added = 1;
                                    break;
                                }
                            }

                            if(already_added){ // if already added i.e. repeated nickname given
                                // send an appropriate error message, specifying the invalid nickname given
                                parsed_correctly = 0;
                                strcpy(send_msg.msg, "Invalid nickname: the nickname ");
                                strcat(send_msg.msg, argv[i]);
                                strcat(send_msg.msg, " has been listed more than once.");
                                client_msg(send_msg, client_idx);
                                break; // stop parsing
                            }
                            else{
                                // otherwise register player instance in the game_session struct
                                (games[game_idx]->players)[n_players] = malloc(sizeof(ingame_client));
                                (games[game_idx]->players)[n_players]->state = WAITING;
                                (games[game_idx]->players)[n_players]->score = 0;
                                (games[game_idx]->players)[n_players]->client_idx = opponent_idx;
                                strcpy((games[game_idx]->players)[n_players]->nickname, argv[i]);

                                n_players++; // and increment the number of players invited to the game session
                            }
                        }
                    }
                }
            }
        }

        // if at no point has parsed_correctly been set to 0
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

            games[game_idx]->seed = rand() % 1000;

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
        else{ // otherwise if parsed_correctly = 0 i.e a parsing error has occured, free memory as necessary
            free(games[game_idx]);
            games[game_idx] = NULL;

            pthread_mutex_unlock(gameMutexes + game_idx); // and release the mutex lock reserved for the game_session struct
        }
    }
}

void sfunc_quick(int argc, char* argv[], int client_idx){
    // initialise new msg instance and allocate enough memory for data part
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

            games[game_idx]->game_type = rand() % 4;
            games[game_idx]->game_idx = game_idx;
            games[game_idx]->time = (rand() % TIME_DEFAULT) + 1;
            games[game_idx]->n_winlines = (rand() % WINLINES_DEFAULT) + 1;
            games[game_idx]->n_baselines = (rand() % BASELINES_DEFAULT) + 1;
            games[game_idx]->seed = rand() % 1000;

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

void sfunc_chill(int argc, char* argv[], int client_idx){
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

    games[game_idx]->game_type = CHILL;
    games[game_idx]->game_idx = game_idx;
    games[game_idx]->seed = rand() % 1000;

    for(int i = 1; i < N_SESSION_PLAYERS; i++){
        games[game_idx]->players[i] = NULL;
    }

    (games[game_idx]->top_three)[0] = (games[game_idx]->top_three)[1] = (games[game_idx]->top_three)[2] = -1;

    clients[client_idx]->game_idx = game_idx;

    if(pthread_create(game_threads + game_idx, NULL, service_game_request, (void*) &(clients[client_idx]->game_idx)) != 0){
        mrerror("Error while creating thread to service newly created game session");
    }

    pthread_mutex_unlock(gameMutexes + game_idx);
}

void sfunc_go(int argc, char* argv[], int client_idx){
    // initialise new msg instance and allocate enough memory for data part
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

                // initialise new msg instance and allocate enough memory for data part
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

// Generates a random and unique nickname for a newly connected client.
void gen_nickname(char nickname[UNAME_LEN]){
    // define dictionary from which we can select words to generate a nickname
    // nicknames will be of the form keyword1 + keyword2 + random number
    char* keywords1[6] = {"Big", "Little", "Cool", "Lame", "Happy", "Sad"};
    char* keywords2[5] = {"Mac", "Muppet", "Hobbit", "Wizard", "Elf"};
    int not_unique = 1; // flag for checking if (not) unique

    while(not_unique){ // keep on looping until unique nickname is found
        // generate random number such that total number of combinations with keywords is >= MAX_CLIENTS
        // (i.e. the algorithm can always find a unique nickname for a client)
        int i = rand() % 6, j = rand() % 5, k = (rand() % MAX_CLIENTS) + 1;
        char str_k[(int) floor(log10(k))+2];
        sprintf(str_k, "%d", k);

        // concat to generate nickname
        strcpy(nickname, keywords1[i]);
        strcat(nickname, keywords2[j]);
        strcat(nickname, str_k);

        not_unique = nickname_uniqueQ(nickname); // convenience function to checks if a nickname is already in use
    }
}

// Returns 1 if the nickname is not unique, 0 otherwise.
int nickname_uniqueQ(char nickname[UNAME_LEN]){
    // loop throughout the clients array
    for(int i = 0; i < MAX_CLIENTS; i++){
        // obtain mutex lock
        pthread_mutex_lock(clientMutexes + i);
        if(clients[i] == NULL){
            // if not initialised, release mutex lock immediately
            pthread_mutex_unlock(clientMutexes + i);
        }
        else if(strcmp(nickname, clients[i]->nickname) == 0){
            // if matched, return 1 i.e. nickname already in use, and release mutex lock
            pthread_mutex_unlock(clientMutexes + i);
            return 1;
        }else{
            // else release mutex lock and continue looping until return 1 or i == MAX_CLIENTS (in which case no match found)
            pthread_mutex_unlock(clientMutexes + i);
        }
    }

    return 0; // reachable only if no match found i.e. nickname not in use and hence unique
}

/* Utility function used to send a message to a client, taking care of encoding the message (as described in detail in
 * the project report), ensuring that the entire message is sent, and carrying out suitable error checks and handling.
 */
void client_msg(msg send_msg, int client_idx){
    // initialise necessary variables for encoding the message
    int msg_len = strlen(send_msg.msg) + 1;
    int str_to_send_len = HEADER_SIZE + msg_len - 1; // enough space for the header + data part + null character
    char header[HEADER_SIZE];
    char* str_to_send = malloc(str_to_send_len);

    // if allocation of memory for holding the message to send failed, report an error and exit
    if(str_to_send == NULL){
        mrerror("Failed to allocate memory for message send");
    }

    // the header must always be of fixed size, with the data part length having MSG_LEN_DIGITS; if the required number
    // of digits is less than MSG_LEN_DIGITS, we prepend the required number of 0s to the header
    int i = 0;
    for(; i < MSG_LEN_DIGITS - ((int) floor(log10(msg_len)) + 1); i++){
        header[i] = '0';
    }

    sprintf(header + i, "%d", msg_len); // concat the data part length
    strcat(header, "::"); // concat the separation token
    sprintf(header + MSG_LEN_DIGITS + 2, "%d", send_msg.msg_type); // concat the message type
    strcat(header, "::"); // concat the separation token

    strcpy(str_to_send, header); // copy the header to the string holding the final message string to be sent
    strcat(str_to_send, send_msg.msg); // append the data part to this string
    str_to_send[str_to_send_len-1] = '\0'; // ensure null terminated

    pthread_mutex_lock(clientMutexes + client_idx); // obtain mutex lock for client to which we are sending the message
    if(clients[client_idx] != NULL){ // if there is a valid client struct at client_idx
        int tbs; // tbs = total bytes sent
        int sent_bytes;
        int fail_flag = 0;

        // make successive calls to send() until the entire message is sent or an error occurs
        for(tbs = 0; tbs < str_to_send_len; tbs += sent_bytes){
            if((sent_bytes = send(clients[client_idx]->client_fd, (void*) str_to_send + tbs, str_to_send_len - tbs, 0)) < 0){
                /* In the case an error occurs during a call to send() with a client, we must handle this appropriate to
                 * ensure graceful disconnection and prevent any threads from hanging. Hence we,
                 * (i)   First make a call to cancel the corresponding service_client thread.
                 * (ii)  Then release the corresponding mutex lock.
                 * (iii) And finally call remove_client to gracefully disconnect the client, free associated memory, etc.
                 */

                pthread_cancel(service_threads[client_idx]);
                pthread_mutex_unlock(clientMutexes + client_idx);
                remove_client(client_idx);

                // set fail flag to prevent calling unlock on the same mutex in succession, which can lead to undefined behaviour
                fail_flag = 1;
                break;
            }
        }

        if(!fail_flag){ // prevent calling unlock on the same mutex twice in succession leading to undefined behaviour
            pthread_mutex_unlock(clientMutexes + client_idx);
        }
    }else{
        pthread_mutex_unlock(clientMutexes + client_idx);
    }

    free(str_to_send); // free memory as necessary
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

                // initialise new msg instance and allocate enough memory for data part
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
                break;
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
        }else if(games[game_idx]->game_type == BOOMER){
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
        pthread_mutex_unlock(gameMutexes + game_idx);

        gameFinishedQ(game_idx, -1);
    }

    return 0;
}

void gameFinishedQ(int game_idx, int remove_client_flag){
    int game_finished = 0;
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

        // initialise new msg instance and allocate enough memory for data part
        msg finished_msg;
        finished_msg.msg_type = CHAT;
        finished_msg.msg = malloc(256 + 3*UNAME_LEN);
        if(finished_msg.msg == NULL){
            mrerror("Error encountered while allocating memory");
        }

        int winner_idx;

        pthread_mutex_lock(gameMutexes + game_idx);

        if(games[game_idx]->game_type != CHILL){
            strcpy(finished_msg.msg, "All players have completed the game! The top players are, in highest ranking order:");

            for(int i = 0; i < 3; i++){
                if((games[game_idx]->top_three)[i] >= 0 && i < games[game_idx]->n_players){
                    strcat(finished_msg.msg, "\n\t");
                    strcat(finished_msg.msg, (games[game_idx]->players)[(games[game_idx]->top_three)[i]]->nickname);
                    strcat(finished_msg.msg, " with a score of ");

                    char score[7];
                    sprintf(score, "%d", (games[game_idx]->players)[(games[game_idx]->top_three)[i]]->score);
                    strcat(finished_msg.msg, score);

                    strcat(finished_msg.msg, " points.");
                }
            }

            winner_idx = (games[game_idx]->players)[(games[game_idx]->top_three)[0]]->client_idx;
        }
        else{
            strcpy(finished_msg.msg, "Game finished! Your score was ");

            char score[7];
            sprintf(score, "%d", (games[game_idx]->players)[0]->score);
            strcat(finished_msg.msg, score);

            strcat(finished_msg.msg, " points.");

            winner_idx = (games[game_idx]->players)[0]->client_idx;
        }

        for(int i = 0; i < N_SESSION_PLAYERS; i++){
            if((games[game_idx]->players)[i] != NULL && (games[game_idx]->players)[i]->state != DISCONNECTED){
                int curr_client_idx = (games[game_idx]->players)[i]->client_idx;

                if(remove_client_flag != curr_client_idx){
                    pthread_mutex_lock(clientMutexes + curr_client_idx);
                    clients[curr_client_idx]->game_idx = -1;

                    if(curr_client_idx != winner_idx && clients[curr_client_idx] != NULL){
                        clients[curr_client_idx]->n_losses++;
                    }
                    else if(curr_client_idx == winner_idx && clients[curr_client_idx] != NULL){
                        clients[curr_client_idx]->n_wins++;
                    }
                    pthread_mutex_unlock(clientMutexes + curr_client_idx);

                    client_msg(finished_msg, curr_client_idx);
                }
            }
        }
    }

    pthread_mutex_unlock(gameMutexes + game_idx);

    if(game_finished){
        free(games[game_idx]);
        games[game_idx] = NULL;
    }
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
