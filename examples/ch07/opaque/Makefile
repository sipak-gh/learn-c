CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic

counter_demo: main.c counter.c counter.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c counter.c

clean:
	rm -f counter_demo
