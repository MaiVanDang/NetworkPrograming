#define _POSIX_C_SOURCE 200112L

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h> 
#include<netdb.h>
#include<time.h>
#include "validation.h"

#define BUFF_SIZE 8192
#define MAX_LOG_TIME_LENGTH 100
#define LOG_FILE "log_20225699.txt"

/**
 * @function log_in_file
 * @brief Writes the client's request and the server's response to a log file.
 *
 * @param request The query string received from the client.
 * @param response The response string sent back to the client.
 *
 * @details
 *  - The log format is: [timestamp]$request$response
 *  - The default log file is defined by LOG_FILE (e.g., "log_20201234.txt").
 */
void log_in_file(const char *request, const char *response) {
    FILE *f = fopen(LOG_FILE, "a");
    if (f == NULL) return;

    time_t current_time = time(NULL);
    struct tm *local_time = localtime(&current_time);
    char time_str[MAX_LOG_TIME_LENGTH];
    strftime(time_str, sizeof(time_str), "[%d/%m/%Y %H:%M:%S]", local_time);
    fprintf(f, "%s$%s$%s\n", time_str, request, response);
    fclose(f);
}

/**
 * @function forward_lookup
 * @brief Performs a DNS forward lookup (domain name ? IP address).
 *
 * @param hostname The domain name to resolve.
 * @param output Output buffer to store results.
 * @param size Size of the output buffer.
 *
 * @details
 *  - If successful, the output string starts with '+' followed by IP addresses.
 *  - If not found, the output string contains "-Not found information".
 */
void forward_lookup(const char *hostname, char *output, size_t size){
	struct addrinfo hints, *res, *p;
	int status;
	char ip_str[INET_ADDRSTRLEN];
	int first = 1;
	
	memset(&hints, 0, sizeof(hints)); 
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM; 
	
	status = getaddrinfo(hostname, NULL, &hints, &res);
	if(status != 0){
		snprintf(output, size, "-Not found information");
        return;
	} 
	
	snprintf(output, size, "+");
	for(p = res; p != NULL; p = p->ai_next){
		struct sockaddr_in  *address = (struct sockaddr_in *) p->ai_addr;
		inet_ntop(AF_INET, &address->sin_addr, ip_str, sizeof(ip_str));
		if (!first) strncat(output, " ", size - strlen(output) - 1);
        strncat(output, ip_str, size - strlen(output) - 1);
        first = 0;
	} 
	
	freeaddrinfo(res); 
}

/**
 * @function reverse_lookup
 * @brief Performs a reverse DNS lookup (IP address ? domain name).
 *
 * @param ipstr The IPv4 address to resolve.
 * @param output Output buffer to store the result.
 * @param size Size of the output buffer.
 *
 * @details
 *  - If successful, output begins with '+' followed by hostnames.
 *  - If not found, output contains "-Not found information".
 */
void reverse_lookup(const char *ipstr, char *output, size_t size) {
    struct in_addr addr;
    
    if (inet_pton(AF_INET, ipstr, &addr) <= 0) {
        snprintf(output, size, "-Not found information");
        return;
    }

    struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    if (he == NULL) {
        snprintf(output, size, "-Not found information");
        return;
    }

    snprintf(output, size, "+%s", he->h_name);
    
    for (int i = 0; he->h_aliases[i] != NULL; i++) {
        strncat(output, " ", size - strlen(output) - 1);
        strncat(output, he->h_aliases[i], size - strlen(output) - 1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
        exit(1);
    }

    int port = validate_port(argv[1]);
    if (port == -1) {
        fprintf(stderr, "Error: Invalid port number (must be 1-65535)\n");
        exit(1);
    }
    
    int server_sock;
    struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
    char buffer[BUFF_SIZE];
    char response[BUFF_SIZE];
    socklen_t sin_size = sizeof(client_addr);

    if ((server_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    memset(&(server_addr.sin_zero), 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind() error");
        close(server_sock);
        exit(1);
    }

    printf("UDP Server running on port %d...\n", port);

    while (1) {
        int bytes_received = recvfrom(server_sock, buffer, BUFF_SIZE - 1, 0,
                                      (struct sockaddr *)&client_addr, &sin_size);
        if (bytes_received < 0) {
            perror("recvfrom() error");
        }

        buffer[bytes_received] = '\0';
        printf("Received: %s\n", buffer);
        
        if (is_valid_ipv4(buffer)) {
	        reverse_lookup(buffer, response, sizeof(response));
	    }
		else if (is_number_or_dot(buffer)) {
		    strcpy(response, "-Invalid IPv4 address: ");
		    strncat(response, buffer, sizeof(response) - strlen(response) - 1);
		}
	    else if (is_valid_domain(buffer)) {
	        forward_lookup(buffer, response, sizeof(response));
	    }
	    else {
		    strcpy(response, "-Invalid domain name: ");
		    strncat(response, buffer, sizeof(response) - strlen(response) - 1);
		}

        sendto(server_sock, response, strlen(response), 0,
               (struct sockaddr *)&client_addr, sin_size);

        log_in_file(buffer, response);
    }

    close(server_sock);
    return 0;
}

