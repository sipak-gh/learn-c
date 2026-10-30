# Makefile for mywc.
#
#   make             build mywc and test_wc
#   make test        build, then run the unit tests and test.sh
#   make clean       remove what make built
#
# For musl:  make clean && make CC=musl-gcc LDFLAGS=-static test

CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -pedantic

.PHONY: all test clean

all: mywc test_wc

mywc: mywc.o wc.o
	$(CC) $(LDFLAGS) -o $@ $^

test_wc: test_wc.o wc.o
	$(CC) $(LDFLAGS) -o $@ $^

mywc.o test_wc.o wc.o: wc.h

test: all
	./test_wc
	./test.sh

clean:
	rm -f mywc test_wc *.o test.in
