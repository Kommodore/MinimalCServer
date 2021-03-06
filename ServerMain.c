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

#include "http_protocol.h"
#include "ServerMain.h"


/**
 * Method to start the server.
 *
 * @param SocketInfo *server Contains the connection data.
 * @param dir The directory to start the server in.
 * @param port The port to start the server on.
 */
void start_server(SocketInfo *server, const char *dir, int port)
{
    int reuse = 1;

    if(chdir(dir) != 0)
    {
        printf("Can't start server in \"%s\", is directory existing?\n", dir);
        exit(1);
    }

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

/**
 * Method to accept incoming connections and fork a new process for them.
 *
 * @param SocketInfo *client The connection data of the client.
 */
void handle_connection(SocketInfo* client)
{
    int iterations;
    ssize_t read_bytes;
    char *request_header_data = NULL;
    char max_input_buffer[MAX_INPUT_BUFFER_SIZE];

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

/**
 * Method to decide what to do with a client request and check whether it is valid or not.
 *
 * @param char *request_header_data A char array containing the browser request.
 * @param SocketInfo *client Struct containing the connection details of the client.
 */
void process_request(char *request_header_data, SocketInfo *client)
{
    int statuscode = HTTP_OK;
    bool is_dir = 0;
    long resp_size = 0;
    char* output = NULL;
    ClientHeader client_data = {.file_path = "", .file_request = "", .method = "", .is_docroot = 0};

    read_header_data(&client_data, request_header_data);
    //free((char*)request_header_data);

    if(strcmp(client_data.method, "GET") != 0)
    {
        output = gen_response(&client_data, is_dir, HTTP_LEVEL_500+1, &resp_size);
    }
    else
    {
        struct stat file_stat;
        if(stat(client_data.file_path, &file_stat) < 0)
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
            if(S_ISDIR(file_stat.st_mode)) // NOLINT
            {
                is_dir = 1;
            }
            else
            {
                FILE* file_ptr = fopen(client_data.file_path, "r");
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

/**
 * Method to parse the incoming header data and put it into a struct.
 *
 * @param ClientHeader *src Struct containing the requested file, where to find it and the request method.
 * @param const char *input_string The browser request char array.
 */
void read_header_data(ClientHeader* src, const char* input_string)
{
    char buffer[256];
    printf("REQUEST:\n%s", input_string);
    strcpy(src->file_path, getcwd(buffer, 256));
    sscanf(input_string, "%s %s HTTP1.1", src->method, buffer);
    strcat(src->file_request, buffer);
    if(buffer[1] != '\0'){ // Removing unnecessary slash
        strcat(src->file_path, buffer);
    } else {
        src->is_docroot = 1;
    }
}

/**
 * Method to generate the response based on factors passed by the process_request method.
 *
 * @param ClientHeader *client_data Struct containing the request method and the requested file.
 * @param int is_dir Boolean whether the requested file is a directory or not.
 * @param int statuscode The status code of the request. Decides whether an error page is to be shown.
 * @param long *resp_size The size of the response.
 *
 * @return The complete response in an char array.
 */
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
            read_file(client_data->file_path, &content, &file_meta);
        }
    }

    // Generate header
    gen_header(&header, statuscode, &file_meta);
    *resp_size = file_meta.file_size+(strlen(header)*sizeof(char));
    response = (char*)malloc((size_t)*resp_size);
    memset((void*)response, '\0', *resp_size);

    strcpy(response, header);
    //fread((response + strlen(header)), sizeof(char), file_meta.file_size, content);

    memcpy((response + strlen(header)), content, file_meta.file_size);
    //free(content);
    //free(header);
    return response;
}

/**
 * Method to generate the reponse header.
 *
 * @param char **header Variable that will contain the reponse header string.
 * @param int status_response The status code used to search for the appropriate http message.
 * @param FileInfo *file_data The file data containing the last modified date.
 **/
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
    sprintf(*header, "HTTP/1.1 %s\nDate: %s\nLast-Modified: %s\nContent-Language: %s\nContent-Type: %s; charset=%s\n\n",
            status_lines[status_response], curr_time, file_data->modify_date, lang, file_data->content_type, char_set);
    printf("RESPONSE:\n%s", *header);
}

/**
 * Method to display the contents of a directory
 *
 * @param ClientHeader *client_data Struct containing the request method and the requested file.
 * @param char **content Variable to contain the content of the page.
 * @param FileInfo* file_meta Struct used to set the last modified date.
 */
void read_dir(const ClientHeader* client_data, char** content, FileInfo* file_meta) {
    DIR* dir_ptr = opendir(client_data->file_path);
    struct dirent *dir_item;
    char buffer[512];
    size_t curr_size = 0;
    size_t max_size = 512;

    *content = (char*)malloc(max_size*sizeof(char));
    sprintf(*content, DIR_LIST_TEMPLATE, client_data->file_request, client_data->file_request);
    curr_size = strlen(*content);

    do {
        if((dir_item = readdir(dir_ptr)) != NULL && strcmp(dir_item->d_name, ".") != 0) {
            if(strcmp(dir_item->d_name, "..") != 0 || client_data->is_docroot == 0){
                if(client_data->is_docroot == 0){
                    sprintf(buffer, "<li><a href=\"%s/%s\">%s</a></li>", client_data->file_request, dir_item->d_name, dir_item->d_name);
                } else {
                    sprintf(buffer, "<li><a href=\"./%s\">%s</a></li>", dir_item->d_name, dir_item->d_name);
                }

                curr_size += strlen(buffer);
                if(curr_size >= max_size){
                    max_size *= 2;
                    *content = (char*)realloc(*content, max_size*sizeof(char));
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
    file_meta->file_size = strlen(*content);

    strcat(*content, "</ul></body></html>");
    closedir(dir_ptr);
}

/**
 * Method to read the contents of a file (text or image) and return it.
 *
 * @param const char *file_path The path to the file to be opened.
 * @param char **content The variable to contain the file content.
 * @param FileInfo *file_meta Struct containing the last modified date.
 */
void read_file(const char* file_path, char** content, FileInfo* file_meta) {
    FILE* file_ptr = fopen(file_path, "r");
    FILE* types_ptr;
    char types_line[512];
    char file_extension[128];
    char mime_type[128];

    char *dot = strrchr(file_path, '.');
    if(dot && dot != file_path)
    {
        if((types_ptr = fopen(MIME_TYPES, "r")) != NULL)
        {
            while(fgets(types_line, 512, types_ptr) != NULL)
            {
                if(types_line[0] != '#')
                {
                    sscanf(types_line, "%s %s", mime_type, file_extension);
                    if(strcmp(file_extension, dot+1) == 0){
                        strcpy(file_meta->content_type, mime_type);
                    }
                }
            }
        }
    }

    // Get file_path size to alloc the needed space
    fseek(file_ptr, 0, SEEK_END);
    file_meta->file_size = (size_t)ftell(file_ptr);
    rewind(file_ptr);
    *content = (char*) malloc((size_t) file_meta->file_size);
    fread(*content, sizeof(char), (size_t) file_meta->file_size, file_ptr);
    fclose(file_ptr);
}

/**
 * Method to return an error message.
 *
 * @param int statuscode The status code to be searched for.
 * @param char **content Char array containing the error document.
 * @param FileInfo *file_meta Struct containing the last modified date.
 */
void read_error(int statuscode, char** content, FileInfo* file_meta) {
    file_meta->file_size = sizeof(char)*1024; // TODO: Harcoded weg
    *content = (char*) malloc((size_t)file_meta->file_size);
    strcpy(*content, get_error_string(statuscode));
}

/**
 * Entry point of the program.
 *
 * @param int argc Argument counter.
 * @param char** argv Char array containing all options passed with starting the program. We need a htdocs dir and a port.
 * @return 0 if everything was successfull.
 */
int main(int argc, char **argv)
{
    int pid;

    int server_port;
    char docroot[256];

    SocketInfo server_info;
    SocketInfo client_info;

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
