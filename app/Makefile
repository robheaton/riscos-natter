# Host tests of the parts of the application that have no Wimp calls (Linux).
CC ?= cc
CFLAGS ?= -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer
test_buffer: buffer.c buffer.h format.c format.h ../tests/test_buffer.c
	$(CC) $(CFLAGS) -I. buffer.c format.c ../tests/test_buffer.c -o $@
test: test_buffer test_settings test_logfile test_theme test_select test_format test_links
	./test_buffer
	./test_settings
	./test_logfile
	./test_theme
	./test_select
	./test_format
	./test_links
clean:
	rm -f test_buffer test_settings test_logfile test_theme test_select test_format test_links
test_settings: settings.c settings.h ../tests/test_settings.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -I. settings.c ../tests/test_settings.c -o $@
test_logfile: logfile.c logfile.h buffer.c buffer.h format.c format.h ../tests/test_logfile.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -I. logfile.c buffer.c format.c ../tests/test_logfile.c -o $@
test_theme: theme.c theme.h settings.c settings.h buffer.h format.h ../tests/test_theme.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -I. theme.c settings.c ../tests/test_theme.c -o $@
test_select: select.c select.h buffer.c buffer.h format.c format.h ../tests/test_select.c
	$(CC) $(CFLAGS) -I. select.c buffer.c format.c ../tests/test_select.c -o $@
test_format: format.c format.h ../tests/test_format.c
	$(CC) $(CFLAGS) -I. format.c ../tests/test_format.c -o $@
test_links: links.c links.h ../tests/test_links.c
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -I. links.c ../tests/test_links.c -o $@
