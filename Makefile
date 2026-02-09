# Makefile for tcping and tcppingd

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE

all: tcpping.exe tcppingd.exe

tcpping.exe: tcpping.c
	$(CC) $(CFLAGS) tcpping.c -o tcpping.exe

tcppingd.exe: tcppingd.c
	$(CC) $(CFLAGS) tcppingd.c -o tcppingd.exe

clean:
	rm -f tcpping.exe tcppingd.exe

rebuild: clean all
