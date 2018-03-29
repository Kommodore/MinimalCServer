#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <time.h>

#define bool int

#define HTTP_OK (HTTP_LEVEL_200)
#define HTTP_FORBIDDEN (HTTP_LEVEL_400+1)
#define HTTP_NOT_FOUND (HTTP_LEVEL_400+4)

#define RESPONSE_CODES 8

#define MAX_INPUT_BUFFER_SIZE 512

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

static int CONNECTION_COUNT = 0;

typedef struct SocketInfo
{
    int sock_fd;
    int addr_len;
    struct sockaddr_in addr;
} SocketInfo;

typedef struct ClientHeader
{
    char method[16];
    char file[256];
    bool is_docroot;
} ClientHeader;

typedef struct FileInfo
{
    size_t file_size;
    char content_type[32];
    char modify_date[32];
} FileInfo;

void start_server(SocketInfo *server, const char *dir, int port);

void handle_connection(SocketInfo* client);

void process_request(char *request_header_data, SocketInfo *client);

void read_header_data(ClientHeader* src, const char* input_string);

char* gen_response(const ClientHeader* client_data, int is_dir, int statuscode, long* resp_size);

void read_dir(const ClientHeader* client_data, char** content, FileInfo* file_meta);

void read_file(const char* file_path, char** content, FileInfo* file_meta);

void read_error(int statuscode, char** content, FileInfo* file_meta);

const char* get_error_string(int status_code);

void gen_header(char **header, int status_response, FileInfo* file_data);

void gen_header(char **header, int status_response, FileInfo* file_data) {
    char char_set[32], curr_time[32], lang[3];

    time_t raw_time = time(NULL);
    strftime(curr_time, 64, "%a, %d %b %Y %T %Z", gmtime(&raw_time));
    strcpy(char_set, "iso-8859-1");
    strcpy(lang, "de");

    // If no modify date is set use the current one
    if(strcmp(file_data->modify_date, "") == 0){
        strcpy(file_data->modify_date, curr_time);
    }

    *header = (char*)malloc(sizeof(char)*256);
    sprintf(*header, "HTTP/1.1 %s\nDate: %s\nLast-Modified: %s\nContent-Language: %s\nContent-Type: %s; charset=%s\n",
            status_lines[status_response], curr_time, file_data->modify_date, lang, file_data->content_type, char_set);
    printf("RESPONSE:\n%s", *header);
}

const char* get_error_string(int status_code){
    char* error = malloc(sizeof(char)*512);
    sprintf(error, "<!DOCTYPE html><html><header><title>%s</title></head><body><h1>%s</h1>", status_lines[status_code], status_lines[status_code]);

    switch(status_code){
        case HTTP_NOT_FOUND:
            strcat(error, "<p>The requested URL [URL] was not found on this server.</p>\n");
            break;
        case HTTP_FORBIDDEN:
            strcat(error, "<p>You don't have permission to access [URL] on this server.</p>\n");
        default:
            strcat(error, "<p>The server encountered an internal error or misconfiguration and was unable to complete your request.</p>\n"
                    "<p>Please contact the server administrator at [MAIL] to inform them of the time this error occurred, and the actions you performed just before this error.</p>\n"
                    "<p>More information about this error may be available\nin the server error log.</p>");
            break;
    }

    strcat(error, "<hr><p>Super Duper Server 1.0.0 at <i>[HOST]</i></p></body></html>");
    return error;
}

void read_file(const char* file_path, char** content, FileInfo* file_meta) {
    FILE* file_ptr = fopen(file_path, "r");

    // Get file size to alloc the needed space
    fseek(file_ptr, 0, SEEK_END);
    file_meta->file_size = (size_t)ftell(file_ptr);
    rewind(file_ptr);
    *content = (char*) malloc((size_t) file_meta->file_size);
    fread(*content, sizeof(char), (size_t) file_meta->file_size, file_ptr); // TODO: Error entfernen
    fclose(file_ptr);
}

void read_error(int statuscode, char** content, FileInfo* file_meta) {
    file_meta->file_size = sizeof(char)*1024; // TODO: Harcoded weg
    *content = (char*) malloc((size_t)file_meta->file_size);
    strcpy(*content, get_error_string(statuscode));
}

void read_dir(const ClientHeader* client_data, char** content, FileInfo* file_meta) {
    DIR* dir_ptr = opendir(client_data->file);
    struct dirent *dir_item;
    char buffer[256];
    size_t curr_size = 0;
    size_t max_size = 256*sizeof(char);

    *content = (char*)malloc(max_size);
    sprintf(*content, "<!DOCTYPE html><html><head><title>Index of %s</title></head><body><h1>Index of %s</h1><ul>", client_data->file, client_data->file);

    do {
        if((dir_item = readdir(dir_ptr)) != NULL && strcmp(dir_item->d_name, ".") != 0) {
            if(strcmp(dir_item->d_name, "..") != 0 || client_data->is_docroot == 0){
                sprintf(buffer, "<li><a href=\"./%s\">%s</a></li>", dir_item->d_name, dir_item->d_name);

                // Realloc string size if too small
                curr_size += strlen(buffer)*sizeof(char);
                if(curr_size >= max_size){
                    max_size *= 2;
                    *content = (char*)realloc(*content, max_size);
                }
                strcat(*content, buffer);
            }
        }
    } while (dir_item != NULL);

    // Add the closing tags
    curr_size +=20*sizeof(char);
    if(curr_size > max_size){
        *content = (char*)realloc(*content, max_size+(20*sizeof(char)));
    }
    file_meta->file_size = max_size+(20*sizeof(char));

    strcat(*content, "</ul></body></html>");
    closedir(dir_ptr);
}


char* gen_response(const ClientHeader* client_data, int is_dir, int statuscode, long* resp_size)
{
    char *header = NULL, *content = NULL, *response = NULL;
    FileInfo file_meta = {.content_type = "text/html", .file_size = 0, .modify_date = ""};

    // Generate content
    if(statuscode != HTTP_OK){
        read_error(statuscode, &content, &file_meta);
    } else {
        if(is_dir == 1){
            read_dir(client_data, &content, &file_meta);
        } else {
            read_file(client_data->file, &content, &file_meta);
            //strcpy(file_meta.content_type, "image/jpeg"); // TODO: Bei Image ändern?
        }
    }

    // Generate header
    gen_header(&header, statuscode, &file_meta);
    *resp_size = file_meta.file_size+(strlen(header)*sizeof(char));
    response = (char*)malloc((size_t)*resp_size);
    memset((void*)response, '\0', *resp_size);
    sprintf(response, "%s\n%s", header, content);
    //free(content);
    //free(header);
    return response;
}

void read_header_data(ClientHeader* src, const char* input_string)
{
    char buffer[256];
    printf("REQUEST:\n%s", input_string);
    strcpy(src->file, getcwd(buffer, 256));
    sscanf(input_string, "%s %s HTTP1.1", src->method, buffer);
    if(buffer[1] != '\0'){ // Removing unnecessary slash
        strcat(src->file, buffer);
    } else {
        src->is_docroot = 1;
    }
}

void process_request(char *request_header_data, SocketInfo *client)
{
    int statuscode = HTTP_OK;
    bool is_dir = 0;
    long resp_size = 0;
    char* output = NULL;
    ClientHeader client_data = {.file = "", .method = "", .is_docroot = 0};

    read_header_data(&client_data, request_header_data);
    //free((char*)request_header_data);

    if(strcmp(client_data.method, "GET") != 0)
    {
        output = gen_response(&client_data, is_dir, HTTP_LEVEL_500+1, &resp_size);
    } 
    else 
    {
        struct stat file_stat;
        if(stat(client_data.file, &file_stat) < 0)
        {
            if(errno == EACCES)
            {
                statuscode = HTTP_LEVEL_400+1;
            } 
            else 
            {
                statuscode = HTTP_LEVEL_400+4;
            }
        } 
        else 
        {
            //TODO: File_ptr übergeben?
            if(S_ISDIR(file_stat.st_mode)) // NOLINT
            {
                is_dir = 1;
            } 
            else 
            {
                FILE* file_ptr = fopen(client_data.file, "r");
                if(file_ptr == NULL)
                {
                    statuscode = HTTP_LEVEL_400+4;
                }
                fclose(file_ptr);
            }
        }
        output = gen_response(&client_data, is_dir, statuscode, &resp_size);
    }
    printf("Outputting\n%s\nwith %li bytes\n", output, resp_size);
    write(client->sock_fd, output, (size_t)resp_size);
    close(client->sock_fd);
    //free(output);
}

void handle_connection(SocketInfo* client)
{
    int iterations;
    ssize_t read_bytes;
    char *request_header_data = NULL;
    char max_input_buffer[MAX_INPUT_BUFFER_SIZE];

    printf("%d: Connected!\n", CONNECTION_COUNT); 

    for(iterations = 0;;++iterations)
    {
        if(iterations == 0)
        {
            request_header_data = (char*)malloc(sizeof(char)*MAX_INPUT_BUFFER_SIZE);
        } 
        else 
        {
            request_header_data = (char*)realloc(request_header_data, strlen(request_header_data)*2);
        }
        if(request_header_data == NULL)
        {
            printf("Error while parsing request\n");
            exit(1);
        }

        // Read all data from the client
        memset((void*)max_input_buffer, '\0', MAX_INPUT_BUFFER_SIZE);
        read_bytes = read(client->sock_fd, max_input_buffer, MAX_INPUT_BUFFER_SIZE);
        strcat(request_header_data, max_input_buffer);

        // If n is smaller than 256, we got all needed data and process the request
        if(read_bytes < MAX_INPUT_BUFFER_SIZE) 
        {
            process_request(request_header_data, client);
            break;
        }
    }

}

void start_server(SocketInfo *server, const char *dir, int port)
{
    int reuse = 1;
    char path[256];

    if(chdir(dir) != 0)
    {
        printf("Can't start server in \"%s\", is directory existing?\n", dir);
        exit(1);
    }
    printf("CWD: %s\n", getcwd(path, 256));

    if((server->sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || setsockopt(server->sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        printf("Cant initialize socket\n");
        exit(1);
    }

    memset((void*)&server->addr, '\0', sizeof(server->addr));
    server->addr_len = sizeof(server->addr);
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = htonl(INADDR_ANY); // NOLINT
    server->addr.sin_port = htons(port); // NOLINT
    if(bind(server->sock_fd, (struct sockaddr*) &server->addr, sizeof(server->addr)) < 0)
    {
        printf("Can't open socket on %d, is socket already in use?\n", port);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    int pid;

    int server_port;
    char docroot[256];

    SocketInfo server_info;
    SocketInfo client_info;


    // TODO:Besser checken, was los ist!
    if(argc < 3)
    {
        printf("Usage: httpServ <documentRoot> <port>\n");
        exit(1);
    }

    strcpy(docroot, argv[1]);
    server_port = atoi(argv[2]); // NOLINT

    start_server(&server_info, docroot, server_port);
    printf("Server started in %s on port %d\n", docroot, server_port);
    listen(server_info.sock_fd, 5);

    for(;;)
    {
        client_info.addr_len = sizeof(client_info.addr);

        if((client_info.sock_fd = accept(server_info.sock_fd, (struct sockaddr *)&client_info.addr, (socklen_t *)&client_info.addr_len)) < 0)
        {
            //TODO: Sollte der Server beendet werden, wenn client verbindung verliert?
            printf("Failed to establish connection with client\n");
            continue;
        }

        ++CONNECTION_COUNT;

        if((pid = fork()) < 0)
        {
            printf("Failed to fork()\n");
            exit(1);
        }
        else if(pid == 0)
        {
            //NOTE: Hier sind wir definitiv im Kindprozess
            close(server_info.sock_fd);
            handle_connection(&client_info);
            exit(0);
        } //NOTE: Hier nicht mehr!

        close(client_info.sock_fd);
    }
}
