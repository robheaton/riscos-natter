/* select.h - selecting text in the conversation and copying it: where a selection starts and ends in the lines of a buffer, which part of each row of the window it covers, and the text that it copies.
   No Wimp calls: the width of text is measured by the function that the caller gives (as in buffer.h), and the time of a line is written by another. */
#ifndef NATTER_SELECT_H
#define NATTER_SELECT_H

#include <stddef.h>
#include "buffer.h"

typedef struct { size_t line; size_t off; } selpos;	/* a place between two characters: the line, and the byte in its text (the length of the text is the end of the line) */

typedef struct
{
  int active;			/* there is a selection (it may be empty) */
  selpos a, b;			/* where it began and where it is now: a can come after b */
} selection;

typedef void (*clock_fn) (long when, char *buf, size_t n);	/* the time of a line for the copy: "19:50" */

void sel_clear (selection *s);
int sel_empty (const selection *s);					/* nothing is selected (no selection, or both ends are the same place) */
void sel_order (const selection *s, selpos *from, selpos *to);	/* the two ends, the first one first */
void sel_set (selection *s, selpos a, selpos b);
void sel_extend (selection *s, selpos to);				/* moves the end that is not the anchor */
void sel_shift (selection *s, size_t dropped);			/* the first lines of the buffer were dropped: the selection moves with the text, or goes if it was in them */

/* The place at row number row of the window and at x units from the start of the text column (negative: before it, in the time and nick columns), for text drawn in the fonts of m. */
selpos sel_hit (const buffer *b, int row, int x, measure_fn m, void *ud);

/* The part of row rowin of line line that is selected, as byte offsets in the text of the line: returns 1 and sets *from and *to, or 0 if none of the row is.
   *prefix says that the time and the nick of the line (in front of its first row) are selected too: the selection starts at or before the start of the line and goes on past it. */
int sel_row (const selection *s, const buffer *b, size_t line, int rowin, size_t *from, size_t *to, int *prefix);

/* The word that is at p (what is between spaces, without the punctuation round it): returns 1 and sets *from and *to, or 0 if p is in a space. */
int sel_word (const buffer *b, selpos p, selpos *from, selpos *to);

/* The selected text as one string to be freed, lines separated by a line feed.  A line that is selected from its start has its time and nick in front, like
   "19:50 <robheaton> have you got a beer"; the rest of a line is just the text.  Returns NULL if nothing is selected. */
char *sel_text (const buffer *b, const selection *s, clock_fn clock);

#endif
