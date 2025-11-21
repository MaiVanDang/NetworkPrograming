#ifndef AUTH_H
#define AUTH_H

#include "../user/user.h"
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>

// Forward declaration
typedef struct Session Session;

// Session structure definition
struct Session
{
    int sockfd;
    char username[MAX_NAME];
    int logged_in;
    int active;
    struct sockaddr_in addr;
    pthread_t tid;
};

int processUSER(char *username, int *logged_in, int *current_index,
                User users[], int user_count,
                int sockfd, Session *sessions, int max_sessions,
                pthread_mutex_t *session_mutex);

int processPOST(char *content, int logged_in);

int processBYE(int *logged_in, int sockfd, Session *sessions, int max_sessions, pthread_mutex_t *session_mutex);

#endif