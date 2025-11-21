#ifndef PROTOCOL_H
#define PROTOCOL_H

#define BUFF_SIZE 1024

void receive_line(int sockfd);

void send_and_receive(int sockfd, const char *msg);

#endif
