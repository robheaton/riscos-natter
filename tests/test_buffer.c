/* test_buffer.c - tests of the buffer model: wrapping, the row index, trimming, nick colours and mentions.  The width of text is faked: 10 units for each character. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buffer.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static int measure (void *ud, int italic, const char *s, size_t len)
{
  size_t i; int n = 0;
  (void) ud; (void) italic;
  for (i = 0; i < len; i++) if (((unsigned char) s[i] & 0xC0) != 0x80) n++;
  return n * 10;
}

static char rowbuf[16][256];
static const char *row_text (const char *text, unsigned *starts, int nrows, int k)	/* row k of the layout, without the spaces at its end */
{
  size_t from = starts[k], to = k + 1 < nrows ? starts[k + 1] : strlen (text);
  while (to > from && text[to - 1] == ' ') to--;
  memcpy (rowbuf[k & 15], text + from, to - from); rowbuf[k & 15][to - from] = 0;
  return rowbuf[k & 15];
}

static void lay (const char *text, int width, int *nrows, unsigned **starts) { layout_text (text, width, 0, measure, NULL, starts, nrows); }

static void test_wrap (void)
{
  unsigned *st; int n;
  lay ("short", 100, &n, &st); CHECK_INT (n, 1); CHECK_INT (st[0], 0); free (st);
  lay ("", 100, &n, &st); CHECK_INT (n, 1); free (st);					/* an empty text is one empty row */
  lay ("the quick brown fox jumps", 100, &n, &st);					/* 10 characters a row */
  CHECK_INT (n, 3); CHECK_STR (row_text ("the quick brown fox jumps", st, n, 0), "the quick"); CHECK_STR (row_text ("the quick brown fox jumps", st, n, 1), "brown fox"); CHECK_STR (row_text ("the quick brown fox jumps", st, n, 2), "jumps");
  free (st);
  lay ("aaaaaaaaaaaaaaaaaaaaaaaaa bb", 100, &n, &st);					/* a word of 25 characters is cut every 10; what is left of it shares the last row with the next word */
  CHECK_INT (n, 3); CHECK_STR (row_text ("aaaaaaaaaaaaaaaaaaaaaaaaa bb", st, n, 0), "aaaaaaaaaa"); CHECK_STR (row_text ("aaaaaaaaaaaaaaaaaaaaaaaaa bb", st, n, 1), "aaaaaaaaaa"); CHECK_STR (row_text ("aaaaaaaaaaaaaaaaaaaaaaaaa bb", st, n, 2), "aaaaa bb");
  free (st);
  lay ("one   two", 40, &n, &st); CHECK_INT (n, 2); CHECK_STR (row_text ("one   two", st, n, 1), "two");	/* 4 characters a row: the spaces between the words end the first row and never start the second */
  free (st);
  lay ("exactly ten", 100, &n, &st); CHECK_INT (n, 2); free (st);
  lay ("0123456789", 100, &n, &st); CHECK_INT (n, 1); free (st);				/* exactly the width */
  lay ("0123456789a", 100, &n, &st); CHECK_INT (n, 2); free (st);
  lay ("caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9", 90, &n, &st);				/* 9 characters: "café café" fits, UTF-8 counted as characters */
  CHECK_INT (n, 2); CHECK_STR (row_text ("caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9", st, n, 0), "caf\xc3\xa9 caf\xc3\xa9"); free (st);
  lay ("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 30, &n, &st);			/* a long word of two-byte characters is cut between characters */
  CHECK_INT (n, 2); CHECK_INT (st[1], 6); free (st);
  lay ("a b c", 5, &n, &st);								/* narrower than one character: one character a row, and it ends */
  CHECK_INT (n, 3); free (st);
  lay ("\xc3\xa9\xc3\xa9", 1, &n, &st); CHECK_INT (n, 2); CHECK_INT (st[1], 2); free (st);
  lay ("anything", 0, &n, &st); CHECK_INT (n, 1); free (st);				/* no width: no wrapping */
  lay ("  leading spaces", 100, &n, &st); CHECK_INT (n, 2); free (st);
}

static void test_buffer (void)
{
  buffer *b = buffer_new ("#chan", BK_CHANNEL, 0);
  size_t line; int r, i;
  b->layout_width = 100;
  buffer_add (b, 1000, LK_TEXT, "alice", "the quick brown fox jumps", 1, 0, 0, measure, NULL);	/* 3 rows */
  buffer_add (b, 1001, LK_TEXT, "bob", "hi", 2, 0, 0, measure, NULL);				/* 1 row */
  buffer_add (b, 1002, LK_JOIN, "carol", "joined", 3, 0, 0, measure, NULL);			/* 1 row */
  buffer_add (b, 1003, LK_ACTION, "dave", "waves at everybody here", 4, 0, 0, measure, NULL);	/* 3 rows */
  CHECK_INT (b->n, 4); CHECK_INT (buffer_total_rows (b), 8);
  CHECK (buffer_find_row (b, 0, &line, &r) && line == 0 && r == 0);
  CHECK (buffer_find_row (b, 2, &line, &r) && line == 0 && r == 2);
  CHECK (buffer_find_row (b, 3, &line, &r) && line == 1 && r == 0);
  CHECK (buffer_find_row (b, 4, &line, &r) && line == 2 && r == 0);
  CHECK (buffer_find_row (b, 7, &line, &r) && line == 3 && r == 2);
  CHECK (!buffer_find_row (b, 8, &line, &r));
  buffer_relayout (b, 200, measure, NULL);							/* wider: fewer rows */
  CHECK_INT (b->n, 4); CHECK_INT (buffer_total_rows (b), 6); CHECK_INT (b->lines[0].nrows, 2);
  CHECK (buffer_find_row (b, 4, &line, &r) && line == 3 && r == 0);
  buffer_relayout (b, 50, measure, NULL);
  CHECK (buffer_total_rows (b) > 8);
  for (i = 0; i < (int) buffer_total_rows (b); i++) { CHECK (buffer_find_row (b, (size_t) i, &line, &r) && r < b->lines[line].nrows); }
  buffer_set_topic (b, "a topic"); CHECK_STR (b->topic, "a topic"); buffer_set_topic (b, NULL); CHECK (b->topic == NULL);
  buffer_free (b);

  /* the oldest lines are dropped when there are too many */
  b = buffer_new ("#busy", BK_CHANNEL, 0);
  b->layout_width = 100; b->max_lines = 10; b->drop_lines = 4;
  { size_t dropped_total = 0;
    for (i = 0; i < 25; i++) { char t[32]; snprintf (t, sizeof t, "line %d", i); dropped_total += buffer_add (b, i, LK_TEXT, "x", t, 0, 0, 0, measure, NULL); CHECK (b->n <= 10); }
    CHECK_INT (dropped_total + b->n, 25); }
  CHECK_STR (b->lines[b->n - 1].text, "line 24"); CHECK_INT (buffer_total_rows (b), b->n);
  for (i = 0; i < (int) b->n; i++) { CHECK (buffer_find_row (b, (size_t) i, &line, &r) && (int) line == i); }
  buffer_free (b);
}

static void test_nicks (void)
{
  CHECK_INT (nick_colour ("Alice"), nick_colour ("alice")); CHECK_INT (nick_colour ("a[b]"), nick_colour ("A{B}"));
  CHECK (nick_colour ("alice") >= 0 && nick_colour ("alice") < NICK_COLOURS);
  { int seen[NICK_COLOURS] = { 0 }, i, used = 0; char n[16];
    for (i = 0; i < 200; i++) { snprintf (n, sizeof n, "user%d", i); seen[nick_colour (n)] = 1; }
    for (i = 0; i < NICK_COLOURS; i++) used += seen[i];
    CHECK_INT (used, NICK_COLOURS); }							/* all the colours get used */
  CHECK (mentions ("bob: hello", "bob")); CHECK (mentions ("hello Bob", "bob")); CHECK (mentions ("hey, bob!", "bob")); CHECK (mentions ("BOB", "bob"));
  CHECK (!mentions ("bobby is here", "bob")); CHECK (!mentions ("hello bob_", "bob")); CHECK (!mentions ("abob", "bob")); CHECK (!mentions ("", "bob")); CHECK (!mentions ("bob", ""));
  CHECK (mentions ("hi a[b]", "a[b]")); CHECK (mentions ("hi A{B}!", "a[b]"));
  CHECK (mentions ("caf\xc3\xa9: x", "caf\xc3\xa9")); CHECK (!mentions ("caf\xc3\xa9s", "caf"));	/* a UTF-8 letter continues a word */
}

static void test_format (void)
{
  buffer *b = buffer_new ("#f", BK_CHANNEL, 0);
  b->layout_width = 1000;
  buffer_add (b, 0, LK_TEXT, "bob", "a\x02" "bold\x02 \x03" "4red\x07", 0, 0, 0, measure, NULL);		/* formatting is kept as runs; the text is plain */
  CHECK_STR (b->lines[0].text, "abold red"); CHECK_INT (b->lines[0].nruns, 3);
  CHECK_INT (b->lines[0].nrows, 1);
  buffer_add (b, 0, LK_TEXT, "amy", "no formatting here", 0, 0, 0, measure, NULL);
  CHECK_STR (b->lines[1].text, "no formatting here"); CHECK_INT (b->lines[1].nruns, 0); CHECK (b->lines[1].runs == NULL);
  buffer_add (b, 0, LK_INFO, NULL, "\x1B[2Jevil\x08\x08", 0, 0, 0, measure, NULL);			/* no control character gets to the Font Manager */
  CHECK_STR (b->lines[2].text, "[2Jevil");
  buffer_free (b);
}

int main (void)
{
  test_format ();
  test_wrap ();
  test_buffer ();
  test_nicks ();
  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
