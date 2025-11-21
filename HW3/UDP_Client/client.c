#define _POSIX_C_SOURCE 200112L

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include"validation.h"

#define BUFF_SIZE 8192

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./client <ServerIP> <PortNumber>\n");
        exit(1);
    }
    
    char *server_ip;
    int server_port = validate_port(argv[2]);
    
    if ((server_port != -1) && (is_valid_ipv4(argv[1]))) {
        server_ip = argv[1];
    }
    else{ 
	    if (!is_valid_ipv4(argv[1])){
	    	fprintf(stderr, "Invalid IPv4 address\n"); 
		}
		if (server_port == -1) {
	        fprintf(stderr, "Error: Invalid port number (must be 1-65535)\n"); 
	    } 
		exit(1);
	} 

    int client_sock;
    struct sockaddr_in server_addr;
    char buffer[BUFF_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    if ((client_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    while (1) {
        printf("\nEnter domain or IP (empty to quit): ");
        if (fgets(buffer, BUFF_SIZE, stdin) == NULL) break;

        buffer[strcspn(buffer, "\n")] = '\0';
        if (strlen(buffer) == 0) break;

        if (sendto(client_sock, buffer, strlen(buffer), 0,
                   (struct sockaddr *)&server_addr, addr_len) < 0) {
            perror("sendto() error");
        }
        else{
        	int received_bytes = recvfrom(client_sock, buffer, BUFF_SIZE - 1, 0,
											(struct sockaddr *)&server_addr, &addr_len);
			
			if(received_bytes < 0){
				perror("recvfrom() error:");
			}
			else{
				buffer[received_bytes] = '\0';
				if (buffer[0] == '+') {
				    printf("Reply from server: %s\n", buffer + 1);
				} else if (buffer[0] == '-') {
				    printf("Reply from server: %s\n", buffer + 1);
				} else {
				    printf("Reply from server: %s\n", buffer);
				}
			}
		}
    }

    close(client_sock);
    return 0;
}

