#ifndef VERTEILTESYSTEME_PRAKTIKUM2_MAIN_H
#define VERTEILTESYSTEME_PRAKTIKUM2_MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include "http_protocol.h"

#define HEADER_BUFFER_SIZE 256

#define ERR_PARAM 2
#define ERR_DIR 3
#define ERR_SOCK_OPEN 4
#define ERR_SOCK_SET_OPT 5
#define ERR_SOCK_NO_BIND 6
#define ERR_ALLOC 7

typedef struct
{
    int sock_fd;
    int addr_len;
    struct sockaddr_in addr;
} socket_info;

typedef struct {
    int status_message;
    char* curr_date;
    char* mod_date;
    char lang[3];
    char type[128];
    char charSet[16];
} server_header;

typedef struct {
    char method[16];
    char file[256];
} client_header;

int server_start(char* dir, int port, socket_info* si);

void read_header_data(client_header* src, char* input_string);

char* gen_response(FILE* file_ptr, int statuscode);

void gen_header(char *header, server_header header_data);

#endif //VERTEILTESYSTEME_PRAKTIKUM2_MAIN_H
