#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <pthread.h>
#include "../user/user.h"
#include "../auth/auth.h"

void handle_protocol_with_session(int sockfd, User users[], int user_count, void *sessions, int max_sessions, pthread_mutex_t *session_mutex);

#endif