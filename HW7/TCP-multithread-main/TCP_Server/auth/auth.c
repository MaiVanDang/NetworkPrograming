#include "../user/user.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/**
 * @brief Check if username is already logged in on another client
 * @return 1 if logged in, 0 otherwise
 */
int is_username_logged_in_nolock(const char *username, int current_sockfd,
                                 Session *sessions, int max_sessions)
{
    for (int i = 0; i < max_sessions; i++)
    {
        if (sessions[i].active)
        {
            printf(" Session[%d]: sockfd=%d, username='%s', logged_in=%d, active=%d\n",
                   i, sessions[i].sockfd, sessions[i].username, sessions[i].logged_in, sessions[i].active);

            if (sessions[i].logged_in &&
                sessions[i].sockfd != current_sockfd &&
                strcmp(sessions[i].username, username) == 0)
            {
                printf(" Username '%s' already logged in on sockfd=%d\n", username, sessions[i].sockfd);
                return 1; // Username already logged in elsewhere
            }
        }
    }

    printf(" Username '%s' not logged in elsewhere\n", username);
    return 0; // Username not logged in
}

/**
 * @brief Process USER command with session management
 * @return response code
 */
int processUSER(char *username, int *logged_in, int *current_index,
                User users[], int user_count,
                int sockfd, Session *sessions, int max_sessions,
                pthread_mutex_t *session_mutex)
{
    if (username == NULL || strlen(username) == 0)
        return 300; // wrong format

    printf("USER: username='%s', sockfd=%d, logged_in=%d\n", username, sockfd, *logged_in);

    pthread_mutex_lock(session_mutex);

    // Check if already logged in on this session
    if (*logged_in)
    {
        printf(" User already logged in on this session\n");
        pthread_mutex_unlock(session_mutex);
        return 213; // already logged in
    }

    int idx = findUser(username, users, user_count);

    if (idx == -1)
    {
        printf(" User '%s' not found\n", username);
        pthread_mutex_unlock(session_mutex);
        return 212; // user not exist
    }

    if (users[idx].status == 0)
    {
        printf("User '%s' is blocked\n", username);
        pthread_mutex_unlock(session_mutex);
        return 211; // user blocked
    }

    // Check if username is already logged in on another client (NO LOCK inside)
    if (is_username_logged_in_nolock(username, sockfd, sessions, max_sessions))
    {
        printf("User '%s' already logged in on another client\n", username);
        pthread_mutex_unlock(session_mutex);
        return 214; // Account already logged in on another client
    }

    // Success: mark as logged in and update session
    *logged_in = 1;
    *current_index = idx;

    // Update session
    for (int i = 0; i < max_sessions; i++)
    {
        if (sessions[i].active && sessions[i].sockfd == sockfd)
        {
            strncpy(sessions[i].username, username, MAX_NAME - 1);
            sessions[i].username[MAX_NAME - 1] = '\0';
            sessions[i].logged_in = 1;
            printf("Session[%d] updated: username='%s', logged_in=1\n", i, username);
            break;
        }
    }

    pthread_mutex_unlock(session_mutex);
    printf("USER: Login successful for '%s'\n", username);
    return 110; // login success
}

/**
 * @brief Process POST command
 */
int processPOST(char *content, int logged_in)
{
    (void)content; // Suppress unused parameter warning

    if (!logged_in)
        return 221; // not logged in

    // Here you can add logic to save the post to a file or database
    return 120; // post successful
}

/**
 * @brief Process BYE command - now with session cleanup
 */
int processBYE(int *logged_in, int sockfd, Session *sessions, int max_sessions, pthread_mutex_t *session_mutex)
{
    if (!*logged_in)
        return 221; // not logged in

    printf("BYE: sockfd=%d\n", sockfd);

    // Clean up session when logging out
    pthread_mutex_lock(session_mutex);

    *logged_in = 0;

    for (int i = 0; i < max_sessions; i++)
    {
        if (sessions[i].active && sessions[i].sockfd == sockfd)
        {
            printf("Clearing session[%d]: username='%s'\n", i, sessions[i].username);
            memset(sessions[i].username, 0, MAX_NAME);
            sessions[i].logged_in = 0;
            break;
        }
    }

    pthread_mutex_unlock(session_mutex);

    return 130; // logout successful
}