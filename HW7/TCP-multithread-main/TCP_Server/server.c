#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "protocol/protocol.h"
#include "user/user.h"
#include "auth/auth.h"

#define BACKLOG 20
#define MAX_SESSIONS 100

Session sessions[MAX_SESSIONS];
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
int session_count = 0;

User users[MAX_USERS];
int user_count = 0;

/**
 * @brief Find session index by socket descriptor
 * @return Session index if found, -1 otherwise
 */
int find_session_by_sockfd(int sockfd)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active && sessions[i].sockfd == sockfd)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Check if username is already logged in on another client
 * @return Session index if found, -1 otherwise
 */
int find_logged_username(const char *username, int exclude_sockfd)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active &&
            sessions[i].logged_in &&
            sessions[i].sockfd != exclude_sockfd &&
            strcmp(sessions[i].username, username) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Add a new session
 * @return Session index if added successfully, -1 if full
 * - description
 *   This function attempts to add a new session for a client connection.
 *  It searches for an inactive session slot, initializes it with the
 *  client's socket information, and marks it as active.
 */
int add_session(int sockfd, struct sockaddr_in addr)
{
    pthread_mutex_lock(&session_mutex);

    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!sessions[i].active)
        {
            slot = i;
            sessions[i].sockfd = sockfd;
            sessions[i].addr = addr;
            sessions[i].active = 1;
            sessions[i].logged_in = 0;
            memset(sessions[i].username, 0, MAX_NAME);
            session_count++;

            printf("[DEBUG] add_session: session[%d] created for sockfd=%d, active=%d, session_count=%d\n",
                   i, sockfd, sessions[i].active, session_count);
            break;
        }
    }

    pthread_mutex_unlock(&session_mutex);
    return slot;
}

/**
 * @brief Remove session
 * - description
 *   This function removes a session associated with the given socket descriptor.
 *   It marks the session as inactive and clears its data.
 */
void remove_session(int sockfd)
{
    pthread_mutex_lock(&session_mutex);

    int idx = find_session_by_sockfd(sockfd);
    if (idx != -1)
    {
        sessions[idx].active = 0;
        sessions[idx].logged_in = 0;
        sessions[idx].username[0] = '\0';
        printf("[Session] Removed session for socket %d\n", sockfd);
    }

    pthread_mutex_unlock(&session_mutex);
}

/**
 * @brief Update session login status
 * - description
 *  This function updates the login status of a session identified by the
 * given socket descriptor. It sets the logged_in flag and updates the username
 * if provided.
 */
void update_session_login(int sockfd, const char *username, int logged_in)
{
    pthread_mutex_lock(&session_mutex);

    int idx = find_session_by_sockfd(sockfd);
    if (idx != -1)
    {
        sessions[idx].logged_in = logged_in;
        if (logged_in && username)
        {
            strncpy(sessions[idx].username, username, MAX_NAME - 1);
            sessions[idx].username[MAX_NAME - 1] = '\0';
        }
        else
        {
            sessions[idx].username[0] = '\0';
        }
    }

    pthread_mutex_unlock(&session_mutex);
}

/**
 * @brief Client handler thread
 * - description
 *   This function handles communication with a connected client. It manages the session,
 *   processes commands according to the protocol, and ensures proper login/logout handling.
 */
void *client_handler(void *arg)
{
    int connfd = *((int *)arg);
    free(arg);

    pthread_detach(pthread_self());

    // Get client address for logging
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(connfd, (struct sockaddr *)&client_addr, &addr_len);

    printf("[Server] Client connected: %s:%d (sockfd=%d)\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port),
           connfd);

    // Add session before handling protocol
    int session_idx = add_session(connfd, client_addr);
    if (session_idx == -1)
    {
        printf("[ERROR] Max sessions reached, rejecting client sockfd=%d\n", connfd);
        send(connfd, "500 Server full\n", 16, 0);
        close(connfd);
        return NULL;
    }

    printf("[DEBUG] Session created: session[%d] for sockfd=%d\n", session_idx, connfd);

    // Handle protocol
    handle_protocol_with_session(connfd, users, user_count,
                                 sessions, MAX_SESSIONS, &session_mutex);

    // Remove session after client disconnects
    remove_session(connfd);
    close(connfd);

    printf("[Server] Client disconnected: sockfd=%d\n", connfd);
    return NULL;
}

/**
 * @brief TCP Server Application with Multi-threading
 * - description
 *   This is the main function for the TCP server application. It initializes
 *   the server, loads user accounts, and listens for incoming client connections.
 *   For each client connection, it spawns a new thread to handle communication.
 */
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <Server_Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int PORT = atoi(argv[1]);
    if (PORT <= 0)
    {
        printf("Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    // Load user accounts once
    user_count = loadAccounts("TCP_Server/account.txt", users, MAX_USERS);
    if (user_count == 0)
    {
        printf("Warning: No accounts loaded. Check account.txt file.\n");
    }

    // Initialize sessions
    memset(sessions, 0, sizeof(sessions));

    int listen_sock, *conn_sock;
    struct sockaddr_in server_addr, client_addr;
    pthread_t tid;
    socklen_t sin_size;

    // Create socket
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket() error");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt() error");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind() error");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, BACKLOG) == -1)
    {
        perror("listen() error");
        exit(EXIT_FAILURE);
    }

    printf("Server started at port %d...\n", PORT);
    printf("Waiting for connections...\n");

    while (1)
    {
        sin_size = sizeof(client_addr);
        conn_sock = malloc(sizeof(int));
        if (conn_sock == NULL)
        {
            perror("malloc() error");
            continue;
        }

        *conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &sin_size);
        if (*conn_sock == -1)
        {
            if (errno == EINTR)
                continue;
            perror("accept() error");
            free(conn_sock);
            continue;
        }

        // Create new thread for each client
        if (pthread_create(&tid, NULL, client_handler, conn_sock) != 0)
        {
            perror("pthread_create() error");
            close(*conn_sock);
            free(conn_sock);
            continue;
        }
    }

    close(listen_sock);
    pthread_mutex_destroy(&session_mutex);
    return 0;
}