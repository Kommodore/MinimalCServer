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

                    if(strcmp(client_data.method, "GET") != 0){
                        statuscode = HTTP_LEVEL_500+1;
                    } else { //TODO: isDir
                        file_ptr = fopen(client_data.file, "r+");
                        if(file_ptr == NULL){
                            statuscode = HTTP_LEVEL_400+4;
                            fclose(file_ptr);
                        } else {
                            statuscode = HTTP_LEVEL_200;
                        }
                    }
                    write(client_info.sock_fd, gen_response(file_ptr, statuscode), 4096); // TODO: Iwie Softcoden
                    close(client_info.sock_fd);
                    break;
                }
            }
            return 0;
        }
    } else {
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

/**
 * Method to read the data from the incoming header.
 *
 * @param src The struct to fill with the data.
 * @param input_string The header data as a string.
 */
void read_header_data(client_header* src, char* input_string){
    printf("REQUEST:\n%s", input_string);
    memset((void*)src->file, '\0', 256);
    memset((void*)src->method, '\0', 16);
    sscanf(input_string, "%s %s HTTP1.1", src->method, src->file);
}

/**
 * Method responsible for assembling the server response
 *
 * @param file_ptr File stream of the file to open.
 * @param statuscode The status code of the outgoing http header.
 *
 * @return Pointer to char array containing the response.
 */
char* gen_response(FILE* file_ptr, int statuscode){
    char* header = NULL;
    char* content = NULL;
    char* response = NULL;
    file_info file_meta = {.content_type = "text/html", .file_size = 0, .modify_date = ""};

    // Generate content
    if(statuscode != 200){
        read_error(statuscode, &content, &file_meta);
    } else {
        read_file(file_ptr, &content, &file_meta);
        //strcpy(file_meta.content_type, "image/jpeg"); // TODO: Bei Image ändern?
    }

    // Generate header
    gen_header(&header, statuscode, file_meta);

    response = (char*)malloc(file_meta.file_size+(strlen(header)*sizeof(char)));
    sprintf(response, "%s\n%s", header, content);
    return response;
}

/**
 * Method that generates the header of the response.
 *
 * @param header The pointer to the char array where the header should be written to.
 * @param status_response The status message of the request.
 * @param file_data struct containing all file info needed for the header. (Last modified, content type)
 */
void gen_header(char **header, int status_response, file_info file_data) {
    char char_set[32], curr_time[32], lang[3];

    time_t raw_time = time(NULL);
    strftime(curr_time, 32, "%a, %d %b %Y %T %Z", gmtime(&raw_time));
    strcpy(char_set, "iso-8859-1");
    strcpy(lang, "de");

    // If no modify date is set use the current one
    if(strcmp(file_data.modify_date, "") == 0){
        strcpy(file_data.modify_date, curr_time);
    }

    *header = (char*)malloc(sizeof(char)*256);
    sprintf(*header, "HTTP/1.1 %s\nDate: %s\nLast-Modified: %s\nContent-Language: %s\nContent-Type: %s; charset=%s\n",
            status_lines[status_response], curr_time, file_data.modify_date, lang, file_data.content_type, char_set);
    printf("RESPONSE:\n%s", *header);
}

/**
 * Method to read a file and its meta data.
 *
 * @param file_ptr Pointer to the file stream.
 * @param content Pointer to the location where the content should be saved.
 * @param file_meta Pointer to the struct containing meta data.
 */
void read_file(FILE* file_ptr, char** content, file_info* file_meta) {
    fseek(file_ptr, 0, SEEK_END);
    file_meta->file_size = ftell(file_ptr);
    rewind(file_ptr);
    *content = (char*) malloc((size_t) file_meta->file_size);
    fread(content, sizeof(char), (size_t) file_meta->file_size, file_ptr);
    fclose(file_ptr);
}

/**
 * Method to parse an error message.
 *
 * @param statuscode The code of the message to prase.
 * @param content Pointer to the location where the content should be saved.
 * @param file_meta Meta information of the error message. In this case just for the size.
 */
void read_error(int statuscode, char** content, file_info* file_meta) {
    file_meta->file_size = sizeof(char)*1024;
    *content = (char*) malloc((size_t)file_meta->file_size);
    strcpy(*content, get_error_string(statuscode));
}
