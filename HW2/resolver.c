#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>

#define MAX_LENGTH_IPv4 17
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025 
#endif

/**
 * @function is_valid_ipv4
 * @brief Check if a string is a valid IPv4 address in the form a.b.c.d.
 *
 * @param ip: A pointer to the input string.
 *
 * @return: 1 if the string is a valid IPv4 address. 
 *			0 otherwise.
 */
int is_valid_ipv4(const char *ip) {
    int num, segments = 0;
    char *ptr;
    char str[MAX_LENGTH_IPv4];

    if (ip == NULL || strlen(ip) == 0) return 0;
    if (strlen(ip) >= sizeof(str)) return 0;

    strcpy(str, ip);
    ptr = strtok(str, ".");

    while (ptr) {
        if (strlen(ptr) == 0) return 0;
        
        for (int i = 0; ptr[i]; i++) {
            if (!isdigit((unsigned char)ptr[i]))
                return 0;
        }
        
        num = atoi(ptr);
        if (num < 0 || num > 255) return 0;
        
        segments++;
        ptr = strtok(NULL, ".");
    }
    return segments == 4;
}

/**
 * @function is_number_or_dot
 * @brief Check if a string contains only digits and dots.
 *
 * @param s: A pointer to the input string.
 *
 * @return 1 if the string only contains digits and dots, 0 otherwise.
 */
int is_number_or_dot(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (!(isdigit((unsigned char)s[i]) || s[i] == '.'))
            return 0;
    }
    return 1;
}

/**
 * @function is_valid_label
 * @brief Check if a DNS label is valid.
 *
 * @param label: A pointer to the label string.
 * @param len: Length of the label.
 *
 * @return: 1 if valid. 
 *          0 otherwise.
 */
int is_valid_label(const char *label, size_t len) {
    if (len == 0 || len > 63) return 0;
    
    if (label[0] == '-' || label[len-1] == '-') return 0;
    
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)label[i]) && label[i] != '-') {
            return 0;
        }
    }
    
    return 1;
}

/**
 * @function is_valid_domain
 * @brief Check if a string is a valid domain name.
 *
 * @param domain: A pointer to the domain string.
 *
 * @return: 1 if the string is a valid domain name.
 *          0 otherwise.
 */
int is_valid_domain(const char *domain) {
    if (domain == NULL) return 0;
    
    size_t len = strlen(domain);
    
    if (len == 0 || len > 253) return 0;
    
    if (domain[0] == '.' || domain[len-1] == '.') return 0;
    
    if (strchr(domain, '.') == NULL) return 0;
    
    for (size_t i = 0; i < len - 1; i++) {
        if (domain[i] == '.' && domain[i+1] == '.') {
            return 0;
        }
    }
    
    char temp[256];
    strncpy(temp, domain, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *label = strtok(temp, ".");
    char *last_label = NULL;
    int label_count = 0;
    
    while (label != NULL) {
        if (!is_valid_label(label, strlen(label))) {
            return 0;
        }
        last_label = label;
        label_count++;
        label = strtok(NULL, ".");
    }
    
    if (label_count < 2) return 0;
    
    if (last_label != NULL && strlen(last_label) < 2) return 0;
    
    if (last_label != NULL) {
        int all_numeric = 1;
        for (size_t i = 0; last_label[i]; i++) {
            if (!isdigit((unsigned char)last_label[i])) {
                all_numeric = 0;
                break;
            }
        }
        if (all_numeric) return 0;
    }
    
    return 1;
}

/**
 * @function forward_lookup
 * @brief Perform DNS forward lookup (domain name ? IP addresses).
 *
 * @param hostname: A pointer to the string containing the domain name.
 *
 * @return void. Print result to stdout.
 *         "Result:\n<list of IP>" if found,
 *         "Not found information" otherwise.
 */
void forward_lookup(const char *hostname){
	struct addrinfo hints, *res, *p;
	int status;
	char ipStr[INET_ADDRSTRLEN];
	
	memset(&hints, 0, sizeof(hints)); 
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM; 
	
	status = getaddrinfo(hostname, NULL, &hints, &res);
	if(status != 0){
		printf("Not found information\n");
        return;
	} 
	
	printf("Result:\n");
	for(p = res; p != NULL; p = p->ai_next){
		struct sockaddr_in  *address = (struct sockaddr_in *) p->ai_addr;
		inet_ntop(AF_INET, &address->sin_addr, ipStr, sizeof(ipStr));
		printf("%s\n", ipStr); 
	} 
	
	freeaddrinfo(res); 
}

/**
 * @function reverse_lookup
 * @brief Perform DNS reverse lookup (IPv4 address ? domain names).
 *
 * @param ipstr: A pointer to the string containing the IPv4 address.
 *
 * @return void. Print result to stdout.
 *         "Result:\n<hostname(s)>" if found,
 *         "Not found information" otherwise.
 */
void reverse_lookup(const char *ipstr) {
    struct in_addr addr;
    
    if (inet_pton(AF_INET, ipstr, &addr) <= 0) {
        printf("Not found information\n");
        return;
    }

    struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    if (he == NULL) {
        printf("Not found information\n");
        return;
    }

    printf("Result:\n");
    printf("%s\n", he->h_name);

    for (char **alias = he->h_aliases; *alias != NULL; alias++) {
        printf("%s\n", *alias);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname|IPv4>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *parameter = argv[1];
    
    if (is_valid_ipv4(parameter)) {
        reverse_lookup(parameter);
    }
    else if (is_number_or_dot(parameter)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", parameter);
    }
    else if (is_valid_domain(parameter)) {
        forward_lookup(parameter);
    }
    else {
        fprintf(stderr, "Invalid domain name: %s\n", parameter);
    }

    return 0;
}

