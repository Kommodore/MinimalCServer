#include "http_protocol.h"

const char* get_error_string(int status_code){
    char* error = malloc(sizeof(char)*512);
    sprintf(error, "<!DOCTYPE html><html><header><title>%s</title></head><body><h1>%s</h1>", status_lines[status_code], status_lines[status_code]);

    switch(status_code){
        case HTTP_NOT_FOUND:
            strcat(error, "<p>The requested URL [URL] was not found on this server.</p>\n");
            break;
        default:
            strcat(error, "<p>The server encountered an internal error or misconfiguration and was unable to complete your request.</p>\n"
                    "<p>Please contact the server administrator at [MAIL] to inform them of the time this error occurred, and the actions you performed just before this error.</p>\n"
                    "<p>More information about this error may be available\nin the server error log.</p>");
            break;
    }

    strcat(error, "<hr><p>Super Duper Server 1.0.0 at <i>[HOST]</i></p></body></html>");
    return error;
}
