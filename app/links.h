/* links.h - finding web addresses in the text of a line.  No Wimp calls. */
#ifndef NATTER_LINKS_H
#define NATTER_LINKS_H

#include <stddef.h>

typedef struct { size_t from, to; } linkspan;		/* bytes [from, to) of the text */

/* The addresses in text (http://, https://, ftp://, ftps:// and www.), up to max, in order; returns how many.  The punctuation round an address is not a part of it. */
size_t links_find (const char *text, linkspan *out, size_t max);

/* The address of span s of text as it can be opened (www.example.org gets http:// in front); returns 1, or 0 if it does not fit in size bytes. */
int links_address (const char *text, const linkspan *s, char *buf, size_t size);

#endif
