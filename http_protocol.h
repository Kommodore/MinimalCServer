#ifndef VERTEILTESYSTEME_PRAKTIKUM2_HTTP_PROTOCOL_H
#define VERTEILTESYSTEME_PRAKTIKUM2_HTTP_PROTOCOL_H

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#define HTTP_FORBIDDEN (HTTP_LEVEL_400+1)
#define HTTP_NOT_FOUND (HTTP_LEVEL_400+4)

#define RESPONSE_CODES 8

static const char *const status_lines[RESPONSE_CODES] = {
    #define HTTP_LEVEL_200  0
    "200 OK",
    #define HTTP_LEVEL_400 1
    "400 Bad Request",
    "401 Unauthorized",
    "402 Payment Required",
    "403 Forbidden",
    "404 Not Found",
    #define HTTP_LEVEL_500 6
    "500 Internal Server Error",
    "501 Not Implemented",
};

const char *get_error_string(int status_code);

#endif //VERTEILTESYSTEME_PRAKTIKUM2_HTTP_PROTOCOL_H
