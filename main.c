#include "main.h"

int main(int argc, char** argv) {

    int error_code = 0;
    int server_port;
    char doc_root[256];

    socket_info server_info;
    socket_info client_info;

    if(argc < 3){
        printf("Usage: httpServ <documentRoot> <port>\n");
        exit(ERR_PARAM);
    }

    strcpy(doc_root, argv[1]);
    server_port = atoi(argv[2]); // NOLINT

    if((error_code = server_start(doc_root, server_port, &server_info)) == 0)
    {
        printf("Server started in %s on port %d\n", doc_root, server_port);
        listen(server_info.sock_fd, 5);

        // Start a new thread for each request.
        for(;;){
            ssize_t read_bytes;
            char *client_header_data;
            char input_buffer[HEADER_BUFFER_SIZE], output_buffer[HEADER_BUFFER_SIZE];
            client_header client_data;

            client_info.addr_len = sizeof(client_info.addr);
            client_info.sock_fd = accept(server_info.sock_fd, (struct sockaddr*) &client_info.addr, (socklen_t*) &client_info.addr_len);

            if(client_info.sock_fd < 0)
            {
                printf("Failed to establish connection with client\n");
                exit(1);
            }

            memset((void*)output_buffer, '\0', HEADER_BUFFER_SIZE);

            // Cycle as long as the client sends a message
            for(int iterations = 0;; iterations++)
            {
                // Alloc enough memory to store whole input string;
                if(iterations == 0){
                    client_header_data = (char*)malloc(sizeof(char)*HEADER_BUFFER_SIZE);
                } else {
                    #pragma clang diagnostic push
                    #pragma clang diagnostic ignored "-Wuninitialized"
                    client_header_data = (char*)realloc(client_header_data, sizeof(client_header_data)*2);
                    #pragma clang diagnostic pop
                }
                if(client_header_data == NULL){
                    printf("Error while parsing request\n");
                    exit(ERR_ALLOC);
                }

                // Read all data from the client
                memset((void*)input_buffer, '\0', HEADER_BUFFER_SIZE);
                read_bytes = read(client_info.sock_fd, input_buffer, HEADER_BUFFER_SIZE);
                strcat(client_header_data, input_buffer);

                // If n is smaller than 256, we got all needed data and can break
                if(read_bytes < HEADER_BUFFER_SIZE)
                {
                    int statuscode;
                    FILE* file_ptr = NULL;

                    read_header_data(&client_data, client_header_data);
                    printf("Method: %s, File: %s\n",client_data.method, client_data.file);

                    if(strcmp(client_data.method, "GET") != 0){
                        statuscode = 501;
                    } else {
                        file_ptr = fopen(client_data.file, "r+");
                        if(file_ptr == NULL){
                            statuscode = 404;
                        } else {
                            statuscode = 200;
                        }
                    }
                    write(client_info.sock_fd, gen_response(file_ptr, statuscode), HEADER_BUFFER_SIZE);
                    close(client_info.sock_fd);
                    break;
                }
            }
            return 0;
        }

    } else
    {
        printf("Failed to start server with error code %i\n", error_code);
        exit(error_code);
    }


}

/**
 * Prints all folders in directory
 */
int server_opendir(char *dir)
{
    DIR *dirp;
    struct dirent *dp;

    if((dirp = opendir(dir)) == NULL)
    {
        printf("The directory: %s does not exist!\n", dir);
        return 0;
    }

    do
    {
        if((dp = readdir(dirp)) != NULL)
        {
            printf("Dir: %s\n", dp->d_name);
        }
    } while (dp != NULL);

    return 1;
}

/**
 * Starts server in docroot dir
 */
int server_start(char *dir, int port, socket_info* si)
{
    int reuse = 1;
    if(chdir(dir) != 0)
    {
        printf("Couldn't start server in: %s\n", dir);
        return(ERR_DIR);
    }

    if((si->sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("Can't open socket: %i\n", si->sock_fd);
        return(ERR_SOCK_OPEN);
    }

    //setsockopt(Socket Descriptor, Option level, Reuse already used socket, option name, option value, option value length)
    if(setsockopt(si->sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0){
        printf("Cant set socket options\n");
        return(ERR_SOCK_SET_OPT);
    }

    si->addr_len = sizeof(si->addr);
    memset((void*)&si->addr, '\0', sizeof(si->addr));
    si->addr.sin_family = AF_INET;
    si->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    si->addr.sin_port = htons(port);
    if(bind(si->sock_fd, (struct sockaddr*) &si->addr, sizeof(si->addr)) < 0){
        printf("Can't bind local address\n");
        return(ERR_SOCK_NO_BIND);
    }

    return 0;
}

void read_header_data(client_header* src, char* input_string){
    memset((void*)src->file, '\0', 256);
    memset((void*)src->method, '\0', 16);
    sscanf(input_string, "%s %s HTTP1.1", src->method, src->file);
}

char* gen_response(FILE* file_ptr, int statuscode){
    char* header;
    char* content;
    char* response;
    long file_size;
    server_header server_data;

    header = (char*)malloc(sizeof(char)*256);

    server_data.status_code = statuscode;
    strcpy(server_data.charSet, "iso-8859-1");
    strcpy(server_data.lang, "de");

    if(statuscode != 200){ds
        if(get_status_message(statuscode) != NULL) {
            char error_file[256];
            sprintf(error_file, "/etc/CMiniServer/error_pages/error_%d.html", statuscode);
            file_ptr = fopen(error_file, "r+");
        }

        if(file_ptr == NULL){
            printf("Couldn't find status code or error file for %d\n",statuscode);
            file_ptr = fopen("/etc/CMiniServer/error_pages/error_404.html", "r+");
        }
    }

    if(file_ptr == NULL){
        file_size = sizeof(char)*256;
        content = (char*)malloc((size_t)file_size);

        strcpy(content, "<h1>No error file found. Please contact the administrator.</h1>");
    } else {
        // Read file into content
        fseek(file_ptr, 0, SEEK_END);
        file_size = ftell(file_ptr);
        rewind(file_ptr);
        content = (char*) malloc((size_t) file_size);
        fread(content, sizeof(char), (size_t) file_size, file_ptr);
    }

    fclose(file_ptr);
    gen_header(header, server_data);
    response = (char*)malloc(file_size+strlen(header)*sizeof(char));
    sprintf(response, "%s\n%s", header, content);

    return response;
}

// TODO: gmttime() oder localtime(), content type und last modified
void gen_header(char *header, server_header header_data)
{
    sprintf(header, "HTTP/1.1 %d %s\nDate: Tue, 20 Mar 2018 15:06:52 GMT\nLast-Modified: Tue, 20 Mar 2018 14:06:52 GMT\nContent-Language: de\nContent-Type: text/html; charset=iso-8859-1\n",
            header_data.status_code, get_status_message(header_data.status_code));
    printf("RESPONSE: %s", header);
}
