#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol/protocol.h"

#define BUFF_SIZE 1024

/**
 * @brief Main function for TCP client
 * @param argc Argument count
 * @param argv Argument vector
 * -description
 * Connects to the server specified by command line arguments.
 * Displays a menu for user interaction to login, post messages, logout, or exit.
 * Uses protocol functions to communicate with the server.
 */

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <Server_IP> <Server_Port>\n", argv[0]);
        exit(1);
    }

    int clientfd;
    struct sockaddr_in servAddr;

    char *SERVER_IP = argv[1];
    int SERVER_PORT = atoi(argv[2]);

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr);

    if (connect(clientfd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
    {
        perror("Connect failed");
        exit(1);
    }

    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    // first welcome message
    receive_line(clientfd);

    while (1)
    {
        printf("\n====== MENU ======\n");
        printf("1. Login\n");
        printf("2. Post Message\n");
        printf("3. Logout\n");
        printf("4. Exit\n");
        printf("Enter choice: ");

        int choice;
        scanf("%d", &choice);
        getchar(); // clear newline in buffer

        char text[600];

        switch (choice)
        {
        case 1:
        {
            char username[100];
            printf("Enter username: ");
            fgets(username, sizeof(username), stdin);
            username[strcspn(username, "\n")] = '\0';

            snprintf(text, sizeof(text), "USER %s", username);
            send_and_receive(clientfd, text);
            break;
        }

        case 2:
        {
            char content[512];
            printf("Enter message: ");
            fgets(content, sizeof(content), stdin);
            content[strcspn(content, "\n")] = '\0';

            snprintf(text, sizeof(text), "POST %s", content);
            send_and_receive(clientfd, text);
            break;
        }

        case 3:
            send_and_receive(clientfd, "BYE");
            break;

        case 4:
            printf("Client exit.\n");
            close(clientfd);
            return 0;

        default:
            printf("Invalid choice.\n");
        }
    }
}
