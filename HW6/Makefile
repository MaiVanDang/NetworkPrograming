CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread
TARGET_SERVER = server
TARGET_CLIENT = client

all: $(TARGET_SERVER) $(TARGET_CLIENT)

$(TARGET_SERVER): TCP_Server/server.c
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) TCP_Server/server.c $(LDFLAGS)

$(TARGET_CLIENT): TCP_Client/client.c
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) TCP_Client/client.c $(LDFLAGS)

clean:
	rm -f $(TARGET_SERVER) $(TARGET_CLIENT)

.PHONY: all clean