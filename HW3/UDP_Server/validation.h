#ifndef DNS_UTILS_H
#define DNS_UTILS_H

#include <stddef.h>  

int validate_port(const char *port_str);
int is_ip_address(const char *input);
int is_valid_ipv4(const char *ip);
int is_number_or_dot(const char *s);
int is_valid_label(const char *label, size_t len);
int is_valid_domain(const char *domain);

#endif
