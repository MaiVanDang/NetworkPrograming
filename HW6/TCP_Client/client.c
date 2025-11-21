#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#define BUFF_SIZE 4096
#define INITIAL_BUFFER_SIZE 64
#define BUFFER_GROW_SIZE 32

typedef struct {
    int sockfd;
    char leftover[BUFF_SIZE];
    int leftover_len;
} ClientConn;

ClientConn global_client;

/**
 * @function read_line
 * @brief Read a line of input with dynamic memory allocation.
 *
 * @return A pointer to dynamically allocated string containing the input.
 *         NULL if memory allocation fails or input is empty.
 *
 * @details
 *  - Starts with INITIAL_BUFFER_SIZE (64 bytes).
 *  - Grows by BUFFER_GROW_SIZE (32 bytes) when needed.
 *  - Reads character by character until '\n' or EOF.
 *  - Automatically resizes buffer using realloc() as input grows.
 *  - Returns NULL if input is empty (length = 0).
 *  - Final realloc() to fit exact length (optimization).
 *  - Caller is responsible for freeing returned memory.
 */
char* read_line(){
	size_t capacity = INITIAL_BUFFER_SIZE;
	size_t length = 0;
	char* buffer = malloc(capacity);
	
	if(!buffer) {
		printf("Memory allocation failed.\n");
		return NULL;
	} 
	
	int c;
	while((c = getchar()) != '\n' && c != EOF){
		if (length >= capacity - 1){
			capacity += BUFFER_GROW_SIZE;
			char* newBuffer = realloc(buffer, capacity);
            if (!newBuffer) {
                free(buffer);
                printf("Memory reallocation failed.\n");
                return NULL;
            }
            buffer = newBuffer;
		} 
		
		buffer[length++] = c;
	}
	
	buffer[length] = '\0';
	
	if (length == 0) {
        free(buffer);
        return NULL;
    }
	
	char* finalBuffer = realloc(buffer, length + 1);
    if (finalBuffer) {
        return finalBuffer;
    }
    
    return buffer;
}

/**
 * @function recv_until_delim
 * @brief Receive data from socket until the delimiter "\r\n" is encountered.
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
 * @function handle_server_response
 * @brief Receive and display server response.
 *
 * @param client Pointer to ClientConn structure containing socket and leftover buffer.
 * @param buff Buffer to store received data.
 * @return 1 if response received successfully, 0 if connection lost.
 *
 * @details
 *  - Receives response using recv_until_delim() with "\r\n" delimiter.
 *  - Parses response code using sscanf().
 *  - Displays appropriate message for each response code:
 *    + 100: Connection successful
 *    + 110: Login successful
 *    + 120: Post successful
 *    + 130: Logout successful
 *    + 211: Account is locked
 *    + 212: Account does not exist
 *    + 213: Session already logged in
 *    + 214: Account is already logged in on another client 
 *    + 221: Not logged in
 *    + 300: Unknown message type
 *    + 500: Internal server error
 *  - Prints "Connection lost!" if recv returns <= 0.
 *  - Returns 0 on connection loss, 1 otherwise.
 */
int handle_server_response(ClientConn* client, char* buff) {
    int received_bytes = recv_until_delim(client, buff, BUFF_SIZE, "\r\n");
    if (received_bytes > 0) {
        char code[4];
        sscanf(buff, "%s", code);
        
        if(strcmp(code, "100") == 0)
            printf("[Server] 100 Connection successful\n");
        else if(strcmp(code, "110") == 0)
            printf("[Server] 110 Login successful\n");
        else if(strcmp(code, "120") == 0)
            printf("[Server] 120 Post successful\n");
        else if(strcmp(code, "130") == 0)
            printf("[Server] 130 Logout successful\n");
        else if(strcmp(code, "211") == 0)
            printf("[Server] 211 Account is locked\n");
        else if(strcmp(code, "212") == 0)
            printf("[Server] 212 Account does not exist\n");
        else if(strcmp(code, "213") == 0)
            printf("[Server] 213 Session already logged in\n");
        else if(strcmp(code, "214") == 0)
			printf("[Server] 214 Account is already logged in on another client\n");
        else if(strcmp(code, "221") == 0)
            printf("[Server] 221 Not logged in\n");
        else if(strcmp(code, "300") == 0)
            printf("[Server] 300 Unknown message type\n");
        else if(strcmp(code, "500") == 0)
            printf("[Server] 500 Internal server error\n");
        else
            printf("[Server] Unknown response: %s\n", code);
        
        return 1;
    } else {
        printf("Connection lost!\n");
        return 0;
    }
}

/**
 * @function send_message
 * @brief Send message to server with delimiter "\r\n".
 *
 * @param sockfd Socket descriptor to send data to.
 * @param message Message to send (without \r\n).
 *
 * @details
 *  - Formats message as "message\r\n" using snprintf().
 *  - Sends complete message through socket using send().
 *  - Does not check for send() errors.
 *  - Maximum message length is BUFF_SIZE - 3 (for \r\n\0).
 */
void send_message(int sockfd, const char* message){
    char buff[BUFF_SIZE];
    snprintf(buff, BUFF_SIZE, "%s\r\n", message);
    send(sockfd, buff, strlen(buff), 0);
}

/**
 * @function print_menu
 * @brief Display menu options for user.
 *
 * @details
 *  - Displays a formatted menu with 4 options:
 *    1. Login (USER username)
 *    2. Post article (POST article)
 *    3. Logout (BYE)
 *    4. Exit
 *  - Prompts user to enter choice.
 *  - Called in main loop before reading user input.
 */
void print_menu(){
    printf("\n=== MENU ===\n");
    printf("1. Login (USER username)\n");
    printf("2. Post article (POST article)\n");
    printf("3. Logout (BYE)\n");
    printf("4. Exit\n");
    printf("============\n");
    printf("Your choice: ");
}

/**
 * @function handle_login
 * @brief Handle login command (USER username).
 *
 * @param client Pointer to ClientConn structure containing socket and leftover buffer.
 * @param buff Buffer for receiving server response.
 * @return 1 if successful or connection alive, 0 if connection lost.
 *
 * @details
 *  - Prompts user to enter username using printf().
 *  - Reads username using read_line() (dynamic allocation).
 *  - Validates that username is not empty.
 *  - Formats command as "USER username".
 *  - Sends command to server using send_message().
 *  - Receives and displays server response using handle_server_response().
 *  - Frees allocated memory for username.
 *  - Returns 0 if connection lost, 1 otherwise.
 */
int handle_login(ClientConn* client, char* buff){
    printf("Enter username: ");
    char* username = read_line();
    if(username && strlen(username) > 0){       
        char message[BUFF_SIZE];
        snprintf(message, BUFF_SIZE, "USER %s", username);
        send_message(client->sockfd, message);
                    
        int result = handle_server_response(client, buff);
        free(username);
        return result;
    }
    else{
        printf("Username cannot be empty!\n");
        if(username) free(username);
    } 
    return 1;
}

/**
 * @function handle_post
 * @brief Handle post article command (POST article).
 *
 * @param client Pointer to ClientConn structure containing socket and leftover buffer.
 * @param buff Buffer for receiving server response.
 * @return 1 if successful or connection alive, 0 if connection lost.
 *
 * @details
 *  - Prompts user to enter article content using printf().
 *  - Reads article using read_line() (dynamic allocation).
 *  - Validates that article is not empty.
 *  - Formats command as "POST article".
 *  - Sends command to server using send_message().
 *  - Receives and displays server response using handle_server_response().
 *  - Frees allocated memory for article.
 *  - Requires user to be logged in (server validates).
 *  - Returns 0 if connection lost, 1 otherwise.
 */
int handle_post(ClientConn* client, char* buff){
    printf("Enter article: ");
    char* article = read_line();
    if(article && strlen(article) > 0){
        char message[BUFF_SIZE];
        snprintf(message, BUFF_SIZE, "POST %s", article);
        send_message(client->sockfd, message);
        int result = handle_server_response(client, buff);
        free(article);
        return result;
    }
    else{
        printf("Message cannot be empty!\n");
        if(article) free(article);
    } 
    return 1;
}

/**
 * @function handle_logout
 * @brief Handle logout command (BYE).
 *
 * @param client Pointer to ClientConn structure containing socket and leftover buffer.
 * @param buff Buffer for receiving server response.
 * @return 1 if successful or connection alive, 0 if connection lost.
 *
 * @details
 *  - Sends "BYE" command to server using send_message().
 *  - Receives and displays server response using handle_server_response().
 *  - Requires user to be logged in (server validates).
 *  - Returns 0 if connection lost, 1 otherwise.
 *  - Does not close socket (allows user to login again).
 */
int handle_logout(ClientConn* client, char* buff){
    send_message(client->sockfd, "BYE");
    return handle_server_response(client, buff);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./client IP_Addr Port_Number\n");
        return 1;
    }
    
    char* server_addr_str = argv[1];
    int server_port = atoi(argv[2]);

    int client_sock;
    struct sockaddr_in server_addr;
    char buff[BUFF_SIZE];
    int received_bytes;

    if((client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket() error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if(inet_pton(AF_INET, server_addr_str, &server_addr.sin_addr) <= 0){
        perror("inet_pton() error");
        exit(1);
    }
    
    if(connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("connect() error");
        exit(1);
    }

    printf("Connected to server %s:%d\n", server_addr_str, server_port);

    global_client.sockfd = client_sock;
    global_client.leftover_len = 0;
    
    received_bytes = recv_until_delim(&global_client, buff, BUFF_SIZE, "\r\n");
    if(received_bytes > 0){
        char code[10];
        sscanf(buff, "%s", code);
        if(strcmp(code, "100") == 0)
            printf("[Server] Connection successful\n");
    }

    while (1) {
        print_menu();
        
        int choice;
        if(scanf("%d", &choice) != 1){
            while(getchar() != '\n');
            printf("Invalid input!\n");
            continue;
        }
        while(getchar() != '\n');

        int continue_loop = 1;
        
        switch (choice) {
            case 1: 
                continue_loop = handle_login(&global_client, buff); 
                break;
            case 2: 
                continue_loop = handle_post(&global_client, buff); 
                break;
            case 3: 
                continue_loop = handle_logout(&global_client, buff); 
                break;
            case 4: 
                printf("Closing connection...\n"); 
                close(client_sock);
                return 0;
            default: 
                printf("Invalid choice!\n");
        }
        
        if (!continue_loop) {
            printf("Server disconnected. Exiting...\n");
            break;
        }
    }

    close(client_sock);
    return 0;
}
