#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define BUFF_SIZE 16384
#define BACKLOG 5
#define LOG_FILE "log_20225699.txt"
#define MAX_LOG_TIME_LENGTH 100
#define WELCOME_MSG "+OK Welcome to file server\r\n"
#define CONFIRM_MSG "+OK Please send file\r\n"
#define SUCCESS_MSG "+OK Successful upload\r\n"
#define ERROR_INVALID_CMD "-ERR Invalid command format\r\n"
#define ERROR_CREATE_FILE "-ERR Cannot create file\r\n"
#define ERROR_UPLOAD_FAIL "-ERR Upload failed\r\n"

void write_log(const char* client_ip, int client_port, const char* request, const char* result) {
    FILE* f = fopen(LOG_FILE, "a");
    if (f == NULL) {
        perror("Cannot open log file");
        return;
    }
    time_t current_time = time(NULL);
    struct tm *local_time = localtime(&current_time);
    char time_str[MAX_LOG_TIME_LENGTH];
    strftime(time_str, sizeof(time_str), "[%d/%m/%Y %H:%M:%S]", local_time);
    fprintf(f, "%s$%s:%d$%s$%s\n", time_str, client_ip, client_port, request, result);
    fclose(f);
}

int create_directory_if_not_exists(const char* directory) {
    struct stat st = {0};
    if (stat(directory, &st) == -1) {
        if (mkdir(directory, 0700) == -1) {
            perror("mkdir() error");
            return -1;
        }
    }
    return 0;
}

int create_and_bind_socket(int port) {
    int listen_sock;
    struct sockaddr_in server_addr;

    if((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket() error");
        return -1;
    }

    int opt = 1;
    if(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt() error");
        close(listen_sock);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        perror("bind() error");
        close(listen_sock);
        return -1;
    }

    if(listen(listen_sock, BACKLOG) == -1){
        perror("listen() error");
        close(listen_sock);
        return -1;
    }

    return listen_sock;
}

int send_message(int sock, const char* message) {
    size_t len = strlen(message);
    ssize_t n = send(sock, message, len, 0);
    if(n < 0) {
        perror("send() error");
        return -1;
    }
    return (int)n;
}

/* --- Buffered reader to handle line + leftover bytes --- */
typedef struct {
    char buf[BUFF_SIZE];
    int start; // index of next unread byte
    int end;   // index after last valid byte
} ConnBuf;

/* Fill buffer if empty space exists */
static int fill_buf(int sock, ConnBuf* cb) {
    if(cb->end >= BUFF_SIZE) {
        // no space
        return 0;
    }
    int r = recv(sock, cb->buf + cb->end, BUFF_SIZE - cb->end, 0);
    if(r > 0) cb->end += r;
    return r;
}

/* Read a line up to and including '\n'. Returns length of line (not including null), 0 on EOF, -1 on error.
   Line will be null-terminated in 'out' (size out_size). If line longer than out_size-1, it's truncated. */
int read_line(int sock, ConnBuf* cb, char* out, int out_size) {
    int i;
    while(1) {
        for(i = cb->start; i < cb->end; ++i) {
            if(cb->buf[i] == '\n') {
                int line_len = i - cb->start + 1;
                int copy_len = (line_len < out_size - 1) ? line_len : (out_size - 1);
                memcpy(out, cb->buf + cb->start, copy_len);
                out[copy_len] = '\0';
                cb->start += line_len; // consume up to and including '\n'
                // compact buffer if empty portion at front is large
                if(cb->start == cb->end) {
                    cb->start = cb->end = 0;
                } else if(cb->start > 0 && cb->start > BUFF_SIZE/2) {
                    // move remaining to front
                    memmove(cb->buf, cb->buf + cb->start, cb->end - cb->start);
                    cb->end = cb->end - cb->start;
                    cb->start = 0;
                }
                return copy_len;
            }
        }
        // no newline yet - try to read more
        int r = fill_buf(sock, cb);
        if(r == 0) {
            // peer closed connection
            if(cb->end - cb->start == 0) return 0;
            // return what's left (without newline)
            int left = cb->end - cb->start;
            int copy_len = (left < out_size - 1) ? left : (out_size - 1);
            memcpy(out, cb->buf + cb->start, copy_len);
            out[copy_len] = '\0';
            cb->start = cb->end = 0;
            return copy_len;
        }
        if(r < 0) {
            perror("recv() error in read_line");
            return -1;
        }
    }
}

/* Receive exact 'filesize' bytes into file, consuming leftover in ConnBuf first */
int receive_file_with_buf(int sock, ConnBuf* cb, const char* filepath, unsigned long filesize) {
    FILE* file = fopen(filepath, "wb");
    if(file == NULL) {
        perror("fopen() error");
        return -1;
    }

    unsigned long total_received = 0;

    // First, consume any leftover bytes in buffer
    if(cb->end > cb->start) {
        int avail = cb->end - cb->start;
        int to_write = (avail > (int)filesize) ? (int)filesize : avail;
        if(to_write > 0) {
            if(fwrite(cb->buf + cb->start, 1, to_write, file) != (size_t)to_write) {
                perror("fwrite() error");
                fclose(file);
                return -1;
            }
            cb->start += to_write;
            total_received += to_write;
            if(cb->start == cb->end) cb->start = cb->end = 0;
        }
    }

    // Then receive remaining bytes directly from socket
    char file_buffer[BUFF_SIZE];
    while(total_received < filesize) {
        unsigned long remain = filesize - total_received;
        int to_recv = (remain > BUFF_SIZE) ? BUFF_SIZE : (int)remain;
        int r = recv(sock, file_buffer, to_recv, 0);
        if(r < 0) {
            perror("recv() error while receiving file");
            fclose(file);
            return -1;
        }
        if(r == 0) {
            fprintf(stderr, "Connection closed unexpectedly while receiving file\n");
            fclose(file);
            return -1;
        }
        size_t written = fwrite(file_buffer, 1, r, file);
        if(written != (size_t)r) {
            perror("fwrite() error");
            fclose(file);
            return -1;
        }
        total_received += r;
        printf("\rReceiving: %lu/%lu bytes (%.1f%%)", total_received, filesize, (total_received*100.0)/filesize);
        fflush(stdout);
    }
    printf("\n");
    fclose(file);
    if(total_received != filesize) return -1;
    return 0;
}

int parse_upload_command(const char* recv_data, char* filename, unsigned long* filesize) {
    char command[16];
    char clean_data[BUFF_SIZE];
    strncpy(clean_data, recv_data, BUFF_SIZE-1);
    clean_data[BUFF_SIZE-1] = '\0';
    // strip CRLF
    int len = strlen(clean_data);
    while(len > 0 && (clean_data[len-1] == '\r' || clean_data[len-1] == '\n')) {
        clean_data[--len] = 0;
    }
    int parsed = sscanf(clean_data, "%15s %255s %lu", command, filename, filesize);
    if(parsed != 3) return -1;
    if(strcmp(command, "UPLD") != 0) return -1;
    return 0;
}

void handle_file_upload(int conn_sock, const char* client_ip, int client_port, const char* directory) {
    ConnBuf cb;
    cb.start = cb.end = 0;
    char line[BUFF_SIZE];

    int r = read_line(conn_sock, &cb, line, sizeof(line));
    if(r <= 0) {
        if(r == 0) printf("Connection closed by client while waiting for command\n");
        return;
    }
    printf("Received: %s", line);

    char filename[256];
    unsigned long filesize;
    if(parse_upload_command(line, filename, &filesize) < 0) {
        send_message(conn_sock, ERROR_INVALID_CMD);
        write_log(client_ip, client_port, line, ERROR_INVALID_CMD);
        return;
    }

    if(send_message(conn_sock, CONFIRM_MSG) < 0) {
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);

    int res = receive_file_with_buf(conn_sock, &cb, filepath, filesize);

    char log_request[512];
    snprintf(log_request, sizeof(log_request), "UPLD %s %lu\\r\\n", filename, filesize);

    const char* result_msg;
    if(res == 0) {
        result_msg = SUCCESS_MSG;
        printf("File %s uploaded successfully (%lu bytes)\n", filename, filesize);
    } else {
        result_msg = ERROR_UPLOAD_FAIL;
        printf("File upload failed\n");
        remove(filepath);
    }

    send_message(conn_sock, result_msg);
    write_log(client_ip, client_port, log_request, result_msg);
}

void handle_client_connection(int conn_sock, const char* client_ip, int client_port, const char* directory) {
    printf("You got a connection from %s:%d\n", client_ip, client_port);
    if(send_message(conn_sock, WELCOME_MSG) < 0) {
        close(conn_sock);
        return;
    }
    write_log(client_ip, client_port, "CONNECT", WELCOME_MSG);
    handle_file_upload(conn_sock, client_ip, client_port, directory);
    close(conn_sock);
}

int main(int argc, char* argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: ./server Port_Number Directory_name\n");
        exit(1);
    }
    int port = atoi(argv[1]);
    char* directory = argv[2];
    if(create_directory_if_not_exists(directory) < 0) {
        exit(1);
    }
    int listen_sock = create_and_bind_socket(port);
    if(listen_sock < 0) {
        exit(1);
    }
    printf("Server started at port %d!\n", port);
    printf("Storage directory: %s\n", directory);
    printf("Waiting for connections...\n\n");
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t sin_size = sizeof(struct sockaddr_in);
        int conn_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &sin_size);
        if(conn_sock == -1) {
            perror("accept() error");
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        if(inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN) == NULL) {
            perror("inet_ntop() error");
            close(conn_sock);
            continue;
        }
        int client_port = ntohs(client_addr.sin_port);
        handle_client_connection(conn_sock, client_ip, client_port, directory);
    }
    close(listen_sock);
    return 0;
}

