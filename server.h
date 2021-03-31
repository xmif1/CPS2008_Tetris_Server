//
// Created by Xandru Mifsud on 31/03/2021.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef CPS2008_TETRIS_SERVER_H
#define CPS2008_TETRIS_SERVER_H

// NETWORKING CONFIG
#define PORT 6666
#define DOMAIN AF_INET // or AF_INET6, correspondingly using netinet/in.h
#define TYPE SOCK_STREAM

// GLOBALS
struct sockaddr_in sockaddrIn = {.sin_family = DOMAIN, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT)};

int server_init();
void mrerror(char* err_msg);
void red();
void reset();


#endif //CPS2008_TETRIS_SERVER_H
