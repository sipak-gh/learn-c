# Makefiles are explained in Chapter 9. Until then, the gcc commands are enough.
CFLAGS = -std=c99 -Wall -Wextra -pedantic

all: room room-musl

room: main.c percent.c
	gcc $(CFLAGS) -o room main.c percent.c

room-musl: main.c percent.c
	musl-gcc $(CFLAGS) -static -o room-musl main.c percent.c

clean:
	rm -f room room-musl
