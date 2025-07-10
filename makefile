CC=clang
CFLAGS=-Wall -g

all: main

main: cpu.o graphics.o timer.o rom.o main.c
	$(CC) $(CFLAGS) -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib main.c cpu.o graphics.o timer.o rom.o -o main.out -lSDL2 -lSDL2main

gpu.o: graphics.c graphics.h
	$(CC) $(CFLAGS) graphics.c graphics.h -c

cpu.o: cpu.c cpu.h
	$(CC) $(CFLAGS) cpu.c cpu.h -c

timer.o: timer.c timer.h
	$(CC) $(CFLAGS) timer.c timer.h -c

rom.o: rom.c rom.h
	$(CC) $(CFLAGS) rom.c rom.h -c

.PHONY: clean

clean:
	rm -f *.o
	rm -f main.out
	rm -rf *.dSYM
	rm -f *.pch