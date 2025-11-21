#define _POSIX_C_SOURCE 200112L

#include "validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#define MAX_LENGTH_IPv4 17

/**
 * @function validate_port
 * @brief Validates and converts port string to integer
 * @param port_str Port number as string
 * @return Port number if valid, -1 otherwise
 */
int validate_port(const char *port_str) {
    if (port_str == NULL || *port_str == '\0') {
        return -1;
    }
    
    for (int i = 0; port_str[i]; i++) {
        if (!isdigit((unsigned char)port_str[i])) {
            return -1;
        }
    }
    
    char *endptr;
    long port_long = strtol(port_str, &endptr, 10);
    
    if (port_long < 1 || port_long > 65535) {
        return -1;
    }
    
    return (int)port_long;
}

/**
 * @function is_ip_address
 * @brief Checks if the given input string is a valid IPv4 address.
 *
 * @param input Input string to be validated.
 * @return 1 if valid IPv4 address, 0 otherwise.
 */
int is_ip_address(const char *input) {
    struct sockaddr_in addr;
    return inet_pton(AF_INET, input, &(addr.sin_addr));
}

/**
 * @function is_valid_ipv4
 * @brief Verifies whether a string follows the IPv4 format a.b.c.d.
 *
 * @param ip Pointer to the IP address string.
 * @return 1 if valid IPv4 format, 0 otherwise.
 */
int is_valid_ipv4(const char *ip) {
    int num, segments = 0;
    char *ptr;
    char str[MAX_LENGTH_IPv4];

    if (ip == NULL || strlen(ip) == 0) return 0;
    if (strlen(ip) >= sizeof(str)) return 0;
    
    if (ip[0] == '.' || ip[strlen(ip)-1] == '.') return 0;
    
    for (size_t i = 0; i < strlen(ip) - 1; i++) {
        if (ip[i] == '.' && ip[i+1] == '.') return 0;
    }

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
 * @brief Checks if a string contains only numeric digits and dots.
 *
 * @param s Input string to be checked.
 * @return 1 if the string contains only digits and dots, 0 otherwise.
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
 * @brief Validates an individual DNS label (a component between dots).
 *
 * @param label Pointer to the label string.
 * @param len Length of the label.
 * @return 1 if valid DNS label, 0 otherwise.
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
 * @brief Checks if a string is a syntactically valid domain name.
 *
 * @param domain Pointer to the domain name string.
 * @return 1 if valid domain name, 0 otherwise.
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
