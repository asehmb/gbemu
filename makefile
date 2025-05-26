CC=clang
CFLAGS=-Wall -O2

all: cpu

cpu: cpu.c
	$(CC) $(CFLAGS) cpu.c -o cpu

clean:
	rm -f cpu