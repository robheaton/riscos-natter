/* test_select.c - tests of the selection of text: where a click is, what a selection covers in each row, words, and the copied text.  The width of text is faked: 10 units for each character. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "select.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (!a_ || strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_ ? a_ : "(null)", b_); } } while (0)

static int measure (void *ud, int italic, const char *s, size_t len)
{
  size_t i; int n = 0;
  (void) ud; (void) italic;
  for (i = 0; i < len; i++) if (((unsigned char) s[i] & 0xC0) != 0x80) n++;
  return n * 10;
}

static void clock19 (long when, char *buf, size_t n) { snprintf (buf, n, "%02ld:%02ld", when / 60, when % 60); }	/* "when" is in minutes and seconds here */

static buffer *make (int width)
{
  buffer *b = buffer_new ("#test", BK_CHANNEL, 0);
  b->layout_width = width;
  return b;
}

static selpos pos (size_t line, size_t off) { selpos p; p.line = line; p.off = off; return p; }

static void test_basic (void)
{
  selection s;
  selpos f, t;
  sel_clear (&s);
  CHECK (sel_empty (&s));
  sel_set (&s, pos (2, 5), pos (1, 3));
  CHECK (!sel_empty (&s));
  sel_order (&s, &f, &t); CHECK_INT (f.line, 1); CHECK_INT (f.off, 3); CHECK_INT (t.line, 2); CHECK_INT (t.off, 5);
  sel_set (&s, pos (1, 4), pos (1, 4)); CHECK (sel_empty (&s));
  sel_extend (&s, pos (3, 0)); CHECK (!sel_empty (&s)); CHECK_INT (s.a.line, 1); CHECK_INT (s.b.line, 3);
  sel_clear (&s); sel_extend (&s, pos (4, 2)); CHECK (s.active); CHECK (sel_empty (&s));		/* extending nothing starts at that place */
  sel_set (&s, pos (5, 1), pos (7, 2)); sel_shift (&s, 3); CHECK_INT (s.a.line, 2); CHECK_INT (s.b.line, 4);
  sel_shift (&s, 3); CHECK (!s.active);								/* the start of it was dropped */
  sel_set (&s, pos (1, 1), pos (7, 2)); sel_shift (&s, 2); CHECK (!s.active);
}

static void test_hit (void)
{
  buffer *b = make (200);
  selpos p;
  buffer_add (b, 0, LK_TEXT, "bob", "hello world", 0, 0, 0, measure, NULL);			/* row 0 */
  buffer_add (b, 0, LK_TEXT, "amy", "aéb", 0, 0, 0, measure, NULL);				/* row 1: é is two bytes */
  p = sel_hit (b, 0, 0, measure, NULL); CHECK_INT (p.line, 0); CHECK_INT (p.off, 0);
  p = sel_hit (b, 0, -50, measure, NULL); CHECK_INT (p.off, 0);					/* in the time and nick columns */
  p = sel_hit (b, 0, 14, measure, NULL); CHECK_INT (p.off, 1);					/* the nearer side of the character */
  p = sel_hit (b, 0, 16, measure, NULL); CHECK_INT (p.off, 2);
  p = sel_hit (b, 0, 100, measure, NULL); CHECK_INT (p.off, 10);
  p = sel_hit (b, 0, 106, measure, NULL); CHECK_INT (p.off, 11);
  p = sel_hit (b, 0, 5000, measure, NULL); CHECK_INT (p.off, 11);				/* beyond the end of the text */
  p = sel_hit (b, 1, 5, measure, NULL); CHECK_INT (p.line, 1); CHECK_INT (p.off, 1);		/* ... "a|éb" */
  p = sel_hit (b, 1, 15, measure, NULL); CHECK_INT (p.off, 3);					/* never in the middle of é */
  p = sel_hit (b, 1, 26, measure, NULL); CHECK_INT (p.off, 4);
  p = sel_hit (b, 7, 30, measure, NULL); CHECK_INT (p.line, 1); CHECK_INT (p.off, 4);		/* below the last row: the end of the last line */
  p = sel_hit (b, -3, 30, measure, NULL); CHECK_INT (p.line, 0); CHECK_INT (p.off, 0);		/* above the first */
  buffer_free (b);

  b = make (100);										/* rows of ten characters */
  buffer_add (b, 0, LK_TEXT, "bob", "the quick brown fox jumps", 0, 0, 0, measure, NULL);	/* "the quick" / "brown fox" / "jumps" */
  p = sel_hit (b, 1, 30, measure, NULL); CHECK_INT (p.line, 0); CHECK_INT (p.off, 10 + 3);	/* "bro|wn": the second row starts at byte 10 */
  p = sel_hit (b, 1, -20, measure, NULL); CHECK_INT (p.off, 10);
  p = sel_hit (b, 2, 999, measure, NULL); CHECK_INT (p.off, 25);
  p = sel_hit (b, 0, 999, measure, NULL); CHECK_INT (p.off, 9);					/* the end of the first row, not the space that wraps */
  buffer_free (b);

  b = make (100);										/* an empty buffer */
  p = sel_hit (b, 0, 10, measure, NULL); CHECK_INT (p.line, 0); CHECK_INT (p.off, 0);
  buffer_free (b);
}

static void test_rows (void)
{
  buffer *b = make (100);
  selection s;
  size_t from, to;
  int prefix;
  buffer_add (b, 0, LK_TEXT, "bob", "the quick brown fox jumps", 0, 0, 0, measure, NULL);	/* line 0: three rows */
  buffer_add (b, 0, LK_TEXT, "amy", "second", 0, 0, 0, measure, NULL);				/* line 1 */
  buffer_add (b, 0, LK_TEXT, "bob", "third", 0, 0, 0, measure, NULL);				/* line 2 */

  sel_clear (&s);
  CHECK (!sel_row (&s, b, 0, 0, &from, &to, &prefix));
  sel_set (&s, pos (0, 4), pos (0, 13));								/* "quick bro" */
  CHECK (sel_row (&s, b, 0, 0, &from, &to, &prefix)); CHECK_INT (from, 4); CHECK_INT (to, 9); CHECK_INT (prefix, 0);	/* the first row ends at 9: the space is not drawn */
  CHECK (sel_row (&s, b, 0, 1, &from, &to, &prefix)); CHECK_INT (from, 10); CHECK_INT (to, 13); CHECK_INT (prefix, 0);
  CHECK (!sel_row (&s, b, 0, 2, &from, &to, &prefix));
  CHECK (!sel_row (&s, b, 1, 0, &from, &to, &prefix));

  sel_set (&s, pos (0, 0), pos (1, 3));								/* from the start of line 0 to "sec" */
  CHECK (sel_row (&s, b, 0, 0, &from, &to, &prefix)); CHECK_INT (from, 0); CHECK_INT (to, 9); CHECK_INT (prefix, 1);
  CHECK (sel_row (&s, b, 0, 1, &from, &to, &prefix)); CHECK_INT (prefix, 0);			/* only the first row has the time and the nick */
  CHECK (sel_row (&s, b, 0, 2, &from, &to, &prefix)); CHECK_INT (from, 20); CHECK_INT (to, 25);
  CHECK (sel_row (&s, b, 1, 0, &from, &to, &prefix)); CHECK_INT (from, 0); CHECK_INT (to, 3); CHECK_INT (prefix, 1);
  CHECK (!sel_row (&s, b, 2, 0, &from, &to, &prefix));

  sel_set (&s, pos (2, 3), pos (0, 15));							/* backwards, over the whole of line 1 */
  CHECK (sel_row (&s, b, 1, 0, &from, &to, &prefix)); CHECK_INT (from, 0); CHECK_INT (to, 6); CHECK_INT (prefix, 1);
  CHECK (sel_row (&s, b, 0, 1, &from, &to, &prefix)); CHECK_INT (from, 15); CHECK_INT (to, 19);
  CHECK (sel_row (&s, b, 2, 0, &from, &to, &prefix)); CHECK_INT (from, 0); CHECK_INT (to, 3); CHECK_INT (prefix, 1);

  sel_set (&s, pos (1, 0), pos (1, 0));								/* nothing */
  CHECK (!sel_row (&s, b, 1, 0, &from, &to, &prefix));
  buffer_free (b);
}

static void test_word (void)
{
  buffer *b = make (1000);
  selpos f, t;
  buffer_add (b, 0, LK_TEXT, "bob", "see https://example.org/x, (please) now", 0, 0, 0, measure, NULL);
  CHECK (sel_word (b, pos (0, 10), &f, &t)); CHECK_INT (f.off, 4); CHECK_INT (t.off, 25);		/* "https://example.org/x": the comma is not a part of it */
  CHECK (sel_word (b, pos (0, 4), &f, &t)); CHECK_INT (f.off, 4);					/* at the start of a word */
  CHECK (sel_word (b, pos (0, 25), &f, &t)); CHECK_INT (f.off, 4); CHECK_INT (t.off, 25);	/* at the end of a word, before the comma: that word */
  CHECK (sel_word (b, pos (0, 30), &f, &t)); CHECK_INT (f.off, 28); CHECK_INT (t.off, 34);	/* "(please)" is "please" */
  CHECK (sel_word (b, pos (0, 0), &f, &t)); CHECK_INT (f.off, 0); CHECK_INT (t.off, 3);
  CHECK (sel_word (b, pos (0, 38), &f, &t)); CHECK_INT (f.off, 36); CHECK_INT (t.off, 39);	/* the last word */
  buffer_free (b);
  b = make (1000);
  buffer_add (b, 0, LK_TEXT, "bob", "a  b", 0, 0, 0, measure, NULL);
  CHECK (!sel_word (b, pos (0, 2), &f, &t));							/* between two spaces */
  buffer_add (b, 0, LK_TEXT, "bob", "", 0, 0, 0, measure, NULL);
  CHECK (!sel_word (b, pos (1, 0), &f, &t));
  CHECK (!sel_word (b, pos (9, 0), &f, &t));
  buffer_free (b);
}

static void test_text (void)
{
  buffer *b = make (100);
  selection s;
  char *t;
  buffer_add (b, 70, LK_TEXT, "bob", "have you got a beer tonight", 0, 0, 0, measure, NULL);	/* 01:10 */
  buffer_add (b, 130, LK_ACTION, "amy", "waves", 0, 0, 0, measure, NULL);			/* 02:10 */
  buffer_add (b, 190, LK_JOIN, NULL, "cy joined", 0, 0, 0, measure, NULL);
  buffer_add (b, 250, LK_NOTICE, "serv", "hello", 0, 0, 0, measure, NULL);
  sel_clear (&s);
  CHECK (sel_text (b, &s, clock19) == NULL);
  sel_set (&s, pos (0, 5), pos (0, 8)); t = sel_text (b, &s, clock19); CHECK_STR (t, "you"); free (t);	/* inside a line: just the text */
  sel_set (&s, pos (0, 0), pos (0, 4)); t = sel_text (b, &s, clock19); CHECK_STR (t, "01:10 <bob> have"); free (t);	/* from its start: time and nick come too */
  sel_set (&s, pos (0, 5), pos (1, 3)); t = sel_text (b, &s, clock19); CHECK_STR (t, "you got a beer tonight\n02:10 * amy wav"); free (t);
  sel_set (&s, pos (3, 5), pos (1, 0)); t = sel_text (b, &s, clock19);
  CHECK_STR (t, "02:10 * amy waves\n03:10 --> cy joined\n04:10 -serv- hello"); free (t);
  sel_set (&s, pos (0, 28), pos (1, 0)); t = sel_text (b, &s, clock19); CHECK_STR (t, ""); free (t);	/* the end of line 0 to the start of line 1: only the break */
  sel_set (&s, pos (1, 0), pos (2, 0)); t = sel_text (b, &s, clock19); CHECK_STR (t, "02:10 * amy waves"); free (t);	/* ends at the start of line 2: nothing of that line */
  sel_set (&s, pos (0, 0), pos (0, 4)); t = sel_text (b, &s, NULL); CHECK_STR (t, "<bob> have"); free (t);	/* no clock */
  sel_set (&s, pos (9, 0), pos (9, 4)); CHECK (sel_text (b, &s, clock19) == NULL);		/* a selection that is not in the buffer */
  buffer_free (b);
}

int main (void)
{
  test_basic ();
  test_hit ();
  test_rows ();
  test_word ();
  test_text ();
  printf ("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
