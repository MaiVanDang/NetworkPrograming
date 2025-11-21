#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define BUFF_SIZE 16384
#define MAX_FILEPATH_LENGTH 512

int create_and_connect_socket(const char* server_addr_str, int server_port) {
    int client_sock;
    struct sockaddr_in server_addr;
    if((client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket() error");
        return -1;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_addr_str);
    if(connect(client_sock, (struct sockaddr*)&server_addr, sizeof(struct sockaddr)) < 0){
        perror("connect() error");
        close(client_sock);
        return -1;
    }
    return client_sock;
}

/* Simple line reader (reads until '\n') */
int read_line(int sock, char* out, int out_size) {
    int pos = 0;
    while(pos < out_size - 1) {
        char c;
        int r = recv(sock, &c, 1, 0);
        if(r < 0) {
            perror("recv() error in read_line");
            return -1;
        } else if(r == 0) {
            // connection closed
            if(pos == 0) return 0;
            break;
        }
        out[pos++] = c;
        if(c == '\n') break;
    }
    out[pos] = '\0';
    return pos;
}

/* send message ensuring CRLF at end */
int send_message(int sock, const char* message) {
    // If message already ends with \n, send as is
    size_t len = strlen(message);
    if(len >= 1 && message[len-1] == '\n') {
        int s = send(sock, message, len, 0);
        if(s < 0) { perror("send() error"); return -1; }
        return s;
    } else {
        // append CRLF
        char tmp[BUFF_SIZE];
        snprintf(tmp, sizeof(tmp), "%s\r\n", message);
        int s = send(sock, tmp, strlen(tmp), 0);
        if(s < 0) { perror("send() error"); return -1; }
        return s;
    }
}

/* get filename from path */
const char* get_filename_from_path(const char* filepath) {
    const char* filename = strrchr(filepath, '/');
    if(filename == NULL) return filepath;
    return filename + 1;
}

unsigned long get_file_size(FILE* file) {
    fseek(file, 0, SEEK_END);
    unsigned long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);
    return filesize;
}

int upload_file(int sock, FILE* file, unsigned long filesize) {
    printf("Uploading file...\n");
    unsigned long total_sent = 0;
    char file_buffer[BUFF_SIZE];
    size_t bytes_read;
    while((bytes_read = fread(file_buffer, 1, BUFF_SIZE, file)) > 0) {
        size_t sent = 0;
        while(sent < bytes_read) {
            ssize_t r = send(sock, file_buffer + sent, bytes_read - sent, 0);
            if(r < 0) {
                perror("send() error");
                return -1;
            }
            sent += r;
        }
        total_sent += sent;
        printf("\rSent: %lu/%lu bytes (%.1f%%)", total_sent, filesize, (total_sent*100.0)/filesize);
        fflush(stdout);
    }
    printf("\n");
    if(total_sent != filesize) {
        fprintf(stderr, "Error: sent %lu but expected %lu\n", total_sent, filesize);
        return -1;
    }
    return 0;
}

int get_filepath_input(char* filepath, int max_length) {
    printf("\nEnter file path (empty to quit): ");
    if(fgets(filepath, max_length, stdin) == NULL) return -1;
    filepath[strcspn(filepath, "\n")] = 0;
    if(strlen(filepath) == 0) return 0;
    return 1;
}

int process_file_upload(int sock, const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if(file == NULL) {
        perror("Cannot open file");
        return -1;
    }
    unsigned long filesize = get_file_size(file);
    const char* filename = get_filename_from_path(filepath);
    printf("File: %s, Size: %lu bytes\n", filename, filesize);

    char command[BUFF_SIZE];
    snprintf(command, sizeof(command), "UPLD %s %lu", filename, filesize);

    // Send upload command (send_message will append CRLF)
    if(send_message(sock, command) < 0) { fclose(file); return -1; }

    // Wait for server confirmation line (must read full line)
    char response[BUFF_SIZE];
    int r = read_line(sock, response, sizeof(response));
    if(r <= 0) {
        fprintf(stderr, "Failed to read server response after UPLD\n");
        fclose(file);
        return -1;
    }
    printf("Server: %s", response);

    // Expect +OK ... before sending file
    if(strncmp(response, "+OK", 3) != 0) {
        printf("Server rejected upload\n");
        fclose(file);
        return -1;
    }

    // Send file data
    if(upload_file(sock, file, filesize) < 0) {
        fclose(file);
        return -1;
    }
    // Tell server we finished sending (half-close write side)
    if(shutdown(sock, SHUT_WR) < 0) {
        perror("shutdown() error");
        // not fatal; continue to read final response
    }

    // Read final server response line
    r = read_line(sock, response, sizeof(response));
    if(r <= 0) {
        fprintf(stderr, "Failed to read final server response\n");
        fclose(file);
        return -1;
    }
    printf("Server: %s", response);
    fclose(file);
    return 0;
}

int main(int argc, char* argv[]){
    if(argc != 3){
        fprintf(stderr, "Usage: ./client IP_Addr Port_Number\n");
        exit(1);
    }
    char* server_addr_str = argv[1];
    int server_port = atoi(argv[2]);
    printf("Connecting to server-port %d\n", server_port);

    while(1) {
        int client_sock = create_and_connect_socket(server_addr_str, server_port);
        if(client_sock < 0) {
            fprintf(stderr, "Failed to connect to server\n");
            exit(1);
        }

        // read welcome line
        char welcome[BUFF_SIZE];
        int r = read_line(client_sock, welcome, sizeof(welcome));
        if(r <= 0) {
            fprintf(stderr, "Failed to receive welcome\n");
            close(client_sock);
            continue;
        }
        printf("%s", welcome);

        char filepath[MAX_FILEPATH_LENGTH];
        int input_result = get_filepath_input(filepath, sizeof(filepath));
        if(input_result == 0) {
            printf("Exiting...\n");
            close(client_sock);
            break;
        } else if(input_result < 0) {
            close(client_sock);
            break;
        }

        process_file_upload(client_sock, filepath);
        close(client_sock);
    }
    return 0;
}

