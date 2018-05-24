CFLAGS=-ggdb -c -Wall -O0
LIBS = -lpil -lm -pthread

all: tcomp

tcomp: main.o
	$(CC) main.o $(LIBS) -g -o tcomp

main.o: main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm *.o tcomp

