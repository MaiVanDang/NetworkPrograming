CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -pthread

# Server files
SERVER_DIR = TCP_Server
SERVER_SRC = $(SERVER_DIR)/server.c \
             $(SERVER_DIR)/protocol/protocol.c \
             $(SERVER_DIR)/auth/auth.c \
             $(SERVER_DIR)/user/user.c

# Client files
CLIENT_DIR = TCP_Client
CLIENT_SRC = $(CLIENT_DIR)/client.c \
             $(CLIENT_DIR)/protocol/protocol.c

# Target executables
SERVER_TARGET = server
CLIENT_TARGET = client

.PHONY: all clean help test

# Default target
all: $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "=========================================="
	@echo "Build completed successfully!"
	@echo "Server: ./$(SERVER_TARGET) <port>"
	@echo "Client: ./$(CLIENT_TARGET) <ip> <port>"
	@echo "=========================================="

# Build server
$(SERVER_TARGET): $(SERVER_SRC)
	@echo "Building server..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Server built: ./$(SERVER_TARGET)"

# Build client
$(CLIENT_TARGET): $(CLIENT_SRC)
	@echo "Building client..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Client built: ./$(CLIENT_TARGET)"

# Clean build artifacts
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
	rm -f *.o $(SERVER_DIR)/*.o $(CLIENT_DIR)/*.o
	rm -f $(SERVER_DIR)/*/*.o $(CLIENT_DIR)/*/*.o
