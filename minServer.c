#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

#define MAX_INPUT_BUFFER_SIZE 512

static int CONNECTION_COUNT = 0;

const char *TYPE_IMAGE_PNG = "image/png";
const char *TYPE_IMAGE_JPEG = "image/jpeg";
const char *TYPE_TEXT_HTML = "text/html";

const char *TEST_RESPONSE = "HTTP/1.1 200 OK\nContent-Type: text/html; charset=iso-8859-1\n\n<!DOCTYPE html><html><head><title>HELLO WORLD</title></head><body><h1>Hello World</h1><a href=\"test\">Test-Link</a></body>";

const char *TEST_RESPONSE_SUCC = "HTTP/1.1 200 OK\nContent-Type: text/html; charset=iso-8859-1\n\n<!DOCTYPE html><html><head><title>FOUND</title></head><body><h1>Page / Dir found</h1><a href=\"/\">Back to docroot</a></body>";

const char *TEST_RESPONSE_NOT_FOUND = "HTTP/1.1 404 OK\nContent-Type: text/html; charset=iso-8859-1\n\n<!DOCTYPE html><html><head><title>NOT FOUND</title></head><body><h1>Page / Dir not found</h1><a href=\"/\">Back to docroot</a></body>";

const char *TEST_RESPONSE_HEADER = "HTTP/1.1 200 OK\nContent-Type: %s\n\n";

const char *TEST_RESPONSE_FOLDER = "HTTP/1.1 200 OK\nContent-Type: text/html; charset=iso-8859-1\n\n<!DOCTYPE html><html><head><title>FOUND</title></head><body><h1>Directory: %s</h1><br><br>%s</body>";

typedef struct SocketInfo
{
    int sock_fd;
    int addr_len;
    struct sockaddr_in addr;
} SocketInfo;

void process_request(char *request_header_data, SocketInfo *client)
{
    //NOTE (jonas): Hier beginnen die Unterschiede zu unserem eigentlichen Programm. Der Anfang ist genau gleich!
    //NOTE (jonas): Wenn du testen willst, musst du einen Ordner "test" in dem docroot ordner anlegen. Den Ordnernamen "test" habe ich
    //                  hard-coded in TEST_RESPONSE, das ist praktisch die Startseite, die ausgegeben wird, wenn du nur ein '/' im request header hast
    char method[256];
    char path[256];
    char *rel_path;
    char *ending;
    char *response_header;
    char *response;

    size_t file_size;
    FILE *file;

    DIR *dir;
    struct dirent *dp;

    sscanf(request_header_data, "%s %s HTTP1.1", method, path);
    
    if(strcmp(method, "GET") == 0)
    {
        printf("%s\n%s\n", method, path);
        if(strcmp(path, "/") == 0)
        {
            //TODO: docroot muss anders behandelt werden, um '.' und '..' zu entfernen
            //NOTE (jonas): Hier wird eine Testseite ausgegeben, die zu einem Ordner verlinkt
            write(client->sock_fd, TEST_RESPONSE, strlen(TEST_RESPONSE));
            close(client->sock_fd);
        }
        else 
        {
            //NOTE (jonas): Hier wird überprueft, ob es sich um einen Ordner oder eine Datei handelt, die aufgerufen werden soll
            //NOTE (jonas): In der if() wird hinter dem letzten '/' getestet, ob in dem string ein '.' ist, wenn ja, dann ist es eine Datei, sonst nicht
            rel_path = path+1;
            printf("REL_PATH: %s\n", rel_path);
            
            if((ending = strchr(strrchr(path, '/'), '.')) != NULL)
            {
                //NOTE (jonas): Wenn es eine Datei ist, wird hier weitergemacht. Es wird nur auf drei verschiedene Types getestet (html, png, jpeg)
                //NOTE (jonas): Wenn es keines der beiden ist, dann wird der Kindprozess beendet. Man sollte allerdings eher eine Fehlerseite zurückgeben
                //NOTE (jonas): Ausserdem wird hier noch nicht getestet, ob der file wirklich existiert. Das muss auch noch implementiert werden
                
                //TODO: Teste, ob file existiert
                printf("DATEI: %s\n", ending);
                file = fopen(rel_path, "r");

                fseek(file, 0, SEEK_END);
                file_size = (size_t)ftell(file);
                rewind(file);
                
                if(strcmp(ending, ".html") == 0)
                {
                    response_header = (char *)malloc(strlen(TEST_RESPONSE_HEADER) + strlen(TYPE_TEXT_HTML));
                    sprintf(response_header, TEST_RESPONSE_HEADER, TYPE_TEXT_HTML);    
                }
                else if(strcmp(ending, ".png") == 0)
                {
                    response_header = (char *)malloc(strlen(TEST_RESPONSE_HEADER) + strlen(TYPE_IMAGE_PNG));
                    sprintf(response_header, TEST_RESPONSE_HEADER, TYPE_IMAGE_PNG);
                }
                else if(strcmp(ending, ".jpeg") == 0)
                {
                    response_header = (char *)malloc(strlen(TEST_RESPONSE_HEADER) + strlen(TYPE_IMAGE_JPEG));
                    sprintf(response_header, TEST_RESPONSE_HEADER, TYPE_IMAGE_JPEG);
                }
                else 
                {
                    printf("Unknown type");
                    exit(1);
                }
                
                response = (char *)malloc((size_t)file_size + strlen(response_header));
                
                strcpy(response, response_header);
                fread((response + strlen(response_header)), sizeof(char), file_size, file);
            
                printf("RESPONSE:\n%s", response);
                write(client->sock_fd, response, file_size + strlen(response_header));
                close(client->sock_fd);
            }
            else 
            {
                //NOTE (jonas): Hier wird einfach durch den Ordner gesucht und in das minimale HTML-Template eingetragen. Alles sehr einfach gehalten
                printf("ORDNER\n");
                if((dir = opendir(rel_path)) != NULL)
                {
                    //TODO: Zeige Ordner / Datei - Struktur
                    //1024 ist nicht viel!
                    char folder_list[1024];
                    char element[256];

                    memset((void *)folder_list, '\0', 1024);
                    memset((void *)element, '\0', 256);

                    strcat(folder_list, "<ul>");

                    printf("REL_PATH_2: %s\n", rel_path);

                    do
                    {
                        if((dp = readdir(dir)) != NULL && strcmp(dp->d_name, ".") != 0)
                        {
                            //NOTE (jonas): Hier scheint das Problem zu entstehen. Wie du gesagt hast, habe ich hier 
                            //                 einfach ein ../ vor den pfad geschrieben. allerdings hilft das nur bei der 
                            //                 ersten ebene. Je weiter ich runter in die Ordner gehe, desto mehr ../ muss ich einfuegen
                            sprintf(element, "<li><a href=\"../%s/%s\">%s</a></li>", rel_path, dp->d_name, dp->d_name);
                            strcat(folder_list, element);
                            printf("Dir: %s\n", element);
                            memset((void *)element, '\0', 256);
                        }
                    } while (dp != NULL);

                    strcat(folder_list, "</ul>");

                    response = (char *)malloc(strlen(TEST_RESPONSE_FOLDER) + strlen(folder_list) + strlen(rel_path));
                    sprintf(response, TEST_RESPONSE_FOLDER, rel_path, folder_list);

                    write(client->sock_fd, response, strlen(TEST_RESPONSE_FOLDER) + strlen(folder_list) + strlen(rel_path));
                    close(client->sock_fd);       
                }
                else 
                {
                    write(client->sock_fd, TEST_RESPONSE_NOT_FOUND, strlen(TEST_RESPONSE_NOT_FOUND));
                    close(client->sock_fd);
                }
                closedir(dir);
            }
            
            
        }
    }
    else 
    {
        //NOTE (jonas): Falls eine andere Methode als GET verwendet wird, beendet das den Kindprozess
        // TODO: Stattdessen 501 Page anzeigen
        printf("Unsupported method: %s\n", method);
        exit(1);
    }
}

void handle_connection(SocketInfo *client)
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

        //NOTE: Der Header wird vollstaendig gelesen, selbst wenn er groesser als MAX_INPUT_BUFFER_SIZE ist!
        memset((void*)max_input_buffer, '\0', MAX_INPUT_BUFFER_SIZE);
        read_bytes = read(client->sock_fd, max_input_buffer, MAX_INPUT_BUFFER_SIZE);
        strcat(request_header_data, max_input_buffer);

        //NOTE: Wenn read_bytes kleiner ist als MAX_INPUT_BUFFER_SIZE, dann ist der Heade vollstaendig gelesen!
        if(read_bytes < MAX_INPUT_BUFFER_SIZE) 
        {
            process_request(request_header_data, client);
            
            //TODO: Sollte man hier free aufrufen? (performance)
            free(request_header_data);
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

int main(int argc, char** argv)
{
    int pid;

    char docroot[256];
    int server_port;

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
