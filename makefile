CC=gcc
CFLAGS=-Wall -g

all: main

main: cpu.o graphics.o main.c
	$(CC) $(CFLAGS) main.c cpu.o graphics.o -o main.out `sdl2-config --cflags --libs`

gpu.o: graphics.c graphics.h
	$(CC) $(CFLAGS) graphics.c graphics.h -c

cpu.o: cpu.c cpu.h
	$(CC) $(CFLAGS) cpu.c cpu.h -c

clean:
	rm -f *.o*