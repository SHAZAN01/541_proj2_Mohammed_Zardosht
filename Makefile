CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -pthread

.PHONY: all clean
all: a.out

a.out: buffer.c
	$(CC) $(CFLAGS) buffer.c -o a.out $(LDLIBS)

clean:
	rm -f a.out
