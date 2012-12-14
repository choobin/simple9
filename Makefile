CC = gcc

CFLAGS = -Wall -Wextra -Werror -ansi -pedantic -O2

CPPFLAGS = -DNDEBUG

DEBUG = #-ggdb #-pg

all: test-simple9

test-simple9: test-simple9.o libsimple9.a
	$(CC) $(DEBUG) -o $@ $^

libsimple9.a: simple9.o
	$(AR) cr libsimple9.a simple9.o

%.o: %.c
	$(CC) $(DEBUG) $(CFLAGS) $(CPPFLAGS) -c $<

clean:
	-$(RM) -f *.o test-simple9 libsimple9.a >/dev/null 2>&1
