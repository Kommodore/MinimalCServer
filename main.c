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
            char *client_header_data = NULL;
            char input_buffer[HEADER_BUFFER_SIZE], output_buffer[HEADER_BUFFER_SIZE];

            client_info.addr_len = sizeof(client_info.addr);
            client_info.sock_fd = accept(server_info.sock_fd, (struct sockaddr*) &client_info.addr, (socklen_t*) &client_info.addr_len);

            if(client_info.sock_fd < 0)
            {
                printf("Failed to establish connection with client\n");
                exit(1);
            }

            memset((void*)output_buffer, '\0', HEADER_BUFFER_SIZE);

            // Cycle as long as the client sends a message
            for(int iterations = 0;; iterations++) {
                // Alloc enough memory to store whole input string;
                if(iterations == 0){
                    client_header_data = (char*)malloc(sizeof(char)*HEADER_BUFFER_SIZE);
                } else {
                    client_header_data = (char*)realloc(client_header_data, sizeof(client_header_data)*2);
                }
                if(client_header_data == NULL){
                    printf("Error while parsing request\n");
                    exit(ERR_ALLOC);
                }

                // Read all data from the client
                memset((void*)input_buffer, '\0', HEADER_BUFFER_SIZE);
                read_bytes = read(client_info.sock_fd, input_buffer, HEADER_BUFFER_SIZE);
                strcat(client_header_data, input_buffer);

                // If n is smaller than 256, we got all needed data and process the request
                if(read_bytes < HEADER_BUFFER_SIZE) {
                    process_request(client_header_data, &client_info, doc_root);
                    break;
                }
            }
        }
    } else {
        printf("Failed to start server with error code %i\n", error_code);
        exit(error_code);
    }
}

/**
 * Method called after the client request has been retrieved
 *
 * @param request_header The complete header as a start.
 * @param client_info Socket info required to send the response to the client.
 */
void process_request(const char* request_header, socket_info* client_info, const char* doc_root){
    int statuscode = HTTP_OK, is_dir = 0;
    long resp_size = 0;
    char* output = NULL;
    client_header client_data = {.file = "", .method = "", .doc_root = 0};

    read_header_data(&client_data, request_header, doc_root);
    //free((char*)request_header);

    if(strcmp(client_data.method, "GET") != 0){
        output = gen_response(&client_data, is_dir, HTTP_LEVEL_500+1, &resp_size);
    } else {
        struct stat file_stat;
        if(stat(client_data.file, &file_stat) < 0){
            if(errno == EACCES){
                statuscode = HTTP_LEVEL_400+1;
            } else {
                statuscode = HTTP_LEVEL_400+4;
            }
        } else {
            if(S_ISDIR(file_stat.st_mode)){
                is_dir = 1;
            } else {
                FILE* file_ptr = fopen(client_data.file, "r");
                if(file_ptr == NULL){
                    statuscode = HTTP_LEVEL_400+4;
                }
                fclose(file_ptr);
            }
        }
        output = gen_response(&client_data, is_dir, statuscode, &resp_size);
    }
    printf("Outputting\n%s\nwith %li bytes\n", output, resp_size);
    write(client_info->sock_fd, output, (size_t)resp_size);
    close(client_info->sock_fd);
    free(output);
}

/**
 * Starts server in docroot dir
 */
int server_start(const char *dir, int port, socket_info* si)
{
    int reuse = 1;
    if(chdir(dir) != 0) {
        printf("Can't start server in%s, is directory existing?\n", dir);
        return(ERR_DIR);
    }

    //setsockopt(Socket Descriptor, Option level, Reuse already used socket, option name, option value, option value length)
    if((si->sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || setsockopt(si->sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0){
        printf("Cant initialize socket\n");
        return(ERR_SOCK_SET_OPT);
    }

    si->addr_len = sizeof(si->addr);
    memset((void*)&si->addr, '\0', sizeof(si->addr));
    si->addr.sin_family = AF_INET;
    si->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    si->addr.sin_port = htons(port);
    if(bind(si->sock_fd, (struct sockaddr*) &si->addr, sizeof(si->addr)) < 0){
        printf("Can't open socket on %d, is socket already in use?\n", port);
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
void read_header_data(client_header* src, const char* input_string, const char* doc_root){
    char buffer[256];
    printf("REQUEST:\n%s", input_string);
    strcpy(src->file, doc_root);
    sscanf(input_string, "%s %s HTTP1.1", src->method, buffer);
    if(buffer[1] != '\0'){ // Removing unnecessary slash
        memmove(buffer, buffer+1, 255);
        strcat(src->file, buffer);
    } else {
        src->doc_root = 1;
    }
}

/**
 * Method responsible for assembling the server response
 *
 * @param file_ptr File stream of the file to open.
 * @param statuscode The status code of the outgoing http header.
 *
 * @return Pointer to char array containing the response.
 */
char* gen_response(const client_header* client_data, int is_dir, int statuscode, long* resp_size){
    char *header = NULL, *content = NULL, *response = NULL;
    file_info file_meta = {.content_type = "text/html", .file_size = 0, .modify_date = ""};

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
    free(content);
    free(header);
    return response;
}

/**
 * Method that generates the header of the response.
 *
 * @param header The pointer to the char array where the header should be written to.
 * @param status_response The status message of the request.
 * @param file_data struct containing all file info needed for the header. (Last modified, content type)
 */
void gen_header(char **header, int status_response, file_info* file_data) {
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

/**
 * Method to read a directory and return it's contents as a list.
 *
 * @param dir_ptr The directory stream.
 * @param content The variable that should contain the result
 * @param file_meta File meta data. Used to set the size.
 */
void read_dir(const client_header* client_data, char** content, file_info* file_meta) {
    DIR* dir_ptr = opendir(client_data->file);
    struct dirent *dir_item;
    char buffer[256];
    size_t curr_size = 0;
    size_t max_size = 256*sizeof(char);

    *content = (char*)malloc(max_size);
    sprintf(*content, "<!DOCTYPE html><html><head><title>Index of %s</title></head><body><h1>Index of %s</h1><ul>", client_data->file, client_data->file);

    do {
        if((dir_item = readdir(dir_ptr)) != NULL && (strcmp(dir_item->d_name, ".") != 0 || client_data->doc_root == 1) && strcmp(dir_item->d_name, "..") != 0) {
            sprintf(buffer, "<li><a href=\"./%s\">%s</a></li>", dir_item->d_name, dir_item->d_name);

            // Realloc string size if too small
            curr_size += strlen(buffer)*sizeof(char);
            if(curr_size >= max_size){
                max_size *= 2;
                *content = (char*)realloc(*content, max_size);
            }
            strcat(*content, buffer);
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

/**
 * Method to read a file and its meta data.
 *
 * @param file_ptr Pointer to the file stream.
 * @param content Pointer to the location where the content should be saved.
 * @param file_meta Pointer to the struct containing meta data.
 */
void read_file(const char* file_path, char** content, file_info* file_meta) {
    FILE* file_ptr = fopen(file_path, "r");

    // Get file size to alloc the needed space
    fseek(file_ptr, 0, SEEK_END);
    file_meta->file_size = (size_t)ftell(file_ptr);
    rewind(file_ptr);
    *content = (char*) malloc((size_t) file_meta->file_size);

    fread(content, sizeof(char), (size_t) file_meta->file_size, file_ptr); // TODO: Error entfernen
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
    file_meta->file_size = sizeof(char)*1024; // TODO: Harcoded weg
    *content = (char*) malloc((size_t)file_meta->file_size);
    strcpy(*content, get_error_string(statuscode));
}
