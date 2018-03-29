#ifndef VERTEILTESYSTEME_PRAKTIKUM2_SERVERMAIN_H
#define VERTEILTESYSTEME_PRAKTIKUM2_SERVERMAIN_H

#define bool int
#define MAX_INPUT_BUFFER_SIZE 512

#define MIME_TYPES "/etc/MinimalCServer/mime.types"

static int CONNECTION_COUNT = 0;

const char* DIR_LIST_TEMPLATE = "<!DOCTYPE html><html><head><title>Index of %s</title></head><body><h1>Index of %s</h1><ul>";

typedef struct SocketInfo
{
    int sock_fd;
    int addr_len;
    struct sockaddr_in addr;
} SocketInfo;

typedef struct ClientHeader
{
    char method[16];
    char file_request[256];
    char file_path[512];
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

void gen_header(char **header, int status_response, FileInfo* file_data);

void read_dir(const ClientHeader* client_data, char** content, FileInfo* file_meta);

void read_file(const char* file_path, char** content, FileInfo* file_meta);

void read_error(int statuscode, char** content, FileInfo* file_meta);

#endif //VERTEILTESYSTEME_PRAKTIKUM2_SERVERMAIN_H
