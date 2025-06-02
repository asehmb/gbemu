CC=gcc
CFLAGS=-Wall -g

all: cpu

cpu_fast: cpu_fast.c
	$(CC) $(CFLAGS) cpu_fast.c -o cpu_fast.out

cpu: cpu.c
	$(CC) $(CFLAGS) cpu.c -o cpu

clean:
	rm -f cpu