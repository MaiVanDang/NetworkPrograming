#include "protocol.h"
#include "../user/user.h"
#include "../auth/auth.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

#define MAX_BUFFER 4096

/**
 * @brief Handle protocol communication with session management
 */
void handle_protocol_with_session(int sockfd, User users[], int user_count,
                                  void *sessions_void, int max_sessions,
                                  pthread_mutex_t *session_mutex)
{
    // Cast void* to Session*
    Session *sessions = (Session *)sessions_void;

    int logged_in = 0;
    int current_user_index = -1;

    char buffer[MAX_BUFFER] = {0};
    int buffer_len = 0;
    int len;

    // Send welcome message per protocol
    send(sockfd, "100 Welcome to server\n", strlen("100 Welcome to server\n"), 0);

    while ((len = recv(sockfd, buffer + buffer_len, MAX_BUFFER - buffer_len - 1, 0)) > 0)
    {
        buffer_len += len;
        buffer[buffer_len] = '\0';

        char *newline;

        // Process full command lines
        while ((newline = strchr(buffer, '\n')) != NULL)
        {
            *newline = '\0'; // terminate one command

            // Remove \r if present (for Windows clients)
            int cmd_len = strlen(buffer);
            if (cmd_len > 0 && buffer[cmd_len - 1] == '\r')
            {
                buffer[cmd_len - 1] = '\0';
            }

            printf("[Client %d Command] %s\n", sockfd, buffer);

            char cmd[10] = {0};
            char arg[512] = {0};
            char res[128];

            // Safe parsing
            sscanf(buffer, "%9s %511[^\n]", cmd, arg);

            if (strcmp(cmd, "USER") == 0)
            {
                int code = processUSER(arg, &logged_in, &current_user_index,
                                       users, user_count,
                                       sockfd, sessions, max_sessions, session_mutex);

                if (code == 110)
                {
                    sprintf(res, "110 Login successful\n");
                }
                else if (code == 211)
                    sprintf(res, "211 Account is blocked\n");
                else if (code == 212)
                    sprintf(res, "212 Account does not exist\n");
                else if (code == 213)
                    sprintf(res, "213 Already logged in\n");
                else if (code == 214)
                    sprintf(res, "214 Account is already logged in on another client\n");
                else
                    sprintf(res, "300 Undefined command\n");

                send(sockfd, res, strlen(res), 0);
            }
            else if (strcmp(cmd, "POST") == 0)
            {
                int code = processPOST(arg, logged_in);

                if (code == 120)
                {
                    sprintf(res, "120 Post message successful\n");
                    // Optionally log the post
                    if (current_user_index >= 0)
                    {
                        printf("[POST] User '%s' posted: %s\n",
                               users[current_user_index].name, arg);
                    }
                }
                else if (code == 221)
                    sprintf(res, "221 You must login first\n");
                else
                    sprintf(res, "300 Undefined command\n");

                send(sockfd, res, strlen(res), 0);
            }
            else if (strcmp(cmd, "BYE") == 0)
            {
                int code = processBYE(&logged_in, sockfd, sessions, max_sessions, session_mutex);

                if (code == 130)
                {
                    sprintf(res, "130 Logout successful\n");
                    current_user_index = -1;
                }
                else if (code == 221)
                    sprintf(res, "221 You must login first\n");
                else
                    sprintf(res, "300 Undefined command\n");

                send(sockfd, res, strlen(res), 0);
            }
            else
            {
                sprintf(res, "300 Undefined command\n");
                send(sockfd, res, strlen(res), 0);
            }

            // Shift remaining buffer forward (stream handling)
            int consumed = (newline - buffer) + 1;
            memmove(buffer, buffer + consumed, buffer_len - consumed);
            buffer_len -= consumed;
            buffer[buffer_len] = '\0';
        }
    }

    printf("[Client %d] Disconnected.\n", sockfd);
}
