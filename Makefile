# Makefile for tcping and tcppingd

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE

all: tcpping tcppingd

tcpping: tcpping.c
	$(CC) $(CFLAGS) tcpping.c -o tcpping

tcppingd: tcppingd.c
	$(CC) $(CFLAGS) tcppingd.c -o tcppingd

clean:
	rm -f tcpping tcppingd

rebuild: clean all