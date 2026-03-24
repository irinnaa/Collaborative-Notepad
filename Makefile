# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -g

# Targets
all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server -lsqlite3 -pthread

client: client.c
	$(CC) $(CFLAGS) client.c -o client -lncurses

# Clean up compiled files
clean:
	rm -f server client
