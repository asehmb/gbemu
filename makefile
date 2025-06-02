CC=gcc
CFLAGS=-Wall -g

all: cpu

cpu: cpu.c
	$(CC) $(CFLAGS) cpu.c -o cpu

clean:
	rm -f cpu