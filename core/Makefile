# Host build of the protocol core and its tests (Linux).  make test runs everything under AddressSanitizer and UBSan.
CC ?= cc
CFLAGS ?= -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -D_POSIX_C_SOURCE=200809L
SRC = irc_parse.c irc_conn.c
all: test_irc fuzz_irc
test_irc: $(SRC) irc.h ../tests/test_irc.c
	$(CC) $(CFLAGS) -I. $(SRC) ../tests/test_irc.c -o $@
fuzz_irc: $(SRC) irc.h ../tests/fuzz_irc.c
	$(CC) $(CFLAGS) -I. $(SRC) ../tests/fuzz_irc.c -o $@
test: all
	./test_irc
	./fuzz_irc 200000
clean:
	rm -f test_irc fuzz_irc
