#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

#define BACKLOG 128
#define BUFF_SIZE 4096
#define ACCOUNT_FILE "account.txt"
#define INITIAL_POLL_SIZE 64

/**
 * @struct Account
 * @brief Structure to store account information loaded from file
 * 
 * @member username Dynamically allocated username string
 * @member status Account status (1 = active, 0 = locked)
 */
typedef struct {
    char* username;
    int status;
} Account;

/**
 * @struct Session
 * @brief Structure to store client session state with integrated buffer management
 * 
 * @member logged_in Flag indicating if user is authenticated (1 = logged in, 0 = not)
 * @member username Dynamically allocated username (empty string if not logged in)
 * @member sockfd Socket file descriptor for this client connection
 * @member client_ip Client IP address in dotted-decimal notation
 * @member client_port Client port number
 * @member active Flag indicating if session is active (1 = active, 0 = disconnected)
 * @member leftover Buffer to store incomplete data from previous recv() calls
 * @member leftover_len Length of data stored in leftover buffer
 * @member session_lock Mutex for protecting session state during command processing
 * 
 * @note In this event-driven architecture, each session manages its own receive buffer
 */
typedef struct {
    int logged_in;
    char* username;
    int sockfd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int active;
    char leftover[BUFF_SIZE];
    int leftover_len;
    pthread_mutex_t session_lock;
} Session;

/**
 * @struct WorkItem
 * @brief Structure representing a work item in the thread pool queue
 * 
 * @member session Pointer to the session that generated this work
 * @member message The command message to be processed
 * 
 * @note Used in producer-consumer pattern between main thread and worker threads
 */
typedef struct {
    Session* session;
    char message[BUFF_SIZE];
} WorkItem;

struct pollfd* poll_fds = NULL;
Session** sessions = NULL;
int poll_size = 0;
int poll_count = 0;
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
int active_connections = 0;

pthread_t worker_threads[10];
WorkItem work_queue[100];
int queue_front = 0, queue_rear = 0, queue_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

/**
 * @function load_account
 * @brief Load account information from the account file
 * 
 * @param inputUsername The username to search for
 * @param acc Pointer to Account structure to populate with found data
 * @return 1 if account found, 0 if not found, -1 on error
 * 
 * @details
 *  - Opens ACCOUNT_FILE ("account.txt") for reading
 *  - Reads file line by line using getline()
 *  - Expected line format: "username status" (space or tab separated)
 *  - Compares username with inputUsername (case-sensitive)
 *  - If found, uses strdup() to allocate and copy username
 *  - Status values: 1 = active account, 0 = locked account
 *  - Caller must free acc->username if return value is 1
 * 
 * @note No file locking - potential race condition if file is modified concurrently
 * @warning Memory leak if caller doesn't free acc->username on success
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
                acc->username = strdup(inputUsername);
                if (!acc->username) {
                    fclose(f);
                    free(line);
                    return -1;
                }
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
 * @function send_response
 * @brief Send response code to client in protocol format
 * 
 * @param sockfd Socket descriptor to send to
 * @param code Response code string (e.g., "100", "110", "211")
 * 
 * @details
 *  - Formats response as "CODE\r\n" (CRLF-terminated)
 *  - Sends complete response using send()
 *  - Prints error message if send() fails but doesn't close connection
 *  - Thread-safe: assumes exclusive access to socket within session lock
 * 
 * @protocol Response format: "CODE\r\n" where CODE is a 3-digit status code
 */

void send_response(int sockfd, const char* code){
    char response[BUFF_SIZE];
    snprintf(response, BUFF_SIZE, "%s\r\n", code);
    if(send(sockfd, response, strlen(response), 0) == -1){
        perror("send() error");
    }
}

/**
 * @function process_user_command
 * @brief Process USER command for user authentication
 * 
 * @param session Pointer to Session structure tracking login state
 * @param arg Username argument from USER command
 * 
 * @details
 *  - Acquires session->session_lock for thread-safe access
 *  - Validation checks (in order):
 *    1. Already logged in ? sends 213
 *    2. Empty username ? sends 300
 *  - Calls load_account() to verify username exists and get status
 *  - Response codes based on account state:
 *    + Account not found ? 212
 *    + Account locked (status=0) ? 211
 *    + Success ? 110
 *  - On success:
 *    + Sets session->logged_in = 1
 *    + Stores username in session
 *  - Releases lock before returning
 * 
 * @protocol USER <username>\r\n
 * @thread_safety Protected by session-level lock (session_lock)
 * @note Improvement over old version: per-session locking instead of global only
 */
void process_user_command(Session* session, const char* arg){
    pthread_mutex_lock(&session->session_lock);
    int sockfd = session->sockfd;
    
    if(session->logged_in){
        send_response(sockfd, "213");
        pthread_mutex_unlock(&session->session_lock);
        return;
    }
    
    if(strlen(arg) == 0){
        send_response(sockfd, "300");
        pthread_mutex_unlock(&session->session_lock);
        return;
    }
    
    Account acc = {NULL, 0};
    int result = load_account(arg, &acc);
            
    if(result == 1){
        if(acc.status == 0){
            send_response(sockfd, "211");
            free(acc.username);
            pthread_mutex_unlock(&session->session_lock);
            return;
        }
        
        session->logged_in = 1;
        if(session->username) free(session->username);
        session->username = strdup(acc.username);
        send_response(sockfd, "110");
        free(acc.username);
    }
    else if(result == 0){
        send_response(sockfd, "212");
    }
    else{
        send_response(sockfd, "500");
    }
    pthread_mutex_unlock(&session->session_lock);
}

/**
 * @function process_post_command
 * @brief Process POST command for posting articles
 * 
 * @param session Pointer to Session structure tracking login state
 * 
 * @details
 *  - Acquires session->session_lock for thread-safe access
 *  - Checks authentication state:
 *    + Not logged in ? sends 221
 *    + Logged in ? sends 120
 *  - Does not validate article content or length
 *  - Does not persist article data (stub implementation)
 *  - Releases lock before returning
 * 
 * @protocol POST\r\n
 * @thread_safety Protected by session-level lock
 * @note This is a minimal implementation - production version would handle article data
 */
void process_post_command(Session* session){
    pthread_mutex_lock(&session->session_lock);
    int sockfd = session->sockfd;
    
    if(!session->logged_in){
        send_response(sockfd, "221");
    }
    else{
        send_response(sockfd, "120");
    }
    pthread_mutex_unlock(&session->session_lock);
}

/**
 * @function process_bye_command
 * @brief Process BYE command for user logout
 * 
 * @param session Pointer to Session structure tracking login state
 * 
 * @details
 *  - Acquires session->session_lock for thread-safe access
 *  - Checks authentication state:
 *    + Not logged in ? sends 221
 *    + Logged in ? sends 130 and resets session state
 *  - On logout:
 *    + Sets session->logged_in = 0
 *    + Frees old username and allocates empty string
 *  - Does NOT close socket connection (client remains connected)
 *  - Releases lock before returning
 * 
 * @protocol BYE\r\n
 * @thread_safety Protected by session-level lock
 * @note Connection stays alive - client can login again with USER command
 */
void process_bye_command(Session* session){
    pthread_mutex_lock(&session->session_lock);
    int sockfd = session->sockfd;
    
    if(!session->logged_in){
        send_response(sockfd, "221");
    }
    else{
        send_response(sockfd, "130");
        session->logged_in = 0;
        if(session->username) free(session->username);
        session->username = strdup("");
    }
    pthread_mutex_unlock(&session->session_lock);
}

/**
 * @function process_command
 * @brief Parse and dispatch command to appropriate handler
 * 
 * @param session Pointer to Session that sent the command
 * @param buffer Command string (already stripped of \r\n delimiter)
 * 
 * @details
 *  - Parses command into command name and arguments:
 *    + Uses strchr() to find first space character
 *    + Extracts command name before space
 *    + Extracts arguments after space (if present)
 *  - Converts command to UPPERCASE for case-insensitive matching
 *  - Dispatches to appropriate handler:
 *    + "USER" ? process_user_command()
 *    + "POST" ? process_post_command()
 *    + "BYE" ? process_bye_command()
 *    + Unknown ? sends 300
 *  - Called from worker thread context
 * 
 * @protocol Format: "COMMAND [ARGUMENTS]\r\n"
 * @thread_safety Called by worker threads with session already validated
 * @improvement Over old version: uppercase conversion, better parsing
 */
void process_command(Session* session, const char* buffer){
    char cmd[20];
    char arg[BUFF_SIZE];
    arg[0] = '\0';
    
    char* space_ptr = strchr(buffer, ' ');
    if(space_ptr){
        size_t cmd_len = space_ptr - buffer;
        if(cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
        strncpy(cmd, buffer, cmd_len);
        cmd[cmd_len] = '\0';
        
        strncpy(arg, space_ptr + 1, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
    } else {
        strncpy(cmd, buffer, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
    }
    
    for(int i = 0; cmd[i]; i++){
        cmd[i] = toupper((unsigned char)cmd[i]);
    }

    if(strcmp(cmd, "USER") == 0) {
        process_user_command(session, arg);
    } else if(strcmp(cmd, "POST") == 0) {
        process_post_command(session);
    } else if(strcmp(cmd, "BYE") == 0) {
        process_bye_command(session);
    } else {
        send_response(session->sockfd, "300");
    }
}

/**
 * @function enqueue_work
 * @brief Add work item to thread pool queue for processing
 * 
 * @param session Pointer to Session that generated the work
 * @param message Command message to be processed
 * 
 * @details
 *  - Acquires queue_mutex for thread-safe queue access
 *  - Checks if queue has space (max 100 items)
 *  - If space available:
 *    + Creates WorkItem with session pointer and message copy
 *    + Adds to circular buffer at queue_rear position
 *    + Advances queue_rear (wraps around at 100)
 *    + Increments queue_count
 *    + Signals queue_cond to wake waiting worker thread
 *  - If queue full: silently drops the work item
 *  - Releases queue_mutex
 * 
 * @architecture Producer-Consumer pattern
 * @producer Main thread (poll loop) enqueues work
 * @consumer Worker threads dequeue and process
 * @note Non-blocking: drops work if queue full (potential message loss under extreme load)
 */
void enqueue_work(Session* session, const char* message){
    pthread_mutex_lock(&queue_mutex);
    if(queue_count < 100){
        work_queue[queue_rear].session = session;
        strncpy(work_queue[queue_rear].message, message, BUFF_SIZE - 1);
        work_queue[queue_rear].message[BUFF_SIZE - 1] = '\0';
        queue_rear = (queue_rear + 1) % 100;
        queue_count++;
        pthread_cond_signal(&queue_cond);
    }
    pthread_mutex_unlock(&queue_mutex);
}

/**
 * @function worker_thread
 * @brief Worker thread function that processes commands from work queue
 * 
 * @param arg Unused (void* for pthread compatibility)
 * @return NULL (never returns - runs indefinitely)
 * 
 * @details
 *  - Runs infinite loop waiting for work
 *  - Acquires queue_mutex and waits on queue_cond if queue empty
 *  - When work available:
 *    + Dequeues WorkItem from queue_front
 *    + Advances queue_front (circular buffer wraps at 100)
 *    + Decrements queue_count
 *    + Releases queue_mutex
 *  - Validates session is still active before processing
 *  - Calls process_command() to handle the command
 *  - Loops back to wait for next work item
 * 
 * @architecture
 *  - Thread pool: 10 worker threads compete for work
 *  - Circular buffer: 100-slot queue managed by front/rear pointers
 *  - Condition variable: efficient sleeping when no work available
 *  - Session validation: prevents use-after-free if session disconnected
 * 
 * @thread_safety
 *  - Queue access protected by queue_mutex
 *  - Session access protected by session->session_lock (in command handlers)
 * 
 * @performance Benefits of thread pool:
 *  - Avoids thread creation/destruction overhead
 *  - Bounded resource usage (10 threads max)
 *  - Better for high-frequency, short-lived tasks
 */
void* worker_thread(void* arg){
    (void)arg;
    while(1){
        pthread_mutex_lock(&queue_mutex);
        while(queue_count == 0){
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        
        WorkItem item = work_queue[queue_front];
        queue_front = (queue_front + 1) % 100;
        queue_count--;
        pthread_mutex_unlock(&queue_mutex);
        
        if(item.session && item.session->active){
            process_command(item.session, item.message);
        }
    }
    return NULL;
}

/**
 * @function find_session_by_sockfd
 * @brief Find session by socket file descriptor
 * 
 * @param sockfd Socket descriptor to search for
 * @return Pointer to Session if found, NULL if not found
 * 
 * @details
 *  - Acquires sessions_mutex for thread-safe array access
 *  - Linear search through sessions array up to poll_count
 *  - Compares session->sockfd with target sockfd
 *  - Returns pointer to matched session or NULL
 *  - Releases sessions_mutex before returning
 * 
 * @thread_safety Protected by sessions_mutex
 * @warning Returned pointer may become invalid if session is removed
 * @note Caller should check session->active before using returned pointer
 */
Session* find_session_by_sockfd(int sockfd){
    pthread_mutex_lock(&sessions_mutex);
    for(int i = 0; i < poll_count; i++){
        if(sessions[i] && sessions[i]->sockfd == sockfd){
            Session* s = sessions[i];
            pthread_mutex_unlock(&sessions_mutex);
            return s;
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    return NULL;
}

/**
 * @function recv_until_delim
 * @brief Receive data from socket until delimiter is encountered
 * 
 * @param session Pointer to Session (contains sockfd and leftover buffer)
 * @param out_buf Buffer to store extracted message (without delimiter)
 * @param max_len Maximum size of out_buf
 * @param delim Delimiter string to search for (typically "\r\n")
 * @return Number of bytes in message (excluding delimiter), 0 if connection closed, -1 on error
 * 
 * @details
 *  - First checks session->leftover buffer for data from previous recv()
 *  - Loops receiving data until delimiter found:
 *    1. Searches for delimiter using strstr()
 *    2. If found:
 *       - Extracts message up to delimiter into out_buf
 *       - Null-terminates message
 *       - Saves remaining data after delimiter into leftover buffer
 *       - Returns message length
 *    3. If not found:
 *       - Receives more data from socket
 *       - Appends to out_buf
 *       - Continues loop
 *  - Handles connection closure (recv returns 0 or -1)
 *  - Prevents buffer overflow by limiting recv size
 * 
 * @protocol Supports pipelined messages (multiple commands in one TCP packet)
 * @buffer_management Uses session->leftover to store incomplete data between calls
 * @note Not thread-safe - assumes single thread receiving from each socket
 */
int recv_until_delim(Session* session, char* out_buf, int max_len, const char* delim){
    char temp_buf[BUFF_SIZE];
    int total_len = 0;

    if(session->leftover_len > 0){
        memcpy(out_buf, session->leftover, session->leftover_len);
        total_len = session->leftover_len;
        session->leftover_len = 0;
    }

    while(1){
        char* delim_pos = strstr(out_buf, delim);
        if(delim_pos){
            int msg_len = delim_pos - out_buf;
            out_buf[msg_len] = '\0';

            int remain_len = total_len - (msg_len + strlen(delim));
            if(remain_len > 0){
                memcpy(session->leftover, delim_pos + strlen(delim), remain_len);
                session->leftover_len = remain_len;
            }
            return msg_len;
        }

        int bytes_recv = recv(session->sockfd, temp_buf, sizeof(temp_buf), 0);
        if(bytes_recv <= 0)
            return bytes_recv;

        if(total_len + bytes_recv >= max_len)
            bytes_recv = max_len - total_len - 1;

        memcpy(out_buf + total_len, temp_buf, bytes_recv);
        total_len += bytes_recv;
        out_buf[total_len] = '\0';
    }
}

/**
 * @function expand_poll_arrays
 * @brief Dynamically expand poll_fds and sessions arrays when capacity reached
 * 
 * @details
 *  - Doubles the size of both arrays (new_size = poll_size * 2)
 *  - Reallocates poll_fds array with realloc()
 *  - Reallocates sessions array with realloc()
 *  - Initializes new slots:
 *    + sessions[i] = NULL
 *    + poll_fds[i].fd = -1
 *    + poll_fds[i].events = 0
 *  - Updates poll_size to new capacity
 *  - Prints expansion message for monitoring
 * 
 * @error_handling If realloc fails, prints error and returns without changing arrays
 * @thread_safety Must be called with sessions_mutex locked
 * @scalability Allows server to handle growing number of connections dynamically
 * @note Exponential growth (doubling) provides O(log n) reallocations over time
 */
void expand_poll_arrays(){
    int new_size = poll_size * 2;
    
    struct pollfd* new_poll_fds = realloc(poll_fds, new_size * sizeof(struct pollfd));
    if(!new_poll_fds){
        perror("realloc poll_fds failed");
        return;
    }
    poll_fds = new_poll_fds;
    
    Session** new_sessions = realloc(sessions, new_size * sizeof(Session*));
    if(!new_sessions){
        perror("realloc sessions failed");
        return;
    }
    sessions = new_sessions;
    
    for(int i = poll_size; i < new_size; i++){
        sessions[i] = NULL;
        poll_fds[i].fd = -1;
        poll_fds[i].events = 0;
    }
    
    poll_size = new_size;
    printf("[EXPAND] Poll arrays expanded to %d slots\n", poll_size);
}

/**
 * @function add_to_poll
 * @brief Add new client connection to poll array and create session
 * 
 * @param sockfd Socket file descriptor of new connection
 * @param ip Client IP address string
 * @param port Client port number
 * @return 0 on success, -1 on failure
 * 
 * @details
 *  - Acquires sessions_mutex for thread-safe array modification
 *  - Checks if array is full and calls expand_poll_arrays() if needed
 *  - Allocates and initializes new Session structure:
 *    + logged_in = 0 (not authenticated)
 *    + username = "" (empty string)
 *    + sockfd, client_ip, client_port set from parameters
 *    + active = 1
 *    + leftover_len = 0
 *    + Initializes session_lock mutex
 *  - Adds to poll arrays at position poll_count:
 *    + poll_fds[poll_count].fd = sockfd
 *    + poll_fds[poll_count].events = POLLIN (monitor for readable data)
 *    + sessions[poll_count] = new_session
 *  - Increments poll_count and active_connections
 *  - Releases sessions_mutex
 * 
 * @thread_safety Protected by sessions_mutex
 * @scalability Automatically expands arrays when full
 * @note Session pointer remains valid until removed by remove_from_poll()
 */
int add_to_poll(int sockfd, const char* ip, int port){
    pthread_mutex_lock(&sessions_mutex);
    
    if(poll_count >= poll_size){
        expand_poll_arrays();
    }
    
    Session* new_session = malloc(sizeof(Session));
    if(!new_session){
        pthread_mutex_unlock(&sessions_mutex);
        return -1;
    }
    
    new_session->logged_in = 0;
    new_session->username = strdup("");
    new_session->sockfd = sockfd;
    strncpy(new_session->client_ip, ip, INET_ADDRSTRLEN);
    new_session->client_port = port;
    new_session->active = 1;
    new_session->leftover_len = 0;
    pthread_mutex_init(&new_session->session_lock, NULL);
    
    poll_fds[poll_count].fd = sockfd;
    poll_fds[poll_count].events = POLLIN;
    sessions[poll_count] = new_session;
    poll_count++;
    active_connections++;
    
    pthread_mutex_unlock(&sessions_mutex);
    return 0;
}

/**
 * @function remove_from_poll
 * @brief Remove session from poll array and free resources
 * 
 * @param index Index in poll_fds/sessions arrays to remove
 * 
 * @details
 *  - Acquires sessions_mutex for thread-safe array modification
 *  - Validates index is within bounds
 *  - Cleans up session resources:
 *    + Destroys session_lock mutex
 *    + Frees username string
 *    + Frees session structure
 *  - Compacts arrays by shifting elements:
 *    + Moves poll_fds[i+1..poll_count-1] down by one position
 *    + Moves sessions[i+1..poll_count-1] down by one position
 *  - Decrements poll_count and active_connections
 *  - Releases sessions_mutex
 * 
 * @thread_safety Protected by sessions_mutex
 * @note Called when client disconnects or connection error occurs
 * @warning After this function, all session pointers in work queue become invalid
 * @algorithm Array compaction: O(n) time complexity where n = number of connections
 */
void remove_from_poll(int index){
    pthread_mutex_lock(&sessions_mutex);
    
    if(index < 0 || index >= poll_count){
        pthread_mutex_unlock(&sessions_mutex);
        return;
    }
    
    Session* session = sessions[index];
    if(session){
        pthread_mutex_destroy(&session->session_lock);
        if(session->username) free(session->username);
        free(session);
    }
    
    for(int i = index; i < poll_count - 1; i++){
        poll_fds[i] = poll_fds[i + 1];
        sessions[i] = sessions[i + 1];
    }
    
    poll_count--;
    active_connections--;
    
    pthread_mutex_unlock(&sessions_mutex);
}

int main(int argc, char* argv[]){
    if(argc != 2){
        printf("Usage: ./server Port_Number\n");
        return 1;
    }

    int port = atoi(argv[1]);
    int listen_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;

    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    
    rl.rlim_cur = rl.rlim_max;
    if(setrlimit(RLIMIT_NOFILE, &rl) == 0){
        getrlimit(RLIMIT_NOFILE, &rl);
    } else {
        printf("Warning: Could not increase FD limit\n");
    }

    poll_size = INITIAL_POLL_SIZE;
    poll_fds = calloc(poll_size, sizeof(struct pollfd));
    sessions = calloc(poll_size, sizeof(Session*));
    
    if(!poll_fds || !sessions){
        perror("Failed to allocate initial arrays");
        exit(1);
    }
    
    for(int i = 0; i < poll_size; i++){
        poll_fds[i].fd = -1;
        sessions[i] = NULL;
    }

    for(int i = 0; i < 10; i++){
        pthread_create(&worker_threads[i], NULL, worker_thread, NULL);
        pthread_detach(worker_threads[i]);
    }

    if((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket() error");
        exit(1);
    }

    int opt = 1;
    if(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
        perror("setsockopt() error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        perror("bind() error");
        exit(1);
    }

    if(listen(listen_sock, BACKLOG) == -1){
        perror("listen() error");
        exit(1);
    }

    printf("Server started at port %d\n", port);

    poll_fds[0].fd = listen_sock;
    poll_fds[0].events = POLLIN;
    poll_count = 1;

    while(1){
        int ret = poll(poll_fds, poll_count, -1);
        
        if(ret == -1){
            perror("poll() error");
            continue;
        }
        
        for(int i = 0; i < poll_count; i++){
            if(poll_fds[i].revents & POLLIN){
                
                if(poll_fds[i].fd == listen_sock){
                    sin_size = sizeof(client_addr);
                    int new_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &sin_size);
                    
                    if(new_sock == -1){
                        perror("accept() error");
                        continue;
                    }

                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                    int client_port = ntohs(client_addr.sin_port);

                    if(add_to_poll(new_sock, client_ip, client_port) == 0){
                        printf("[CONNECT] New client from %s:%d (socket %d) [Active: %d]\n", 
                               client_ip, client_port, new_sock, active_connections);
                        send_response(new_sock, "100");
                    } else {
                        printf("[REJECT] Failed to add client %s:%d\n", client_ip, client_port);
                        send_response(new_sock, "500");
                        close(new_sock);
                    }
                }
                else{
                    Session* session = sessions[i];
                    if(!session) continue;

                    char buff[BUFF_SIZE];
                    
                    while(1){
                    	int bytes = recv_until_delim(session, buff, BUFF_SIZE, "\r\n");

	                    if(bytes <= 0){
	                        printf("[DISCONNECT] Client %s:%d (socket %d) disconnected [Active: %d]\n",
	                               session->client_ip, session->client_port, 
	                               session->sockfd, active_connections - 1);
	                        
	                        close(session->sockfd);
	                        session->active = 0;
	                        remove_from_poll(i);
	                        i--;
	                    }
	                    else{
	                        printf("[RECEIVED] %s:%d: %s\n", 
	                               session->client_ip, session->client_port, buff);
	                        enqueue_work(session, buff);
	                    }
	                    
	                    if (session->leftover_len == 0) break;
					} 
                }
            }
        }
    }

    close(listen_sock);
    free(poll_fds);
    free(sessions);
    return 0;
}
