/* scroll.c - spike for the IRC client: can a Wimp window with our own redraw hold a chat scrollback on RISC OS 64?

   10,000 generated lines (time, nick, text) in proportional fonts with colours, a work area of 360,000 OS units for the scroll bar, a redraw loop that
   paints only the lines inside the clip rectangle, and live appends (click in the window to start and stop them).  Every fifth line carries six small squares,
   each a different way of filling a rectangle, to find out which of them show on the screen of RISC OS 64 (in the DeskLib64 slider test OS_Plot after
   Wimp_RedrawWindow drew nothing):
     a  DeskLib GFX_RectangleFill after ColourTrans_SetGCOL
     b  OS_SetColour + OS_Plot 4 + OS_Plot 101 (absolute), straight through SWI
     c  Wimp_PlotIcon of a filled icon (Wimp colour 11)
     d  Wimp_SetColour (11) + GFX_RectangleFill
     e  OS_SetColour + OS_Plot 96+5 (rectangle fill, relative)
     f  ColourTrans_SetGCOL + OS_Plot 0x65 (the same as b but with the DeskLib colour call)
   The log <Wimp$ScrapDir>.ircspike gets the times: building the lines, measuring all of them with Font_StringWidth, and each redraw. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeskLib:Core.h"
#include "DeskLib:Event.h"
#include "DeskLib:EventMsg.h"
#include "DeskLib:Handler.h"
#include "DeskLib:Wimp.h"
#include "DeskLib:WimpSWIs.h"
#include "DeskLib:Window.h"
#include "DeskLib:Font.h"
#include "DeskLib:ColourTran.h"
#include "DeskLib:GFX.h"
#include "DeskLib:SWI.h"
#include "DeskLib:Time.h"

#define MAXLINES 20000
#define NSTART 10000
#define LH 36				/* line height in OS units */
#define WIDTH 1500			/* work area width in OS units */
#define GUTTER 240			/* the squares */
#define TIMEX (GUTTER + 10)
#define NICKRIGHT (TIMEX + 210)		/* nicks are right-aligned to this x */
#define TEXTX (NICKRIGHT + 20)

typedef struct { char time[12]; char nick[20]; char text[240]; unsigned nickcol; int kind; } line_t;	/* kind: 0 text, 1 highlight, 2 join/part, 3 action */

static line_t *lines;
static int nlines;
static window_handle win;
static font_handle fnorm, fbold, fital;
static FILE *logf;
static int live;
static unsigned last_add;
static int redraws;

static const char *nicks[] = { "alice", "bob", "carol_", "dave|away", "eve", "frank", "g", "Heather", "ivan", "judith_the_long_nick" };
static const char *words[] = { "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "RISC", "OS", "64", "compiles", "Wimp", "window", "redraw", "scroll", "font", "http://example.org/a/long/url/path", "\xe2\x80\x93", "caf\xc3\xa9", "naive" };
static const unsigned palette[] = { 0x2020D000, 0x20A02000, 0xD0602000, 0x60208000, 0x00A0A000, 0xA0A00000, 0x00608000, 0x4040C000 };

static void say (const char *fmt, ...)
{
  va_list ap;
  if (!logf) return;
  va_start (ap, fmt); vfprintf (logf, fmt, ap); va_end (ap);
  fputc ('\n', logf); fflush (logf);
}

static void make_line (int i, line_t *l)
{
  int n = 4 + (i * 7) % 14, k;
  int nk = (i * 5 + i / 3) % 10;
  char *p = l->text;
  memset (l, 0, sizeof *l);
  snprintf (l->time, sizeof l->time, "%02d:%02d:%02d", (i / 3600) % 24, (i / 60) % 60, i % 60);
  strncpy (l->nick, nicks[nk], sizeof l->nick - 1);
  l->nickcol = palette[nk % 8];
  l->kind = (i % 11 == 0) ? 1 : (i % 17 == 0) ? 2 : (i % 13 == 0) ? 3 : 0;
  for (k = 0; k < n; k++)
  {
    const char *w = words[(i * 3 + k * 5) % (int) (sizeof words / sizeof *words)];
    if (p + strlen (w) + 2 >= l->text + sizeof l->text) break;
    p += sprintf (p, "%s%s", k ? " " : "", w);
  }
  if (l->kind == 2) { strcpy (l->text, "has joined #riscos64"); }
}

static int string_width (font_handle h, const char *s)		/* in OS units */
{
  font_string fs;
  memset (&fs, 0, sizeof fs);
  Font_SetFont (h);
  fs.s = DL_PTR (s); fs.x = 1 << 30; fs.y = 1 << 30; fs.split = -1; fs.term = (int) strlen (s);
  if (Font_StringWidth (&fs)) return 0;
  return fs.x / 400;
}

static void gcol (unsigned rgb) { ColourTrans_SetGCOL (rgb, 0, 0); }

static void paint (font_handle h, unsigned fg, unsigned bg, int flags, int x, int y, const char *s)
{
  ColourTrans_SetFontColours (h, bg, fg, 14);
  Font_Paint (s, font_OSCOORS | flags, x, y);
}

static void squares (int ox, int ytop)
{
  int x = ox + 8, y = ytop - LH + 6, s = 22, k = 0;
  icon_block ib;
  /* a */ gcol (0x0000FF00); GFX_RectangleFill (x + k * 30, y, s, s); k++;
  /* b */ SWI (2, 0, 0x61 /* OS_SetColour */, 0, 0x00FF0000); SWI (3, 0, 0x45, 4, x + k * 30, y); SWI (3, 0, 0x45, 101, x + k * 30 + s, y + s); k++;
  /* c */ memset (&ib, 0, sizeof ib);
  ib.workarearect.min.x = x + k * 30; ib.workarearect.min.y = y; ib.workarearect.max.x = x + k * 30 + s; ib.workarearect.max.y = y + s;
  ib.flags.value = (11u << 28) | 0x20; Wimp_PlotIcon (&ib); k++;
  /* d */ SWI (1, 0, 0x400E6 /* Wimp_SetColour */, 11); GFX_RectangleFill (x + k * 30, y, s, s); k++;
  /* e */ SWI (2, 0, 0x61, 0, 0xFF000000); SWI (3, 0, 0x45, 4, x + k * 30, y); SWI (3, 0, 0x45, 96 + 5, s, s); k++;
  /* f */ gcol (0xFF000000); SWI (3, 0, 0x45, 4, x + k * 30, y); SWI (3, 0, 0x45, 101, x + k * 30 + s, y + s); k++;
}

static void draw_line (int ox, int ytop, int i)
{
  line_t *l = &lines[i];
  int base = ytop - LH + 12, w;
  if (l->kind == 1)		/* a highlighted line: a pale yellow band (method a) behind the whole line */
  {
    gcol (0xC0FFFF00); GFX_RectangleFill (ox + GUTTER, ytop - LH, WIDTH - GUTTER, LH);
  }
  if (i % 5 == 0) squares (ox, ytop);
  paint (fnorm, 0x80808000, 0xFFFFFF00, 0, ox + TIMEX, base, l->time);
  if (l->kind == 2) { paint (fital, 0x80808000, 0xFFFFFF00, 0, ox + TEXTX, base, l->text); return; }
  w = string_width (fbold, l->nick);
  paint (fbold, l->nickcol, 0xFFFFFF00, 0, ox + NICKRIGHT - w - 40 + 40, base, l->nick);
  paint (l->kind == 3 ? fital : fnorm, 0x00000000, l->kind == 1 ? 0xC0FFFF00 : 0xFFFFFF00, 0, ox + TEXTX, base, l->text);
}

static BOOL redraw_handler (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  unsigned t0 = Time_Monotonic ();
  int drawn = 0;
  (void) reference;
  rb.window = event->data.openblock.window;
  Wimp_RedrawWindow (&rb, &more);
  while (more)
  {
    int ox = rb.rect.min.x - rb.scroll.x, oy = rb.rect.max.y - rb.scroll.y;
    int first = (oy - rb.cliprect.max.y) / LH, last = (oy - rb.cliprect.min.y) / LH, i;
    if (first < 0) first = 0;
    if (last >= nlines) last = nlines - 1;
    for (i = first; i <= last; i++) { draw_line (ox, oy - i * LH, i); drawn++; }
    Wimp_GetRectangle (&rb, &more);
  }
  redraws++;
  if (redraws <= 5 || redraws % 50 == 0) say ("redraw %d: %d lines in %u cs", redraws, drawn, Time_Monotonic () - t0);
  return TRUE;
}

static void set_extent (void)
{
  wimp_box b;
  b.min.x = 0; b.max.x = WIDTH; b.max.y = 0; b.min.y = -nlines * LH - 8;
  Wimp_SetExtent (win, &b);
}

static void add_line (void)
{
  window_state st;
  window_redrawblock rb;
  int at_bottom, visible;
  if (nlines >= MAXLINES) { live = 0; return; }
  Wimp_GetWindowState (win, &st);
  visible = st.openblock.screenrect.max.y - st.openblock.screenrect.min.y;
  at_bottom = (-st.openblock.scroll.y + visible) >= nlines * LH - 2 * LH;
  make_line (nlines, &lines[nlines]);
  nlines++;
  set_extent ();
  rb.window = win; rb.rect.min.x = 0; rb.rect.max.x = WIDTH; rb.rect.max.y = -(nlines - 1) * LH; rb.rect.min.y = -nlines * LH;
  Wimp_ForceRedraw (&rb);
  if (at_bottom)
  {
    st.openblock.scroll.y = -(nlines * LH + 8 - visible);
    if (st.openblock.scroll.y > 0) st.openblock.scroll.y = 0;
    Wimp_OpenWindow (&st.openblock);
  }
}

static BOOL null_handler (event_pollblock *event, void *reference)
{
  unsigned now = Time_Monotonic ();
  (void) event; (void) reference;
  if (live && now - last_add >= 20) { last_add = now; add_line (); }
  return TRUE;
}

static BOOL click_handler (event_pollblock *event, void *reference)
{
  (void) reference;
  if (event->data.mouse.button.data.select || event->data.mouse.button.data.adjust) { live = !live; say ("live appends %s", live ? "on" : "off"); }
  return TRUE;
}

static BOOL close_handler (event_pollblock *event, void *reference)
{
  (void) event; (void) reference;
  say ("closed by the user after %d redraws, %d lines", redraws, nlines);
  Event_CloseDown ();
  exit (0);
  return TRUE;
}

int main (void)
{
  window_block wb;
  unsigned t0, t1, t2;
  int i, total = 0;
  logf = fopen ("<Wimp$ScrapDir>.ircspike", "w");
  Event_Initialise ("IRCSpike");
  EventMsg_Initialise ();
  event_mask.data.null = 0;			/* we want null events for the live appends */
  lines = malloc (MAXLINES * sizeof *lines);
  if (!lines) return 1;
  t0 = Time_Monotonic ();
  for (i = 0; i < NSTART; i++) make_line (i, &lines[i]);
  nlines = NSTART;
  t1 = Time_Monotonic ();
  say ("built %d lines in %u cs", nlines, t1 - t0);
  /* "\\EUTF8" asks the Font Manager for the UTF-8 encoding (the strings of IRC are UTF-8) */
  if (Font_FindFont (&fnorm, "Homerton.Medium\\EUTF8", 11 * 16, 11 * 16, 0, 0)) { say ("no Homerton.Medium in UTF-8"); if (Font_FindFont (&fnorm, "Homerton.Medium", 11 * 16, 11 * 16, 0, 0)) return 1; }
  if (Font_FindFont (&fbold, "Homerton.Bold\\EUTF8", 11 * 16, 11 * 16, 0, 0)) fbold = fnorm;
  if (Font_FindFont (&fital, "Homerton.Medium.Oblique\\EUTF8", 11 * 16, 11 * 16, 0, 0)) fital = fnorm;
  t1 = Time_Monotonic ();
  for (i = 0; i < nlines; i++) total += string_width (fnorm, lines[i].text);
  t2 = Time_Monotonic ();
  say ("measured %d lines with Font_StringWidth in %u cs (sum of widths %d)", nlines, t2 - t1, total);
  memset (&wb, 0, sizeof wb);
  wb.screenrect.min.x = 100; wb.screenrect.min.y = 150; wb.screenrect.max.x = 1100; wb.screenrect.max.y = 800;
  wb.behind = -1;
  wb.flags.value = 0xBF000002u;			/* back, close, title, toggle, vertical scroll bar, size box; moveable; NOT auto redrawn: we redraw */
  wb.colours.cols.titlefore = 7; wb.colours.cols.titleback = 2; wb.colours.cols.workfore = 7; wb.colours.cols.workback = 0;
  wb.colours.cols.scrollouter = 3; wb.colours.cols.scrollinner = 1; wb.colours.cols.titlefocus = 12;
  wb.workarearect.min.x = 0; wb.workarearect.max.x = WIDTH; wb.workarearect.max.y = 0; wb.workarearect.min.y = -nlines * LH - 8;
  wb.titleflags.value = 0x19; wb.workflags.value = 0;
  wb.spritearea = 1;
  wb.minsize.x = 400; wb.minsize.y = 200;
  wb.numicons = 0;
  strcpy (wb.title.text, "IRC spike");
  Wimp_CreateWindow (&wb, &win);
  Event_Claim (event_REDRAW, win, event_ANY, redraw_handler, NULL);
  Event_Claim (event_CLICK, win, event_ANY, click_handler, NULL);
  Event_Claim (event_CLOSE, win, event_ANY, close_handler, NULL);
  Event_Claim (event_OPEN, event_ANY, event_ANY, Handler_OpenWindow, NULL);
  Event_Claim (event_NULL, event_ANY, event_ANY, null_handler, NULL);
  {
    window_openblock ob;
    memset (&ob, 0, sizeof ob);
    ob.window = win; ob.screenrect = wb.screenrect; ob.scroll.x = 0; ob.scroll.y = 0; ob.behind = -1;
    Wimp_OpenWindow (&ob);
  }
  say ("window open, entering the poll loop");
  for (;;) Event_Poll ();
  return 0;
}
