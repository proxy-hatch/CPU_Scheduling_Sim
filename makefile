CC=gcc
CFLAGS=-w -std=c11
PROG=simulation-app
OBJS= main.o list.o

simulation-app: $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c


clean:
	ls | grep -v list.o | grep .o | xargs rm