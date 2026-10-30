/* buffer.c - see buffer.h. */
#include <stdlib.h>
#include <string.h>
#include "buffer.h"

static void *xrealloc (void *p, size_t n) { p = realloc (p, n ? n : 1); if (!p) abort (); return p; }
static char *xstrdup (const char *s) { size_t n = strlen (s) + 1; char *r = xrealloc (NULL, n); memcpy (r, s, n); return r; }

/* ---- wrapping ---- */

/* the number of bytes of s[0..len) for one row of the given width: whole words while they fit; a word that is wider than the row is cut between characters; never 0 */
static size_t row_length (const char *s, size_t len, int width, int italic, measure_fn m, void *ud)
{
  size_t i = 0, last = 0, k;
  if (m (ud, italic, s, len) <= width) return len;
  while (i < len)
    {
      size_t j = i;
      while (j < len && s[j] != ' ') j++;
      if (m (ud, italic, s, j) <= width) last = j; else break;
      while (j < len && s[j] == ' ') j++;
      i = j;
    }
  if (last) return last;
  k = 1;								/* the first word is too wide: as many characters as fit, at least one */
  for (;;)
    {
      size_t next = k + 1;
      while (next < len && ((unsigned char) s[next] & 0xC0) == 0x80) next++;
      if (k >= len || next > len || m (ud, italic, s, next) > width) break;
      k = next;
    }
  while (k < len && ((unsigned char) s[k] & 0xC0) == 0x80) k++;		/* the first character may be longer than one byte */
  return k;
}

void layout_text (const char *text, int width, int italic, measure_fn m, void *ud, unsigned **starts, int *nrows)
{
  size_t len = strlen (text), pos = 0;
  unsigned *st = NULL;
  size_t n = 0, cap = 0;
  if (width <= 0 || !len)
    {
      st = xrealloc (NULL, sizeof *st); st[0] = 0;
      *starts = st; *nrows = 1;
      return;
    }
  while (pos < len)
    {
      size_t take;
      if (n == cap) { cap = cap ? cap * 2 : 4; st = xrealloc (st, cap * sizeof *st); }
      st[n++] = (unsigned) pos;
      take = row_length (text + pos, len - pos, width, italic, m, ud);
      pos += take;
      while (pos < len && text[pos] == ' ') pos++;
    }
  *starts = st; *nrows = (int) n;
}

/* ---- buffers ---- */

buffer *buffer_new (const char *name, int kind, int network)
{
  buffer *b = xrealloc (NULL, sizeof *b);
  memset (b, 0, sizeof *b);
  b->name = xstrdup (name); b->kind = kind; b->network = network;
  b->max_lines = 5000; b->drop_lines = 500;
  b->rowbase = xrealloc (NULL, sizeof *b->rowbase); b->rowbase[0] = 0;
  b->scroll_row = -1;
  b->log = -1;
  return b;
}

static void line_free (bline *l) { free (l->nick); free (l->text); free (l->starts); }

void buffer_free (buffer *b)
{
  size_t i;
  if (!b) return;
  for (i = 0; i < b->n; i++) line_free (&b->lines[i]);
  free (b->lines); free (b->rowbase); free (b->name); free (b->topic); free (b);
}

static void line_layout (buffer *b, bline *l, measure_fn m, void *ud)
{
  free (l->starts); l->starts = NULL;
  layout_text (l->text, b->layout_width, l->kind == LK_ACTION, m, ud, &l->starts, &l->nrows);
  l->width = b->layout_width;
}

static void rebuild_rowbase (buffer *b)
{
  size_t i, rows = 0;
  b->rowbase = xrealloc (b->rowbase, (b->n + 1) * sizeof *b->rowbase);
  for (i = 0; i < b->n; i++) { b->rowbase[i] = rows; rows += (size_t) b->lines[i].nrows; }
  b->rowbase[b->n] = rows;
}

size_t buffer_add (buffer *b, long when, int kind, const char *nick, const char *text, int colour, int highlight, int self, measure_fn m, void *ud)
{
  bline *l;
  size_t dropped = 0;
  if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 64; b->lines = xrealloc (b->lines, b->cap * sizeof *b->lines); }
  l = &b->lines[b->n];
  memset (l, 0, sizeof *l);
  l->when = when; l->kind = kind; l->nick = nick ? xstrdup (nick) : NULL; l->text = xstrdup (text);
  l->colour = colour; l->highlight = highlight; l->self = self;
  line_layout (b, l, m, ud);
  b->n++;
  if (b->n > b->max_lines)
    {
      size_t i, drop = b->drop_lines ? b->drop_lines : 1;
      if (drop > b->n) drop = b->n;
      for (i = 0; i < drop; i++) line_free (&b->lines[i]);
      memmove (b->lines, b->lines + drop, (b->n - drop) * sizeof *b->lines);
      b->n -= drop; dropped = drop;
      b->new_marker = b->new_marker > (int) drop ? b->new_marker - (int) drop : 0;
      rebuild_rowbase (b);
    }
  else
    {
      b->rowbase = xrealloc (b->rowbase, (b->n + 1) * sizeof *b->rowbase);
      b->rowbase[b->n] = b->rowbase[b->n - 1] + (size_t) l->nrows;
    }
  return dropped;
}

void buffer_relayout (buffer *b, int width, measure_fn m, void *ud)
{
  size_t i;
  b->layout_width = width;
  for (i = 0; i < b->n; i++) line_layout (b, &b->lines[i], m, ud);
  rebuild_rowbase (b);
}

size_t buffer_total_rows (const buffer *b) { return b->rowbase[b->n]; }

int buffer_find_row (const buffer *b, size_t row, size_t *line, int *rowinline)
{
  size_t lo = 0, hi = b->n;
  if (row >= b->rowbase[b->n]) return 0;
  while (hi - lo > 1)							/* the last line whose first row is <= row */
    {
      size_t mid = (lo + hi) / 2;
      if (b->rowbase[mid] <= row) lo = mid; else hi = mid;
    }
  *line = lo; *rowinline = (int) (row - b->rowbase[lo]);
  return 1;
}

void buffer_set_topic (buffer *b, const char *topic) { free (b->topic); b->topic = topic ? xstrdup (topic) : NULL; }

/* ---- nicks ---- */

static int fold (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c == '[' ? '{' : c == ']' ? '}' : c == '\\' ? '|' : c; }

int nick_colour (const char *nick)
{
  unsigned h = 5381;
  for (; *nick; nick++) h = h * 33u + (unsigned) fold ((unsigned char) *nick);
  return (int) (h % NICK_COLOURS);
}

static int nick_char (int c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '[' || c == ']' || c == '\\' || c == '`' || c == '^' || c == '{' || c == '|' || c == '}' || (unsigned char) c >= 0x80;
}

int mentions (const char *text, const char *nick)
{
  size_t nl = strlen (nick), i, k;
  if (!nl) return 0;
  for (i = 0; text[i]; i++)
    {
      if (i && nick_char ((unsigned char) text[i - 1])) continue;		/* only at the start of a word */
      for (k = 0; k < nl; k++) if (!text[i + k] || fold ((unsigned char) text[i + k]) != fold ((unsigned char) nick[k])) break;
      if (k == nl && !nick_char ((unsigned char) text[i + nl])) return 1;	/* ... and at its end ("bob_" is not "bob"; "bob:" and "bob," are) */
    }
  return 0;
}
