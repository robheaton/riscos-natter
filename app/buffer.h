/* buffer.h - the text of one conversation (a channel, a query or the status of a network): lines, and how they are wrapped into rows for a window of a given width.
   No Wimp calls: the width of a piece of text is measured by a function that the caller gives (Font Manager in the application, a fake in the tests). */
#ifndef NATTER_BUFFER_H
#define NATTER_BUFFER_H

#include <stddef.h>

enum { LK_TEXT, LK_ACTION, LK_NOTICE, LK_INFO, LK_JOIN, LK_PART, LK_QUIT, LK_NICK, LK_TOPIC, LK_MODE, LK_ERROR };
enum { BK_STATUS, BK_CHANNEL, BK_QUERY };

/* the width in OS units of s[0..len), in the italic font if italic is set */
typedef int (*measure_fn) (void *ud, int italic, const char *s, size_t len);

typedef struct
{
  long when;			/* seconds since 1970, UTC */
  int kind;			/* LK_* */
  char *nick;			/* who it is from, or NULL */
  char *text;
  int colour;			/* the colour of the nick, 0 to NICK_COLOURS - 1 */
  int highlight;		/* it mentions our nick */
  int self;			/* we said it */
  int width, nrows;		/* the width that the rows were made for (0: not laid out) and how many there are */
  unsigned *starts;		/* the byte offset in text where each row starts */
} bline;

typedef struct buffer
{
  char *name;			/* "#riscos64", "bob", or the name of the network for its status */
  int kind;			/* BK_* */
  int network;			/* the index of the network it belongs to */
  bline *lines; size_t n, cap;
  size_t max_lines, drop_lines;	/* more than max_lines: the oldest drop_lines go */
  size_t *rowbase;		/* rowbase[i] is the number of the first row of line i; n + 1 entries, the last is the total */
  int layout_width;		/* the width that new lines are laid out for */
  int unread, highlights;	/* lines that arrived while the buffer was not shown */
  int new_marker;		/* the line before which the "new messages" mark is drawn (n = none); set when the buffer is left */
  char *topic;
  int scroll_row;		/* the first row that is shown, or -1 to follow the bottom */
} buffer;

#define NICK_COLOURS 8

buffer *buffer_new (const char *name, int kind, int network);
void buffer_free (buffer *b);
/* Adds a line, laid out for the current width.  Returns how many lines were dropped from the start (the row numbers of the other lines have shifted). */
size_t buffer_add (buffer *b, long when, int kind, const char *nick, const char *text, int colour, int highlight, int self, measure_fn m, void *ud);
void buffer_relayout (buffer *b, int width, measure_fn m, void *ud);	/* a new width: lay out all the lines again */
size_t buffer_total_rows (const buffer *b);
/* the line that holds row number row and which of its rows it is; returns 0 if there is no such row */
int buffer_find_row (const buffer *b, size_t row, size_t *line, int *rowinline);
void buffer_set_topic (buffer *b, const char *topic);

/* the wrapping of a text: fills *starts (to be freed) and *nrows */
void layout_text (const char *text, int width, int italic, measure_fn m, void *ud, unsigned **starts, int *nrows);

int nick_colour (const char *nick);			/* the same colour for the same nick, whatever the case */
int mentions (const char *text, const char *nick);	/* the nick is in the text as a word (case does not matter) */

#endif
