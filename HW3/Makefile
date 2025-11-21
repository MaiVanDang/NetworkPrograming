CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L -IUDP_Server

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): UDP_Server/server.c UDP_Server/validation.c UDP_Server/validation.h
	$(CC) $(CFLAGS) -o $(SERVER) UDP_Server/server.c UDP_Server/validation.c

$(CLIENT): UDP_Client/client.c UDP_Server/validation.c UDP_Server/validation.h
	$(CC) $(CFLAGS) -o $(CLIENT) UDP_Client/client.c UDP_Server/validation.c

clean:
	rm -f $(SERVER) $(CLIENT) *.o
