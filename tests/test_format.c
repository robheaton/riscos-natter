/* test_format.c - tests of the formatting codes of IRC: the plain text, the runs of one style, colours, and the control characters that are dropped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "format.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static char *plain;
static fmtrun *runs;
static size_t n;
static void parse (const char *raw) { free (plain); free (runs); fmt_parse (raw, &plain, &runs, &n); }

int main (void)
{
  fmtrun st;
  parse ("hello world"); CHECK_STR (plain, "hello world"); CHECK_INT (n, 0); CHECK (runs == NULL);
  parse (""); CHECK_STR (plain, ""); CHECK_INT (n, 0);

  parse ("a\x02" "bold\x02 plain");									/* ^B toggles */
  CHECK_STR (plain, "abold plain"); CHECK_INT (n, 2);
  CHECK_INT (runs[0].off, 1); CHECK_INT (runs[0].flags, FMT_BOLD);
  CHECK_INT (runs[1].off, 5); CHECK_INT (runs[1].flags, 0);
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.flags, 0);
  fmt_style_at (runs, n, 1, &st); CHECK_INT (st.flags, FMT_BOLD);
  fmt_style_at (runs, n, 4, &st); CHECK_INT (st.flags, FMT_BOLD);
  fmt_style_at (runs, n, 5, &st); CHECK_INT (st.flags, 0);
  fmt_style_at (runs, n, 99, &st); CHECK_INT (st.flags, 0);

  parse ("\x1D" "i\x1F" "u\x16" "r\x1E" "s\x0F" "x");						/* italic, underline, reverse, strike, reset */
  CHECK_STR (plain, "iursx");
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.flags, FMT_ITALIC);
  fmt_style_at (runs, n, 1, &st); CHECK_INT (st.flags, FMT_ITALIC | FMT_UNDERLINE);
  fmt_style_at (runs, n, 2, &st); CHECK_INT (st.flags, FMT_ITALIC | FMT_UNDERLINE | FMT_REVERSE);
  fmt_style_at (runs, n, 3, &st); CHECK_INT (st.flags, FMT_ITALIC | FMT_UNDERLINE | FMT_REVERSE | FMT_STRIKE);
  fmt_style_at (runs, n, 4, &st); CHECK_INT (st.flags, 0);

  parse ("\x03" "4red\x03 normal");								/* ^C4 */
  CHECK_STR (plain, "red normal");
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, 4); CHECK_INT (st.bg, FMT_DEFAULT_COLOUR);
  fmt_style_at (runs, n, 3, &st); CHECK_INT (st.fg, FMT_DEFAULT_COLOUR);				/* a ^C with no digits puts the colours back */
  parse ("\x03" "04,12ab\x03" "7c"); CHECK_STR (plain, "abc");					/* two digits, and a background */
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, 4); CHECK_INT (st.bg, 12);
  fmt_style_at (runs, n, 2, &st); CHECK_INT (st.fg, 7); CHECK_INT (st.bg, 12);					/* a new foreground keeps the background */
  parse ("\x03" "1,x"); CHECK_STR (plain, ",x");							/* a comma with no digit after it is text */
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, 1);
  parse ("\x03" "123"); CHECK_STR (plain, "3"); fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, 12);	/* at most two digits */
  parse ("\x03" "99a\x03" "16b\x03" "5,99c");
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, FMT_DEFAULT_COLOUR);				/* 99 is the default; 16 and more are not supported: default */
  fmt_style_at (runs, n, 1, &st); CHECK_INT (st.fg, FMT_DEFAULT_COLOUR);
  fmt_style_at (runs, n, 2, &st); CHECK_INT (st.fg, 5); CHECK_INT (st.bg, FMT_DEFAULT_COLOUR);
  parse ("\x03" "5,3a\x03" "6b");									/* a new foreground keeps the background */
  fmt_style_at (runs, n, 1, &st); CHECK_INT (st.fg, 6); CHECK_INT (st.bg, 3);

  parse ("\x04" "FF8000hex\x04 x");								/* ^DRRGGBB */
  CHECK_STR (plain, "hex x");
  fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, FMT_RGB_FLAG | 0xFF8000);
  fmt_style_at (runs, n, 3, &st); CHECK_INT (st.fg, FMT_DEFAULT_COLOUR);
  parse ("\x04" "00FF00,102030z"); fmt_style_at (runs, n, 0, &st); CHECK_INT (st.fg, FMT_RGB_FLAG | 0x00FF00); CHECK_INT (st.bg, FMT_RGB_FLAG | 0x102030);
  parse ("\x04" "12G456x"); CHECK_STR (plain, "12G456x");						/* not six hex digits: only the code goes */
  CHECK_INT (fmt_rgb (FMT_RGB_FLAG | 0xABCDEF), 0xABCDEF); CHECK_INT (fmt_rgb (4), 0xFF0000); CHECK_INT (fmt_rgb (FMT_DEFAULT_COLOUR), -1); CHECK_INT (fmt_rgb (16), -1);

  parse ("a\x01\x07" "b\r\n\x1B[31mc\x7F\t" "d\x11" "e"); CHECK_STR (plain, "ab[31mc d" "e");	/* other controls go; a tab is a space; escape sequences lose their escape */
  CHECK_INT (n, 0);
  parse ("\x02\x02plain"); CHECK_STR (plain, "plain"); CHECK_INT (n, 0);			/* a style that is the default is no formatting */
  parse ("\x02\x03" "4\x0F" "x"); CHECK_STR (plain, "x"); CHECK_INT (n, 0);
  parse ("\xC3\xA9\x02" "\xC3\xA9"); CHECK_STR (plain, "\xC3\xA9\xC3\xA9"); CHECK_INT (runs[0].off, 2);	/* UTF-8 passes through */
  parse ("x\x02"); CHECK_STR (plain, "x");								/* a code at the very end */

  free (plain); free (runs);
  printf ("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
