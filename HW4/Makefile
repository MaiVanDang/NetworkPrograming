CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L -IUDP_Server

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): TCP_Server/server.c
	$(CC) $(CFLAGS) -o $(SERVER) TCP_Server/server.c

$(CLIENT): TCP_Client/client.c
	$(CC) $(CFLAGS) -o $(CLIENT) TCP_Client/client.c

clean:
	rm -f $(SERVER) $(CLIENT) *.o
