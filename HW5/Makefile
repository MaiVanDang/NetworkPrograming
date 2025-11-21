CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L -IUDP_Server

all: server client

server: TCP_Server/server.c
	$(CC) $(CFLAGS) -o server TCP_Server/server.c

client: TCP_Client/client.c
	$(CC) $(CFLAGS) -o client TCP_Client/client.c

clean:
	rm -f server client *.o

.PHONY: all clean
