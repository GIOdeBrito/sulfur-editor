CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS = -lncurses

all: editor

editor: main.c
	$(CC) $(CFLAGS) -o editor main.c $(LDLIBS)

clean:
	rm -f editor
