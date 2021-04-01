#include "server.h"

int main(){
    int socket_fd = server_init();
    int client_fd;
    struct sockaddr_in clientaddrIn;
    pthread_t service_threads[MAX_CLIENTS];

    // Main loop listening for client connections, ready to accept them as sufficient resources become available.
    while(1){
        socklen_t sizeof_clientaddrIn = sizeof(clientaddrIn);
        if((client_fd = accept(socket_fd,(struct sockaddr*) &clientaddrIn, &sizeof_clientaddrIn)) < 0){
            mrerror("Error on attempt to accept client connection");
        }

        add_client(client_fd, clientaddrIn, service_threads);
    }

    // How to gracefully close connections when 'server shuts down'?
}

int server_init(){
    int socket_fd;

    printf("-----------------------------------------\n"
           "Initialising server...\n");

    // Create socket
    if((socket_fd = socket(SDOMAIN, TYPE, 0)) < 0){
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

void add_client(int client_fd, struct sockaddr_in clientaddrIn, pthread_t* service_threads){
    if(n_clients < MAX_CLIENTS - 1){ // if further resource constraints exist, add them here
        pthread_mutex_lock(&clients_mutex); pthread_mutex_lock(&n_clients_mutex);

        clients[n_clients] = &(client){.client_fd = client_fd, .clientaddrIn = clientaddrIn, .nickname=gen_nickname()};

        if(pthread_create(service_threads + n_clients, NULL, service_client, (void*) (clients + n_clients)) != 0){
            mrerror("Error while creating thread to service newly connected client");
        }

        n_clients++;

        pthread_mutex_unlock(&clients_mutex); pthread_mutex_unlock(&n_clients_mutex);
    }else{
        char* msg = "Maximum number of clients acheived: unable to connect at the moment.";
        if(send(client_fd, msg, strlen(msg), 0) < 0){
            mrerror("Error encountered while communicating with new client");
        }

        close(client_fd);
    }
}

void* service_client(void* arg){
    client* client_ptr;
    client_ptr = (client*) arg;

    char recv_msg[BUFFER_SIZE];
    while(recv(client_ptr->client_fd, recv_msg, BUFFER_SIZE, 0) > 0){
        if(recv_msg[0] == '!'){
            char *token = strtok(recv_msg, " ");
            char **token_list = malloc(0);
            int n_tokens = 0;

            while(token != NULL){
                token_list = realloc(token_list, (n_tokens+1)*sizeof(char*));

                if(token_list != NULL){
                    token_list[n_tokens] = token;
                    token = strtok(NULL, " ");
                    n_tokens++;
                }else{
                    mrerror("Error during tokenisation of client message");
                }
            }

            int msg_flag = 1;

            for(int i = 0; i < N_SFUNCS; i++){
                if(strcmp(token_list[0], sfunc_dict[i]) == 0){
                    (*sfunc)(n_tokens, token_list, client_ptr->nickname);
                    msg_flag = 0;
                    break;
                }
            }

            if(msg_flag){
                sfunc_msg(1, (char*[]){recv_msg}, client_ptr->nickname);
            }
        }else{
            sfunc_msg(1, (char*[]){recv_msg}, client_ptr->nickname);
        }
    }
}

/* ------ SERVER FUNCTIONS (sfunc) ------ */

void sfunc_leaderboard(int argc, char* argv[], char* client_id){}
void sfunc_players(int argc, char* argv[], char* client_id){}
void sfunc_playerstats(int argc, char* argv[], char* client_id){}
void sfunc_battle(int argc, char* argv[], char* client_id){}
void sfunc_quick(int argc, char* argv[], char* client_id){}
void sfunc_chill(int argc, char* argv[], char* client_id){}
void sfunc_go(int argc, char* argv[], char* client_id){}
void sfunc_nickname(int argc, char* argv[], char* client_id){}
void sfunc_help(int argc, char* argv[], char* client_id){}

void sfunc_msg(int argc, char* argv[], char* client_id){
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i] != NULL){
            char* msg = clients[i]->nickname;
            strcat(msg, ">\t");
            strcat(msg, argv[0]);

            if(send(clients[i]->client_fd, msg, strlen(msg), 0) < 0){
                smrerror(strcat("Unable to send message to client ", clients[i]->nickname));
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* --------- UTILITY FUNCTIONS --------- */

char* gen_nickname(){
    char* keywords1[6] = {"Big", "Little", "Cool", "Lame", "Happy", "Sad"};
    char* keywords2[5] = {"Mac", "Muppet", "Hobbit", "Wizard", "Elf"};
    int not_unique = 1;
    char* nickname;

    while(not_unique){
        int i = rand() % 6, j = rand() % 5, k = (rand() % MAX_CLIENTS) + 1;
        char str_k[(int) floor(log10(k))+2];
        sprintf(str_k, "%d", k);

        nickname = keywords1[i];
        strcat(nickname, keywords2[j]);
        strcat(nickname, str_k);

        not_unique = nickname_uniqueQ(nickname);
    }

    return nickname;
}

// Returns 1 if the nickname is not unique, 0 otherwise.
int nickname_uniqueQ(char* nickname){
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(strcmp(nickname, clients[i]->nickname)){
            return 1;
        }
    }

    return 0;
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
