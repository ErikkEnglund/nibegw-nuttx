# Host unit tests for the nibegw frame parser: run "make" in this directory.

CC     ?= cc
CFLAGS += -std=c99 -Wall -Wextra -Werror -I..

test: test_proto
	./test_proto

test_proto: test_proto.c ../nibegw_proto.c ../nibegw_proto.h
	$(CC) $(CFLAGS) -o $@ test_proto.c ../nibegw_proto.c

clean:
	rm -f test_proto

.PHONY: test clean
