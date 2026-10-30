/* format.c - see format.h. */
#include <stdlib.h>
#include <string.h>
#include "format.h"

static const unsigned mirc[16] =
{ 0xFFFFFF, 0x000000, 0x00007F, 0x009300, 0xFF0000, 0x7F0000, 0x9C009C, 0xFC7F00, 0xFFFF00, 0x00FC00, 0x009393, 0x00FFFF, 0x0000FC, 0xFF00FF, 0x7F7F7F, 0xD2D2D2 };

long fmt_rgb (int colour)
{
  if (colour == FMT_DEFAULT_COLOUR) return -1;
  if (colour & FMT_RGB_FLAG) return colour & 0xFFFFFF;
  if (colour >= 0 && colour < 16) return (long) mirc[colour];
  return -1;
}

typedef struct { fmtrun cur; fmtrun *runs; size_t n, cap; } state;

static void push (state *s, size_t off)				/* the style changed at this place */
{
  if (s->n && s->runs[s->n - 1].off == off) { s->runs[s->n - 1] = s->cur; s->runs[s->n - 1].off = off; return; }
  if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 8; s->runs = realloc (s->runs, s->cap * sizeof *s->runs); if (!s->runs) abort (); }
  s->runs[s->n] = s->cur; s->runs[s->n].off = off; s->n++;
}

static int hexval (int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static int hex6 (const char *p)					/* six hex digits: the colour (with the flag), or -1 */
{
  int i, v = 0;
  for (i = 0; i < 6; i++) { int h = hexval ((unsigned char) p[i]); if (h < 0) return -1; v = v * 16 + h; }
  return v | FMT_RGB_FLAG;
}

void fmt_parse (const char *raw, char **plain, fmtrun **runs, size_t *nruns)
{
  size_t len = strlen (raw), n = 0, i = 0;
  char *out = malloc (len + 1);
  state s;
  if (!out) abort ();
  memset (&s, 0, sizeof s);
  s.cur.fg = s.cur.bg = FMT_DEFAULT_COLOUR;
  while (i < len)
    {
      unsigned char c = (unsigned char) raw[i++];
      switch (c)
	{
	case 0x02: s.cur.flags ^= FMT_BOLD; push (&s, n); break;
	case 0x1D: s.cur.flags ^= FMT_ITALIC; push (&s, n); break;
	case 0x1F: s.cur.flags ^= FMT_UNDERLINE; push (&s, n); break;
	case 0x16: s.cur.flags ^= FMT_REVERSE; push (&s, n); break;
	case 0x1E: s.cur.flags ^= FMT_STRIKE; push (&s, n); break;
	case 0x0F: s.cur.flags = 0; s.cur.fg = s.cur.bg = FMT_DEFAULT_COLOUR; push (&s, n); break;
	case 0x03:							/* ^C[fg[,bg]]: one or two digits each; nothing after it puts the colours back */
	  {
	    int fg = -2, bg = -2, k;
	    for (k = 0, fg = -2; k < 2 && i < len && raw[i] >= '0' && raw[i] <= '9'; k++, i++) fg = (fg < 0 ? 0 : fg) * 10 + (raw[i] - '0');
	    if (fg >= 0 && i + 1 < len && raw[i] == ',' && raw[i + 1] >= '0' && raw[i + 1] <= '9')
	      for (i++, k = 0, bg = -2; k < 2 && i < len && raw[i] >= '0' && raw[i] <= '9'; k++, i++) bg = (bg < 0 ? 0 : bg) * 10 + (raw[i] - '0');
	    if (fg < 0) s.cur.fg = s.cur.bg = FMT_DEFAULT_COLOUR;
	    else
	      {
		s.cur.fg = fg == 99 ? FMT_DEFAULT_COLOUR : fg < 16 ? fg : FMT_DEFAULT_COLOUR;
		if (bg >= 0) s.cur.bg = bg == 99 ? FMT_DEFAULT_COLOUR : bg < 16 ? bg : FMT_DEFAULT_COLOUR;
	      }
	    push (&s, n);
	    break;
	  }
	case 0x04:							/* ^DRRGGBB[,RRGGBB] */
	  {
	    int fg = i + 6 <= len ? hex6 (raw + i) : -1;
	    if (fg < 0) { s.cur.fg = s.cur.bg = FMT_DEFAULT_COLOUR; push (&s, n); break; }
	    i += 6; s.cur.fg = fg;
	    if (i + 7 <= len && raw[i] == ',') { int bg = hex6 (raw + i + 1); if (bg >= 0) { s.cur.bg = bg; i += 7; } }
	    push (&s, n);
	    break;
	  }
	case '\t': out[n++] = ' '; break;
	default:
	  if (c < 0x20 || c == 0x7F) break;				/* a control character: dropped (0x11 monospace, line breaks, bells ...) */
	  out[n++] = (char) c;
	}
    }
  out[n] = 0;
  *plain = out;
  {									/* a text whose runs are all the default style has none */
    size_t k;
    int plain_style = 1;
    for (k = 0; k < s.n; k++) if (s.runs[k].flags || s.runs[k].fg != FMT_DEFAULT_COLOUR || s.runs[k].bg != FMT_DEFAULT_COLOUR) plain_style = 0;
    if (plain_style) { free (s.runs); s.runs = NULL; s.n = 0; }
  }
  *runs = s.runs; *nruns = s.n;
}

void fmt_style_at (const fmtrun *runs, size_t nruns, size_t off, fmtrun *out)
{
  size_t lo = 0, hi = nruns;
  out->off = 0; out->flags = 0; out->fg = out->bg = FMT_DEFAULT_COLOUR;
  if (!nruns || runs[0].off > off) return;
  while (hi - lo > 1)							/* the last run that starts at or before off */
    {
      size_t mid = (lo + hi) / 2;
      if (runs[mid].off <= off) lo = mid; else hi = mid;
    }
  *out = runs[lo];
}
