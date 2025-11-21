#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFF_SIZE 1024

/**
 * @brief Receive a line from the server and print it
 * @param sockfd Socket file descriptor
 * -description
 * Receives data from the server until a newline character is encountered or the buffer limit is reached.
 * The received data is then printed to the standard output.
 */

void receive_line(int sockfd)
{
    char buff[BUFF_SIZE];
    int len = recv(sockfd, buff, BUFF_SIZE - 1, 0);

    if (len <= 0)
        return;

    buff[len] = '\0';
    printf("Server response: %s", buff);
}

/**
 * @brief Send a message to the server and receive a response
 * @param sockfd Socket file descriptor
 * @param msg Message to send
 * -description
 * Sends the specified message to the server followed by a newline character.
 * After sending the message, it calls receive_line to get and print the server's response.
 */
void send_and_receive(int sockfd, const char *msg)
{
    send(sockfd, msg, strlen(msg), 0);
    send(sockfd, "\n", 1, 0); // ensure full line

    receive_line(sockfd);
}
