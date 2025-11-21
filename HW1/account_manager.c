#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>

#define MAX_LOG_LENGTH 2048
#define MAX_LOG_TIME_LENGTH 100
#define INITIAL_BUFFER_SIZE 64
#define BUFFER_GROW_SIZE 32

#define MENU_LOGIN 1
#define MENU_POST_MESSAGE 2
#define MENU_LOGOUT 3
#define MENU_EXIT 4

FILE *log_file = NULL;

typedef struct {
    char *username;
    int status;
    bool isLoggedIn;
} Account;

/* List of functions */

/**
 * @function readLine: Read a line of input with dynamic memory allocation.
 * 
 * @return: A pointer to dynamically allocated string containing the input.
 *          NULL if memory allocation fails or input is empty.
 */
char* readLine();

void displayMenu();

/**
 * @function login: Handle user login process.
 * 
 * @param accountFilename: A pointer to string containing account file path.
 * @param account: A pointer to Account structure to store login information.
 * 
 * @return: A string indicating the login result.
 *          "+OK"  if user login success.
 *          "-ERR" if user login fail.
 */
const char* login(const char* accountFilename, Account* account);

/**
 * @function verifyLogin: Check if user is currently logged in.
 * 
 * @param account: A pointer to Account structure containing current state.
 * 
 * @return: true if user is logged in.
 			false if user is not logged in.
 */
bool verifyLogin(const Account* account);

/**
 * @function authenticate: Verify user credentials against account file.
 * 
 * @param accountFilename: A pointer to string containing account file path.
 * @param inputUsername: A pointer to string containing username to verify.
 * @param account: A pointer to Account structure to store user information.
 * 
 * @return: true if user found in file.
 			false if user not found or file error.
 */
bool authenticate(const char* accountFilename, const char* inputUsername, Account* account);

/**
 * @function checkAccountActiveStatus: Check if account status is active.
 * 
 * @param accountStatus: Integer representing account status from file.
 * 
 * @return: 1 if account is active.
 			0 if account is inactive.
 */
int checkAccountActiveStatus(int accountStatus);

/**
 * @function postMessage: Process user's message posting request.
 * 
 * @param account: A pointer to Account structure containing current user state.
 * @param message: A pointer to char array to store the message.
 * 
 * @return: A string indicating the postMessage result.
 *          "+OK"  if user post mesage success.
 *          "-ERR" if user post mesage fail.
 */
const char* postMessage(const Account* account, char** message);

/**
 * @function logout: Handle user logout process.
 * 
 * @param account: A pointer to Account structure to update logout state.
 * 
 * @return: A string indicating the logout result.
 *          "+OK"  if user logout success.
 *          "-ERR" if user logout fail.
 */
const char* logout(Account* account);

/**
 * @function clearInputBuffer: Clear remaining characters from input buffer.
 * 
 * @return: Void.
 */
void clearInputBuffer();

/**
 * @function log_with_time: Generate a log message string with current timestamp.
 *
 * @param buffer A pointer to char array to store the formatted log message.
 * @param size   The maximum size of the buffer.
 * @param format A printf-style format string for the log message.
 * @param ...    Additional arguments corresponding to the format string.
 *
 */
void log_with_time(char *buffer, size_t size, const char* format, ...);


/**
 * @function log_in_file: Write a log message into the log file.
 *
 * @param format A printf-style format string for the log message.
 * @param ...    Additional arguments corresponding to the format string.
 *
 * @return void
 *
 */
void log_in_file(const char* format, ...);


/* Function implementations */

char* readLine(){
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

void displayMenu() {
    printf("\n=== USER ACCOUNT MANAGEMENT SYSTEM ===\n");
    printf("1. Login\n");
    printf("2. Post Message\n");
    printf("3. Logout\n");
    printf("4. Exit\n");
    printf("Please choose: ");
}

const char* login(const char* accountFilename, Account* account) {
    
    if (verifyLogin(account)) {
        printf("You have already logged in.\n");
        return "-ERR";
    }
    
    printf("Username: ");
    char* inputUsername = readLine();
    if (!inputUsername || strlen(inputUsername) == 0) {
        if (inputUsername) free(inputUsername);
        printf("Invalid username.\n");
        return "-ERR";
    }
    
    const char* result;
    
    if (authenticate(accountFilename, inputUsername, account)) {
        if (checkAccountActiveStatus(account->status)) { 
            account->isLoggedIn = true;
            printf("Hello %s.\n", account->username);
            result = "+OK";
        } else {
            printf("Account is banned.\n");
            result = "-ERR";
        }
    } else {
        printf("Account does not exist.\n");
        result = "-ERR";
    }
    
    free(inputUsername);
    return result;
}

bool verifyLogin(const Account* account) {
    return account->isLoggedIn;
}

bool authenticate(const char* accountFilename, const char* inputUsername, Account* account) {
    
	FILE* accountFile = fopen(accountFilename, "r");
    if (!accountFile) {
        printf("Error: Cannot open account file.\n");
        return false;
    }
    
    char* line = NULL;
    size_t lineSize = 0;
    
    bool foundUser = false;
    
    while (getline(&line, &lineSize, accountFile) > 0) {
        char* username = strtok(line, " \t\n");
        char* statusStr = strtok(NULL, " \t\n");
        
        if (username && statusStr) {
            int status = atoi(statusStr);
            
            if (strcmp(username, inputUsername) == 0) {
                size_t usernameLen = strlen(inputUsername);
                account->username = malloc(usernameLen + 1);
                if (account->username) {
                    strcpy(account->username, inputUsername);
                    account->status = status;
                    foundUser = true;
                }else {
		            printf("Memory allocation failed.\n");
		        }
                break;
            }
        }
    }
    
    free(line);
    fclose(accountFile);
    return foundUser;
}

int checkAccountActiveStatus(int accountStatus) {
    return (accountStatus == 1);
}

const char* postMessage(const Account* account, char** message) {
    printf("Post message: ");
    
    *message = readLine();
    
    if (!*message || strlen(*message) == 0) {
        if (*message) free(*message);
        *message = NULL;
        printf("Invalid message.\n");
        return "-ERR";
    } 
    
    if (verifyLogin(account)) {
        printf("Successful post.\n");
        return "+OK"; 
    } else {
        printf("You have not logged in.\n");
        return "-ERR"; 
    }
}

const char* logout(Account* account) {
    if (verifyLogin(account)) {
        account->isLoggedIn = false;
        if (account->username) {
	        free(account->username);
	        account->username = NULL;
	    }
        printf("Successful log out.\n");
        return "+OK";
    } else {
        printf("You have not logged in.\n");
        return "-ERR";
    }
}

void clearInputBuffer(void) {
    int bufferChar;
    
    while ((bufferChar = getchar()) != '\n' && bufferChar != EOF) {
        /* Clear all remaining characters in buffer */
    }
}

void log_with_time(char *buffer, size_t size, const char* format, ...) {
    time_t current_time;
    struct tm *local_time;
    char time_buffer[MAX_LOG_TIME_LENGTH];
    va_list args;

    current_time = time(NULL);
    local_time = localtime(&current_time);

    strftime(time_buffer, sizeof(time_buffer),
             "[%d/%m/%Y %H:%M:%S]", local_time);

    int offset = snprintf(buffer, size, "%s ", time_buffer);

    va_start(args, format);
    vsnprintf(buffer + offset, size - offset, format, args);
    va_end(args);
}

void log_in_file(const char* format, ...) {
    if (log_file == NULL) return;

    char log_buffer[MAX_LOG_LENGTH];
    va_list args;

    va_start(args, format);
    vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    va_end(args);

    fprintf(log_file, "%s\n", log_buffer);
    fflush(log_file);
}

int main(int argc, char* argv[]) {
    Account currentAccount;
    int menuChoice;
    bool running;
    char buffer[MAX_LOG_LENGTH];
    
    memset(&currentAccount, 0, sizeof(Account));
    currentAccount.isLoggedIn = false;
    running = true;
    
    if (argc != 2) {
        printf("Usage: %s <account_file>\n", argv[0]);
        return 1;
    }
    
    log_file = fopen("log_20225699.txt", "a");
    if (log_file == NULL) {
        printf("Error: Cannot open log file.\n");
        return 1;
    }
    
    while (running) {
        displayMenu();
        if (scanf("%d", &menuChoice) != 1) {
		    printf("Invalid input. Please enter a number(1->4).\n");
		    clearInputBuffer();
		    continue;
		}
		
		clearInputBuffer();
		
        char result[5]; 
        
        switch (menuChoice) {
            case MENU_LOGIN:{
            	strcpy(result, login(argv[1], &currentAccount));
			    const char* usernameForLog = (currentAccount.username && currentAccount.isLoggedIn) 
			                                ? currentAccount.username : "";
			    log_with_time(buffer, sizeof(buffer), "$ %d $ %s $ %s", menuChoice, usernameForLog, result);
			    log_in_file("%s", buffer);
			    break;
            }
                
            case MENU_POST_MESSAGE: {
			    char* message = NULL;
				strcpy(result, postMessage(&currentAccount, &message));
				if (message) {
				    log_with_time(buffer, sizeof(buffer), "$ %d $ %s $ %s", menuChoice, message, result);
				    free(message);
				} else {
				    log_with_time(buffer, sizeof(buffer), "$ %d $ $ %s", menuChoice, result);
				}
				log_in_file("%s", buffer);
			    break;
			}
                
            case MENU_LOGOUT:
                strcpy(result, logout(&currentAccount));
                log_with_time(buffer, sizeof(buffer), "$ %d $ $ %s", menuChoice, result);
    			log_in_file("%s", buffer);
                break;
                
            case MENU_EXIT:
                running = false;
                log_with_time(buffer, sizeof(buffer), "$ %d $ $ +OK", menuChoice);
    			log_in_file("%s", buffer);
                break;
        }
    }
    
    if (currentAccount.username) {
	    free(currentAccount.username);
	    currentAccount.username = NULL;
	}
	
	if (log_file) {
	    fclose(log_file);
	}
    
    return 0;
}
