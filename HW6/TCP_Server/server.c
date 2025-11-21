#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include<netdb.h>
#include <pthread.h>
#include <errno.h>

#define BACKLOG 20
#define BUFF_SIZE 4096
#define ACCOUNT_FILE "account.txt"
#define MAX_SESSIONS 1000

/**
 * @struct Account
 * @brief Structure to store account information
 */
typedef struct {
    char* username;
    int status;
} Account;

/**
 * @struct Session
 * @brief Structure to store client session state.
 */
typedef struct {
    int logged_in;
    char* username;
    int sockfd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int active;
} Session;

/**
 * @struct ClientConn
 * @brief Structure to store client connection with leftover buffer
 */
typedef struct {
    int sockfd;
    char leftover[BUFF_SIZE];
    int leftover_len;
} ClientConn;

/**
 * @struct ThreadArg
 * @brief Arguments passed to client handler thread
 */
typedef struct {
    int sockfd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
} ThreadArg;

Session* sessions[MAX_SESSIONS];
int session_count = 0;
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @function load_account
 * @brief Load account information from file.
 *
 * @param inputUsername The username to search for in the account file.
 * @param acc Pointer to Account structure to store the found account data.
 * @return 1 if account found, 0 if not found, -1 on error.
 *
 * @details
 *  - Opens ACCOUNT_FILE ("account.txt") for reading.
 *  - Reads file line by line using getline().
 *  - Each line format: "username status" (space or tab separated).
 *  - Compares username with inputUsername (case-sensitive).
 *  - If found, allocates memory for acc->username and copies data.
 *  - Status: 1 = active, 0 = locked.
 *  - Caller must free acc->username after use if return value is 1.
 */
int load_account(const char* inputUsername, Account* acc){
    FILE* f = fopen(ACCOUNT_FILE, "r");
    if(!f){
        perror("Cannot open account file");
        return -1;
    }
    
    char* line = NULL;
    size_t lineSize = 0;
    int foundUser = 0;
    
    while (getline(&line, &lineSize, f) > 0){
        char* username = strtok(line, " \t\n");
        char* statusStr = strtok(NULL, " \t\n");
        
        if (username && statusStr) {
            int status = atoi(statusStr);
            if (strcmp(username, inputUsername) == 0) {
                size_t usernameLen = strlen(inputUsername);
                acc->username = malloc(usernameLen + 1);
                if (!acc->username) {
                    fclose(f);
                    free(line);
                    return -1;
                }
                strcpy(acc->username, inputUsername);
                acc->status = status;
                foundUser = 1;
                break;
            }
        }
    }
    free(line);
    fclose(f);
    return foundUser; 
}

/**
 * @function recv_until_delim
 * @brief Receive data from socket until delimiter is encountered.
 *
 * @param client Pointer to ClientConn structure containing socket and leftover buffer.
 * @param out_buf Buffer to store the received message (without delimiter).
 * @param max_len Maximum size of out_buf.
 * @param delim Delimiter string to search for (e.g., "\r\n").
 * @return Number of bytes in the message (excluding delimiter), 0 if connection closed, -1 on error.
 *
 * @details
 *  - First checks if there's leftover data from previous recv() calls.
 *  - Continuously receives data from socket until delimiter is found.
 *  - Extracts one complete message and stores remainder in client->leftover.
 *  - Null-terminates the message in out_buf.
 *  - Handles buffer overflow by limiting bytes received to max_len.
 *  - Supports pipelined messages (multiple messages in one TCP packet).
 */
int recv_until_delim(ClientConn *client, char *out_buf, int max_len, const char *delim) {
    char temp_buf[BUFF_SIZE];
    int total_len = 0;

    if (client->leftover_len > 0) {
        memcpy(out_buf, client->leftover, client->leftover_len);
        total_len = client->leftover_len;
        client->leftover_len = 0;
    }

    while (1) {
        char *delim_pos = strstr(out_buf, delim);
        if (delim_pos) {
            int msg_len = delim_pos - out_buf; 
            out_buf[msg_len] = '\0';

            int remain_len = total_len - (msg_len + strlen(delim));
            if (remain_len > 0) {
                memcpy(client->leftover, delim_pos + strlen(delim), remain_len);
                client->leftover_len = remain_len;
            }
            return msg_len;
        }

        int bytes_recv = recv(client->sockfd, temp_buf, sizeof(temp_buf), 0);
        if (bytes_recv <= 0)
            return bytes_recv; 

        if (total_len + bytes_recv >= max_len)
            bytes_recv = max_len - total_len - 1;

        memcpy(out_buf + total_len, temp_buf, bytes_recv);
        total_len += bytes_recv;
        out_buf[total_len] = '\0';
    }
}

/**
 * @function send_response
 * @brief Send response message to client with format: "CODE\r\n".
 *
 * @param sockfd Socket descriptor to send data to.
 * @param code Response code string (e.g., "100", "110", "211").
 *
 * @details
 *  - Formats response as "CODE\r\n".
 *  - Sends complete response through socket using send().
 *  - Prints error message if send() fails.
 *  - Does not close connection on error.
 */
void send_response(int sockfd, const char* code){
    char response[BUFF_SIZE];
    snprintf(response, BUFF_SIZE, "%s\r\n", code);
    if(send(sockfd, response, strlen(response), 0) == -1){
        perror("send() error: ");
        return;     
    }
}

/**
 * @function is_account_logged_in
 * @brief Check if an account is already logged in on another client.
 *
 * @param username Username to check.
 * @param current_sockfd Socket descriptor of the current session (to exclude self).
 * @return 1 if account is logged in elsewhere, 0 if not.
 *
 * @details
 *  - Locks session_mutex before accessing global sessions array.
 *  - Iterates through all active sessions.
 *  - Checks if any session (except current one) has the same username and is logged in.
 *  - Thread-safe: uses mutex to prevent race conditions.
 *  - Returns immediately upon finding a match.
 */
int is_account_logged_in(const char* username, int current_sockfd){
    pthread_mutex_lock(&session_mutex);
    for(int i = 0; i < session_count; i++){
        if(sessions[i] != NULL && 
           sessions[i]->active && 
           sessions[i]->logged_in && 
           sessions[i]->sockfd != current_sockfd &&
           strcmp(sessions[i]->username, username) == 0){
            pthread_mutex_unlock(&session_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

/**
 * @function process_user_command
 * @brief Process USER command for user login.
 *
 * @param session Pointer to Session structure tracking login state.
 * @param arg Username argument from USER command.
 * @param sockfd Socket descriptor to send response to.
 *
 * @details
 *  - Validates that user is not already logged in (sends 213 if already logged in).
 *  - Checks that username is not empty (sends 300 if empty).
 *  - Calls load_account() to verify username exists.
 *  - If account found and status=0: sends 211 (account locked).
 *  - If account found and status=1: 
 *    + Checks if account is already logged in on another client (sends 214).
 *    + If not logged in elsewhere: logs in user, sends 110.
 *  - If account not found: sends 212.
 *  - On internal error: sends 500.
 *  - Updates session->logged_in and session->username on success.
 *  - Thread-safe: uses mutex when checking logged-in accounts.
 */
void process_user_command(Session* session, const char* arg, int sockfd){
    if(session->logged_in){
        send_response(sockfd, "213"); //Session already logged in
    }
    else if(strlen(arg) == 0){
        send_response(sockfd, "300"); //Unknown message type
    }
    else{
        Account acc = {NULL, 0};
        int result = load_account(arg, &acc);
                
        if(result == 1){
             if(acc.status == 0){
	            send_response(sockfd, "211");//Account is locked
	            free(acc.username);
	            return;
	        }
	        
	        if(is_account_logged_in(arg, sockfd)){
	            send_response(sockfd, "214");//Account already logged in on another client
	            free(acc.username);
	            return;
	        }
	        
	        session->logged_in = 1;
	        free(session->username);
	        session->username = malloc(strlen(acc.username) + 1);
	        if (!session->username) {
	            send_response(sockfd, "500");//Internal server error
	            free(acc.username);
	            return;
	        }
	        strcpy(session->username, acc.username);
	        send_response(sockfd, "110");//Login successful
	        
	        free(acc.username);
        }
        else if(result == 0){
            send_response(sockfd, "212");//Account does not exist
        }
        else{
            send_response(sockfd, "500");//Internal server error
        }
    }
}

/**
 * @function process_post_command
 * @brief Process POST command for posting article.
 *
 * @param session Pointer to Session structure tracking login state.
 * @param sockfd Socket descriptor to send response to.
 *
 * @details
 *  - Checks if user is logged in (sends 221 if not logged in).
 *  - If logged in, prints article to server console with client info.
 *  - Sends 120 response code (post successful).
 *  - Does not validate article content or length.
 */
void process_post_command(Session* session, int sockfd){
    if(!session->logged_in){
        send_response(sockfd, "221");//Not logged in
    }
    else{
        send_response(sockfd, "120");//Post successful
    }
}

/**
 * @function process_bye_command
 * @brief Process BYE command for user logout.
 *
 * @param session Pointer to Session structure tracking login state.
 * @param sockfd Socket descriptor to send response to.
 *
 * @details
 *  - Checks if user is logged in (sends 221 if not logged in).
 *  - If logged in, sends 130 response code (logout successful).
 *  - Resets session->logged_in to 0.
 *  - Frees and reallocates session->username to empty string.
 */
void process_bye_command(Session* session, int sockfd){
    if(!session->logged_in){
        send_response(sockfd, "221");//Not logged in
    }
    else{
        send_response(sockfd, "130");//Logout successful
        session->logged_in = 0;
        free(session->username);
        session->username = malloc(1);
        if (session->username) strcpy(session->username, "");
    }
}

/**
 * @function handle_client
 * @brief Handle client connection and process commands in a separate thread.
 *
 * @param arg Pointer to ThreadArg structure containing socket and client info.
 * @return NULL when thread completes.
 *
 * @details
 *  - Extracts connection info from ThreadArg and frees it.
 *  - Initializes ClientConn and Session structures.
 *  - Registers session in global sessions array (thread-safe with mutex).
 *  - Sends initial 100 response code (connection successful).
 *  - Enters main loop to receive and process commands.
 *  - Uses recv_until_delim() to receive messages with "\r\n" delimiter.
 *  - Parses command using sscanf() to extract command name and arguments.
 *  - Supports commands: USER, POST, BYE.
 *  - Sends 300 for unknown commands.
 *  - Breaks loop when client disconnects (recv returns <= 0).
 *  - Marks session as inactive and removes from global list (thread-safe).
 *  - Frees session->username and closes socket before returning.
 *  - Thread automatically terminates after client disconnect.
 */
void* handle_client(void* arg){
	
	ThreadArg* thread_arg = (ThreadArg*)arg;
    int sockfd = thread_arg->sockfd;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(client_ip, thread_arg->client_ip);
    int client_port = thread_arg->client_port;
    free(thread_arg);
    
    char buff[BUFF_SIZE];
    int received_bytes;
    ClientConn client;
    
    client.sockfd = sockfd;
    client.leftover_len = 0;
    
    Session* session = malloc(sizeof(Session));
    if (!session) {
        perror("malloc() error: ");
        close(sockfd);
        return NULL;
    }
    
    client.sockfd = sockfd;
    client.leftover_len = 0;
    session->logged_in = 0;
    strcpy(session->client_ip, client_ip);
    session->client_port = client_port;
    session->active = 1;
    session->username = malloc(1);
    
    pthread_mutex_lock(&session_mutex);
    if(session_count < MAX_SESSIONS){
        sessions[session_count++] = session;
    } else {
        pthread_mutex_unlock(&session_mutex);
        free(session->username);
        free(session);
        close(sockfd);
        return NULL;
    }
    pthread_mutex_unlock(&session_mutex);
    
    send_response(sockfd, "100");//Connection successful
    
    while(1){
        received_bytes = recv_until_delim(&client, buff, BUFF_SIZE, "\r\n");
        if(received_bytes <= 0){
            printf("Client [%s:%d] disconnected\n", client_ip, client_port);
            break;
        }
        
        printf("Received: [%s:%d] %s\n", client_ip, client_port, buff);
        
        char cmd[10];
        char arg[BUFF_SIZE];
        memset(cmd, 0, sizeof(cmd));
        memset(arg, 0, sizeof(arg));
        
        if(sscanf(buff, "%9s %[^\r\n]", cmd, arg) < 1){
            send_response(sockfd, "300");//Unknown message type
            continue;
        }
        
        if (strcmp(cmd, "USER") == 0)
            process_user_command(session, arg, sockfd);
        else if (strcmp(cmd, "POST") == 0)
            process_post_command(session, sockfd);
        else if (strcmp(cmd, "BYE") == 0)
            process_bye_command(session, sockfd);
        else
            send_response(sockfd, "300");//Unknown message type
    }
    
    pthread_mutex_lock(&session_mutex);
    session->active = 0;
    for(int i = 0; i < session_count; i++){
        if(sessions[i] == session){
            for(int j = i; j < session_count - 1; j++){
                sessions[j] = sessions[j + 1];
            }
            session_count--;
            break;
        }
    }
    pthread_mutex_unlock(&session_mutex);
    
    free(session->username);
    free(session);
    close(sockfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: ./server Port_Number\n");
        return 1;
    }

    int port = atoi(argv[1]);
    
    int listen_sock, conn_sock;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    pthread_t tid;
    socklen_t sin_size;

    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt() error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        perror("bind() error: ");
        exit(1);
    }
    
    if(listen(listen_sock, BACKLOG) == -1){
        perror("listen() error: ");
        exit(1);
    }

    printf("Server started at port %d\n", port);

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if((conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &sin_size)) == -1){
            if(errno == EINTR)
                continue;
            else{
                perror("accept() error: ");
                exit(1);
            }
        }

        ThreadArg* arg = malloc(sizeof(ThreadArg));
        if(!arg){
            perror("malloc failed");
            close(conn_sock);
            continue;
        }
        
        arg->sockfd = conn_sock;
        arg->client_port = ntohs(client_addr.sin_port);
        
        if(inet_ntop(AF_INET, &client_addr.sin_addr, arg->client_ip, INET_ADDRSTRLEN) == NULL){
            perror("inet_ntop() error: ");
            free(arg);
            close(conn_sock);
            continue;
        }
        
        printf("You got a connection from %s:%d\n", arg->client_ip, arg->client_port);
        
        if(pthread_create(&tid, NULL, handle_client, arg) != 0){
            perror("pthread_create() error: ");
            free(arg);
            close(conn_sock);
            continue;
        }
        
//        pthread_detach(tid);
    }
    
    close(listen_sock);
    return 0;
    
}

