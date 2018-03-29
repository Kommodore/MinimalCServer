#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_protocol.h"


const char* TEMPLATE = "<!DOCTYPE html><html><header><title>%s</title></head><body><h1>%s</h1>";
const char* NOT_FOUND = "<p>The requested URL [URL] was not found on this server.</p>\n";
const char* FORBIDDEN = "<p>You don't have permission to access [URL] on this server.</p>\n";
const char* DEFAULT = "<p>The server encountered an internal error or misconfiguration and was unable to complete your request.</p>\n"
                      "<p>Please contact the server administrator at [MAIL] to inform them of the time this error occurred, and the actions you performed just before this error.</p>\n"
                      "<p>More information about this error may be available\nin the server error log.</p>";

const char* get_error_string(int status_code){
    char* error = malloc(sizeof(char)*512);
    sprintf(error, TEMPLATE, status_lines[status_code], status_lines[status_code]);

    switch(status_code){
        case HTTP_NOT_FOUND:
            strcat(error, NOT_FOUND);
            break;
        case HTTP_FORBIDDEN:
            strcat(error, FORBIDDEN);
        default:
            strcat(error, DEFAULT);
            break;
    }

    strcat(error, "<hr><p>Super Duper Server 1.0.0 at <i>[HOST]</i></p></body></html>");
    return error;
}
