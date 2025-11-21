#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define BUFF_SIZE 2048
#define MAX_TEST_CONNS 2000  // Test v?i 2000 k?t n?i!

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <server_port>\n", argv[1]);
        return 1;
    }

    int serverPort = atoi(argv[1]);
    struct sockaddr_in serverAddr;
    int clients[MAX_TEST_CONNS];
    int numConnected = 0;
    int numRejected = 0;
    char rBuff[BUFF_SIZE];
    
    bzero(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    printf("=== STRESS TEST: Testing 1100 concurrent connections ===\n");
    printf("Server: %s:%d\n", SERVER_IP, serverPort);
    printf("Expected: First 1024 accepted, remaining rejected\n\n");

    // T?o 1100 k?t n?i ð? test gi?i h?n
    for (int i = 0; i < MAX_TEST_CONNS; i++) {
        clients[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (clients[i] == -1) {
            perror("socket() error");
            break;
        }

        // Set timeout ð? tránh b? treo
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(clients[i], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(clients[i], SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(clients[i], (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
            printf("[%04d] Connect failed: %s\n", i+1, strerror(errno));
            close(clients[i]);
            clients[i] = -1;
            break;
        }

        // Nh?n response t? server
        int ret = recv(clients[i], rBuff, BUFF_SIZE, 0);
        if (ret <= 0) {
            printf("[%04d] No response from server\n", i+1);
            close(clients[i]);
            clients[i] = -1;
            numRejected++;
        } else {
            rBuff[ret] = '\0';
            
            // Ki?m tra xem có b? reject không
            if (strstr(rBuff, "500") != NULL) {
                printf("[%04d] REJECTED by server: %s", i+1, rBuff);
                close(clients[i]);
                clients[i] = -1;
                numRejected++;
            } else if (strstr(rBuff, "100") != NULL) {
                numConnected++;
                if ((i+1) % 100 == 0 || i < 10 || i >= MAX_TEST_CONNS - 10) {
                    printf("[%04d] ACCEPTED: %s", i+1, rBuff);
                }
            } else {
                printf("[%04d] Unknown response: %s", i+1, rBuff);
                numConnected++;
            }
        }

        // Delay nh? ð? tránh quá t?i server
        usleep(5000); // 5ms
    }

    printf("\n=== RESULTS ===\n");
    printf("Total attempts:    %d\n", MAX_TEST_CONNS);
    printf("Accepted:          %d\n", numConnected);
    printf("Rejected:          %d\n", numRejected);
    printf("Expected accepted: 1024\n");
    printf("Expected rejected: %d\n", MAX_TEST_CONNS - 1024);

    if (numConnected == 1024 && numRejected == MAX_TEST_CONNS - 1024) {
        printf("\n? TEST PASSED: Server correctly limits to 1024 connections\n");
    } else if (numConnected >= 1024) {
        printf("\n??  WARNING: Server accepted more than 1024 connections\n");
    } else {
        printf("\n? TEST FAILED: Server behavior unexpected\n");
    }

    // Test thêm: Ðóng m?t vài k?t n?i và th? k?t n?i m?i
    printf("\n=== TESTING SLOT RELEASE ===\n");
    printf("Closing 10 connections...\n");
    int closed = 0;
    for (int i = 0; i < MAX_TEST_CONNS && closed < 10; i++) {
        if (clients[i] != -1) {
            close(clients[i]);
            clients[i] = -1;
            closed++;
        }
    }

    sleep(1); // Ð?i server x? l? disconnect

    printf("Attempting 10 new connections...\n");
    int newConnected = 0;
    for (int i = 0; i < 10; i++) {
        int newSock = socket(AF_INET, SOCK_STREAM, 0);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(newSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(newSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == 0) {
            int ret = recv(newSock, rBuff, BUFF_SIZE, 0);
            if (ret > 0 && strstr(rBuff, "100") != NULL) {
                newConnected++;
                printf("[NEW %d] Accepted - slot was released correctly\n", i+1);
            }
            close(newSock);
        }
    }

    if (newConnected == 10) {
        printf("\n? SLOT RELEASE TEST PASSED\n");
    } else {
        printf("\n??  Only %d/10 new connections accepted\n", newConnected);
    }

    // Ðóng t?t c? k?t n?i c?n l?i
    printf("\nCleaning up...\n");
    for (int i = 0; i < MAX_TEST_CONNS; i++) {
        if (clients[i] != -1) {
            close(clients[i]);
        }
    }

    printf("Test completed.\n");
    return 0;
}
