/* select.c - see select.h. */
#include <stdlib.h>
#include <string.h>
#include "select.h"

static int before (selpos x, selpos y) { return x.line < y.line || (x.line == y.line && x.off < y.off); }

void sel_clear (selection *s) { s->active = 0; s->a.line = s->a.off = s->b.line = s->b.off = 0; }
int sel_empty (const selection *s) { return !s->active || (s->a.line == s->b.line && s->a.off == s->b.off); }

void sel_order (const selection *s, selpos *from, selpos *to)
{
  if (before (s->b, s->a)) { *from = s->b; *to = s->a; } else { *from = s->a; *to = s->b; }
}

void sel_set (selection *s, selpos a, selpos b) { s->active = 1; s->a = a; s->b = b; }
void sel_extend (selection *s, selpos to) { if (!s->active) sel_set (s, to, to); else s->b = to; }

void sel_shift (selection *s, size_t dropped)
{
  if (!s->active || !dropped) return;
  if (s->a.line < dropped || s->b.line < dropped) { sel_clear (s); return; }
  s->a.line -= dropped; s->b.line -= dropped;
}

/* ---- where a click is ---- */

static size_t row_end (const bline *l, int rowin)		/* the end of the row as drawn: the spaces at its end are not */
{
  size_t to = rowin + 1 < l->nrows ? l->starts[rowin + 1] : strlen (l->text);
  size_t from = l->starts[rowin];
  while (to > from && l->text[to - 1] == ' ') to--;
  return to;
}

static size_t next_char (const char *s, size_t i, size_t end)	/* the next place between characters (UTF-8) */
{
  i++;
  while (i < end && ((unsigned char) s[i] & 0xC0) == 0x80) i++;
  return i;
}

selpos sel_hit (const buffer *b, int row, int x, measure_fn m, void *ud)
{
  selpos p = { 0, 0 };
  size_t line, from, to, lo, hi;
  int rowin, italic;
  const bline *l;
  if (!b->n) return p;
  if (row < 0) return p;
  if (!buffer_find_row (b, (size_t) row, &line, &rowin)) { p.line = b->n - 1; p.off = strlen (b->lines[p.line].text); return p; }
  l = &b->lines[line];
  from = l->starts[rowin]; to = row_end (l, rowin);
  p.line = line; p.off = from;
  if (x <= 0 || to <= from) return p;
  italic = l->kind == LK_ACTION;
  if (m (ud, italic, l->text + from, to - from) <= x) { p.off = to; return p; }
  lo = from; hi = to;							/* the width at lo is <= x, at hi it is > x; narrow it down to two neighbouring places */
  while (next_char (l->text, lo, to) < hi)
    {
      size_t mid = lo + (hi - lo) / 2;
      while (mid > lo && ((unsigned char) l->text[mid] & 0xC0) == 0x80) mid--;
      if (mid <= lo) mid = next_char (l->text, lo, to);
      if (mid >= hi) break;
      if (m (ud, italic, l->text + from, mid - from) <= x) lo = mid; else hi = mid;
    }
  {
    int wl = lo > from ? m (ud, italic, l->text + from, lo - from) : 0, wh = m (ud, italic, l->text + from, hi - from);
    p.off = (x - wl) * 2 >= wh - wl ? hi : lo;				/* the nearer of the two */
  }
  return p;
}

/* ---- what is selected in a row ---- */

int sel_row (const selection *s, const buffer *b, size_t line, int rowin, size_t *from, size_t *to, int *prefix)
{
  selpos f, t, start = { 0, 0 };
  const bline *l;
  size_t rs, re, lo, hi, len;
  if (sel_empty (s) || line >= b->n) return 0;
  sel_order (s, &f, &t);
  if (line < f.line || line > t.line) return 0;
  l = &b->lines[line];
  len = strlen (l->text);
  start.line = line;
  if (prefix) *prefix = rowin == 0 && !before (start, f) && before (start, t);		/* f <= (line, 0) < t */
  rs = l->starts[rowin]; re = row_end (l, rowin);
  lo = line == f.line ? f.off : 0;
  hi = line == t.line ? t.off : len;
  if (lo < rs) lo = rs;
  if (hi > re) hi = re;
  if (hi <= lo) return 0;
  *from = lo; *to = hi;
  return 1;
}

/* ---- words ---- */

int sel_word (const buffer *b, selpos p, selpos *from, selpos *to)
{
  const char *t;
  size_t len, lo, hi;
  if (p.line >= b->n) return 0;
  t = b->lines[p.line].text; len = strlen (t);
  if (p.off > len) p.off = len;
  if (p.off < len && t[p.off] == ' ' && (p.off == 0 || t[p.off - 1] == ' ')) return 0;	/* in the middle of spaces */
  if (p.off == len && (len == 0 || t[len - 1] == ' ')) return 0;
  lo = p.off; hi = p.off;
  while (lo > 0 && t[lo - 1] != ' ') lo--;
  while (hi < len && t[hi] != ' ') hi++;
  while (lo < hi && strchr ("([{<\"'", t[lo])) lo++;				/* the punctuation round the word is not a part of it */
  while (hi > lo && strchr (".,;:!?)]}>\"'", t[hi - 1])) hi--;
  if (hi <= lo) return 0;
  from->line = to->line = p.line; from->off = lo; to->off = hi;
  return 1;
}

/* ---- copying ---- */

static void put (char **buf, size_t *n, size_t *cap, const char *s, size_t k)
{
  if (*n + k + 1 > *cap) { *cap = (*n + k + 1) * 2; *buf = realloc (*buf, *cap); if (!*buf) abort (); }
  memcpy (*buf + *n, s, k); *n += k; (*buf)[*n] = 0;
}

char *sel_text (const buffer *b, const selection *s, clock_fn clock)
{
  selpos f, t;
  char *out = NULL;
  size_t n = 0, cap = 0, line;
  if (sel_empty (s)) return NULL;
  sel_order (s, &f, &t);
  if (f.line >= b->n) return NULL;
  put (&out, &n, &cap, "", 0);
  for (line = f.line; line <= t.line && line < b->n; line++)
    {
      const bline *l = &b->lines[line];
      size_t len = strlen (l->text);
      size_t lo = line == f.line ? f.off : 0, hi = line == t.line ? t.off : len;
      if (lo > len) lo = len;
      if (hi > len) hi = len;
      if (line > f.line && line == t.line && hi == 0) break;				/* the selection ends at the very start of this line: nothing of it is selected */
      if (line > f.line) put (&out, &n, &cap, "\n", 1);
      if (lo == 0)									/* selected from its start: the time and the nick come with it */
	{
	  char ts[16] = "";
	  if (clock) clock (l->when, ts, sizeof ts);
	  if (ts[0]) { put (&out, &n, &cap, ts, strlen (ts)); put (&out, &n, &cap, " ", 1); }
	  switch (l->kind)
	    {
	    case LK_TEXT: if (l->nick) { put (&out, &n, &cap, "<", 1); put (&out, &n, &cap, l->nick, strlen (l->nick)); put (&out, &n, &cap, "> ", 2); } break;
	    case LK_NOTICE: if (l->nick) { put (&out, &n, &cap, "-", 1); put (&out, &n, &cap, l->nick, strlen (l->nick)); put (&out, &n, &cap, "- ", 2); } break;
	    case LK_ACTION: put (&out, &n, &cap, "* ", 2); if (l->nick) { put (&out, &n, &cap, l->nick, strlen (l->nick)); put (&out, &n, &cap, " ", 1); } break;
	    case LK_JOIN: put (&out, &n, &cap, "--> ", 4); break;
	    case LK_PART: case LK_QUIT: put (&out, &n, &cap, "<-- ", 4); break;
	    default: put (&out, &n, &cap, "-- ", 3); break;
	    }
	}
      put (&out, &n, &cap, l->text + lo, hi - lo);
    }
  return out;
}
