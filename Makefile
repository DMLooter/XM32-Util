CC = gcc
CFLAGS = -O3 -Wall -fmessage-length=0


build: compile
	$(CC) $(CFLAGS) M32UDP.o -o M32

compile: M32.h M32UDP.c
	$(CC) $(CFLAGS) -c M32UDP.c

clean:
	rm M32UDP.o M32

run: build
	./M32
