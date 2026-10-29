# Host tests of the parts of the application that have no Wimp calls (Linux).
CC ?= cc
CFLAGS ?= -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer
test_buffer: buffer.c buffer.h ../tests/test_buffer.c
	$(CC) $(CFLAGS) -I. buffer.c ../tests/test_buffer.c -o $@
test: test_buffer test_settings
	./test_buffer
	./test_settings
clean:
	rm -f test_buffer test_settings
test_settings: settings.c settings.h ../tests/test_settings.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -I. settings.c ../tests/test_settings.c -o $@
