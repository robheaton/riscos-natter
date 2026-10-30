/* natter.c - Natter, an IRC client for RISC OS 64.  The window layout of option A: one window with the scrollback, and panes around it: the buffer list on the left, the topic above, the
   nick list on the right and the input line below.  The protocol is in ../core; the text of the conversations is in buffer.c; demo.c is a pretend server for trying it without a network. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include "DeskLib:Core.h"
#include "DeskLib:Event.h"
#include "DeskLib:EventMsg.h"
#include "DeskLib:Handler.h"
#include "DeskLib:Wimp.h"
#include "DeskLib:WimpSWIs.h"
#include "DeskLib:Window.h"
#include "DeskLib:Icon.h"
#include "DeskLib:Font.h"
#include "DeskLib:ColourTran.h"
#include "DeskLib:GFX.h"
#include "DeskLib:SWI.h"
#include "DeskLib:Time.h"
#include "DeskLib:Pane2.h"
#include "DeskLib:Menu.h"
#include "DeskLib:Screen.h"
#include "DeskLib:Error.h"
#include "irc.h"
#include "buffer.h"
#include "demo.h"
#include "net.h"
#include "settings.h"
#include "logfile.h"
#include "choices.h"

/* ---- the geometry, in OS units ---- */
#define LH 36				/* a row of the scrollback */
#define LISTLH 36			/* a row of the buffer list and the nick list */
#define LEFT_W 300
#define RIGHT_W 250
#define TOP_H 44
#define BOT_H 52
#define PAD 12
#define TS_W 80				/* the time column */
#define NICK_W 210			/* the nick column, right aligned */
#define GAP 14
#define SB 44				/* the width of a scroll bar: the panes have one beside their work area, and it covers the window behind */
#define LEFT_EDGE (LEFT_W + SB)		/* the left pane and its scroll bar */
#define RIGHT_EDGE (RIGHT_W + SB)
#define TEXT_X (LEFT_EDGE + PAD + TS_W + NICK_W + GAP)	/* where the text starts, from the left edge of the window */
#define MIN_W (TEXT_X + RIGHT_EDGE + 340)		/* the narrowest window: the text column keeps room for about 40 characters */

/* ---- colours: 0xBBGGRR00 ---- */
#define COL_TEXT 0x00000000
#define COL_MUTED 0x80808000
#define COL_WHITE 0xFFFFFF00
#define COL_BAND 0xC0FFFF00
#define COL_SEL 0xFFE8D000
#define COL_RED 0x0000C800
#define COL_MARK 0x2020D000
#define COL_LISTBG 0xF0F0F000
static const unsigned nickcol[NICK_COLOURS] = { 0xC0600000, 0x50A03000, 0x2020D000, 0x00A0D000, 0xA0306000, 0x80808000, 0xA0A00000, 0x20208000 };

#define MAXBUF 64
#define MAXHIST 64

static window_handle win_main, win_topic, win_left, win_right, win_input;
static icon_handle ic_nick, ic_input;
/* font_handle is an unsigned char but Font_FindFont stores a whole word through its pointer (see MODIFICATIONS64.md of DeskLib64): each handle has a word of its own */
static unsigned int fnorm_w, fbold_w, fital_w;
#define fnorm ((font_handle) fnorm_w)
#define fbold ((font_handle) fbold_w)
#define fital ((font_handle) fital_w)
static irc_conn *conn;
static buffer *bufs[MAXBUF];
static int nbufs, cur;
static char nicklabel[48], inputbuf[512], titlebuf[96];
static int layout_width;			/* the width of the text column that the current buffer is laid out for */
static int win_width, main_h;			/* the visible width and height of the main window at the last check */
static char *history[MAXHIST]; static int nhist, hpos = -1;
static unsigned long generation;
static FILE *logf;
static net *srvlink;				/* the connection to a real server; NULL while the demonstration is the server */
static int demo_active = 1;
static int link_closed;				/* the net reported the end of the connection: it is freed by the idle loop */
static int want_connected;			/* the user wants to be connected: after a failure the connection is made again */
static unsigned reconnect_at, backoff_cs = 500;	/* when to try again (0: not waiting), and how long the next wait is: 5 s, doubling to 5 minutes */
static char rejoin[1024];			/* the channels that we were in when the connection ended */
static logger *lg;				/* the log files */
static char logdir[220];
static settings cfg_set;			/* the choices, from the file */
static netsettings cur_net;			/* the network that is in use (a copy: /connect host makes one that is not in the file) */
#define CHOICES_READ "Choices:Natter.Choices"
#define CHOICES_WRITE "<Choices$Write>.Natter.Choices"
static const char *current_nick (void) { return cur_net.nick[0] ? cur_net.nick : cfg_set.nick; }
#define CA_FILE "<Natter$Dir>.Resources.ca-bundle"

static char logpath[200];

static void dlog (const char *fmt, ...)
{
  va_list ap;
  if (!logf) return;
  va_start (ap, fmt); vfprintf (logf, fmt, ap); va_end (ap);
  fputc ('\n', logf); fflush (logf);
}
#define LOG dlog

/* the Pi holds back what is written to the NAS until the file is closed: close it and open it again, so that what has been logged so far can be read */
static void log_sync (void)
{
  if (!logf || !logpath[0]) return;
  fclose (logf);
  logf = fopen (logpath, "a");
}

/* ---- text and fonts ---- */

static int measure_cb (void *ud, int italic, const char *s, size_t len)
{
  font_string fs;
  char tmp[1024], *p = tmp;
  (void) ud;
  if (len >= sizeof tmp) p = malloc (len + 1);
  if (!p) return 0;
  memcpy (p, s, len); p[len] = 0;
  memset (&fs, 0, sizeof fs);
  Font_SetFont (italic ? fital : fnorm);
  fs.s = DL_PTR (p); fs.x = 1 << 30; fs.y = 1 << 30; fs.split = -1; fs.term = (int) len;
  if (Font_StringWidth (&fs)) fs.x = 0;
  if (p != tmp) free (p);
  return fs.x / 400;
}

static int bold_width (const char *s)
{
  font_string fs;
  memset (&fs, 0, sizeof fs);
  Font_SetFont (fbold);
  fs.s = DL_PTR (s); fs.x = 1 << 30; fs.y = 1 << 30; fs.split = -1; fs.term = (int) strlen (s);
  if (Font_StringWidth (&fs)) return 0;
  return fs.x / 400;
}

static void gcol (unsigned rgb) { ColourTrans_SetGCOL (rgb, 0, 0); }
static void fill (int x, int y, int w, int h, unsigned rgb) { gcol (rgb); GFX_RectangleFill (x, y, w, h); }

static void paint (font_handle f, unsigned fg, unsigned bg, int x, int y, const char *s, size_t len)
{
  char tmp[1024], *p = tmp;
  if (len >= sizeof tmp) len = sizeof tmp - 1;
  memcpy (p, s, len); p[len] = 0;
  if (!len) return;
  Font_SetFont (f);
  ColourTrans_SetFontColours (f, bg, fg, 14);
  Font_Paint (p, font_OSCOORS, x, y);
}

/* ---- time ---- */

static long tz_offset;					/* seconds east of UTC, from the territory */

static void read_timezone (void)
{
  int name = 0, cs = 0;
  if (SWI (0, 2, 0x43048 | XOS_Bit, &name, &cs) == NULL) tz_offset = cs / 100;		/* Territory_ReadCurrentTimeZone: R1 = centiseconds */
  LOG ("time zone offset %ld seconds", tz_offset);
}

static void clock_text (long when, char *buf, size_t n)
{
  time_t tt = (time_t) (when + tz_offset);
  struct tm *g = gmtime (&tt);
  if (g) snprintf (buf, n, "%02d:%02d", g->tm_hour, g->tm_min); else snprintf (buf, n, "--:--");
}

static long parse_time (const char *iso)		/* "2026-10-29T21:30:00.000Z" */
{
  int y, mo, d, h, mi, s;
  long days;
  if (sscanf (iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return (long) time (NULL);
  y -= mo <= 2;									/* days since 1970 (Howard Hinnant's algorithm) */
  { long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned) (y - era * 400), doy = (unsigned) ((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1), doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + (long) doe - 719468; }
  return days * 86400L + h * 3600L + mi * 60L + s;
}

/* ---- buffers ---- */

static void force_redraw (window_handle w)
{
  window_state st;
  window_redrawblock rb;
  Wimp_GetWindowState (w, &st);
  rb.window = w;
  rb.rect.min.x = st.openblock.scroll.x; rb.rect.max.x = st.openblock.scroll.x + (st.openblock.screenrect.max.x - st.openblock.screenrect.min.x);
  rb.rect.max.y = st.openblock.scroll.y; rb.rect.min.y = st.openblock.scroll.y - (st.openblock.screenrect.max.y - st.openblock.screenrect.min.y);
  Wimp_ForceRedraw (&rb);
}

static buffer *find_buffer (const char *name)
{
  int i;
  for (i = 0; i < nbufs; i++) if (bufs[i]->kind != BK_STATUS && !irc_conn_casecmp (conn, bufs[i]->name, name)) return bufs[i];
  return NULL;
}

static int buffer_index (const buffer *b) { int i; for (i = 0; i < nbufs; i++) if (bufs[i] == b) return i; return -1; }

static buffer *add_buffer (const char *name, int kind)
{
  buffer *b;
  if (nbufs >= MAXBUF) return bufs[0];
  b = buffer_new (name, kind, 0);
  b->layout_width = layout_width;
  bufs[nbufs++] = b;
  force_redraw (win_left);
  return b;
}

static buffer *get_buffer (const char *name, int kind)
{
  buffer *b = find_buffer (name);
  return b ? b : add_buffer (name, kind);
}

static int total_height (const buffer *b) { return TOP_H + (int) buffer_total_rows (b) * LH + BOT_H + 8; }

/* The Wimp lets the user drag a window no bigger than its work area, so the work area of the main window is never smaller than the screen; the scrolling is held to the real content. */
static int content_height (void) { return total_height (bufs[cur]); }

static void set_main_extent (void)
{
  wimp_box ext;
  int h = content_height (), w = win_width;
  Screen_CacheModeInfo ();
  if (h < screen_size.y) h = screen_size.y;
  if (h < main_h) h = main_h;
  if (w < screen_size.x) w = screen_size.x;
  ext.min.x = 0; ext.max.x = w; ext.max.y = 0; ext.min.y = -h;
  Wimp_SetExtent (win_main, &ext);
}

static void scroll_main_to_bottom (void)
{
  window_state st;
  int vis;
  Wimp_GetWindowState (win_main, &st);
  if (!st.flags.data.open) return;						/* the window is hidden: opening it here would show it without its panes */
  vis = st.openblock.screenrect.max.y - st.openblock.screenrect.min.y;
  st.openblock.scroll.y = total_height (bufs[cur]) > vis ? vis - total_height (bufs[cur]) : 0;
  Wimp_OpenWindow (&st.openblock);
}

static int main_at_bottom (void)
{
  window_state st;
  int vis;
  Wimp_GetWindowState (win_main, &st);
  vis = st.openblock.screenrect.max.y - st.openblock.screenrect.min.y;
  return -st.openblock.scroll.y + vis >= total_height (bufs[cur]) - 2 * LH;
}

static void set_title (void)
{
  buffer *b = bufs[cur];
  const char *net = irc_conn_network (conn) ? irc_conn_network (conn) : "Natter";
  char t[96];
  if (b->kind == BK_STATUS) snprintf (t, sizeof t, "%s", net);
  else snprintf (t, sizeof t, "%s: %s", net, b->name);
  Window_SetTitle (win_main, t);
}

/* the nick list of the channel that is shown: the members, sorted by prefix and then by nick */
typedef struct { const irc_user *user; unsigned prefix; } nickrow;
static nickrow *nicks; static size_t nnicks, capnicks; static int nicks_dirty = 1;

static int nick_order (const void *a, const void *b)
{
  const nickrow *x = a, *y = b;
  int rx = 99, ry = 99, i;
  for (i = 0; i < 31; i++) { if (x->prefix & (1u << i)) { rx = i; break; } }
  for (i = 0; i < 31; i++) { if (y->prefix & (1u << i)) { ry = i; break; } }
  if (rx != ry) return rx - ry;
  return irc_casecmp (x->user->nick, y->user->nick, irc_conn_casemap (conn));
}

static void rebuild_nicks (void)
{
  const irc_channel *ch = bufs[cur]->kind == BK_CHANNEL ? irc_conn_channel (conn, bufs[cur]->name) : NULL;
  size_t i, n = ch ? irc_channel_nmembers (ch) : 0;
  nnicks = 0;
  if (n > capnicks) { capnicks = n + 64; nicks = realloc (nicks, capnicks * sizeof *nicks); if (!nicks) abort (); }
  for (i = 0; i < n; i++)
    {
      const irc_member *m = irc_channel_member (conn, ch, i);
      nicks[nnicks].user = m->user; nicks[nnicks].prefix = m->prefix; nnicks++;
    }
  qsort (nicks, nnicks, sizeof *nicks, nick_order);
  nicks_dirty = 0;
}

static void set_pane_extent (window_handle w, int rows, int width)
{
  wimp_box ext;
  int h = rows * LISTLH + 8;
  if (h < main_h - TOP_H) h = main_h - TOP_H;				/* the Wimp makes a window no taller than its work area: never less than the visible height */
  ext.min.x = 0; ext.max.x = width; ext.max.y = 0; ext.min.y = -h;
  Wimp_SetExtent (w, &ext);
}

static void refresh_lists (void)
{
  set_pane_extent (win_left, nbufs + 1, LEFT_W);
  force_redraw (win_left);
  if (nicks_dirty) rebuild_nicks ();
  set_pane_extent (win_right, (int) nnicks + 1, RIGHT_W);
  force_redraw (win_right);
  force_redraw (win_topic);
}

static void relayout_current (void)
{
  buffer_relayout (bufs[cur], layout_width, measure_cb, NULL);
  set_main_extent ();
}

static void select_buffer (int i)
{
  buffer *old = bufs[cur], *b;
  LOG ("select buffer %d", i);
  if (i < 0 || i >= nbufs) return;
  old->new_marker = (int) old->n;
  cur = i; b = bufs[cur];
  if (b->layout_width != layout_width) buffer_relayout (b, layout_width, measure_cb, NULL);
  if (!b->unread) b->new_marker = 0;					/* nothing new: no mark */
  b->unread = 0; b->highlights = 0;
  nicks_dirty = 1;
  set_title ();
  set_main_extent ();
  scroll_main_to_bottom ();
  force_redraw (win_main);
  refresh_lists ();
  Icon_SetCaret (win_input, ic_input);
  LOG ("selected buffer %d: %s", cur, bufs[cur]->name);
  log_sync ();
}

static int log_enabled (const buffer *b)
{
  if (b->log >= 0) return b->log;
  if (cur_net.log >= 0) return cur_net.log;
  return cfg_set.log;
}

/* a line for a buffer */
static void add_line (buffer *b, long when, int kind, const char *nick, const char *text, int self, int highlight)
{
  int colour = nick ? nick_colour (nick) : 0;
  int shown = (buffer_index (b) == cur);
  int follow = shown && main_at_bottom ();
  buffer_add (b, when, kind, nick, text, colour, highlight, self, measure_cb, NULL);
  if (lg && !demo_active && log_enabled (b))
    {
      char t[1300];
      log_format (t, sizeof t, when, tz_offset, kind, nick, text);
      logger_add (lg, cur_net.name[0] ? cur_net.name : "Natter", b->kind, b->name, t);
    }
  if (shown)
    {
      set_main_extent ();
      if (follow) scroll_main_to_bottom ();
      force_redraw (win_main);
    }
  else if (kind == LK_TEXT || kind == LK_ACTION || kind == LK_NOTICE)
    {
      b->unread++;
      if (highlight) b->highlights++;
      force_redraw (win_left);
    }
}

static void status_line (int kind, const char *fmt, ...)
{
  char t[1024];
  va_list ap;
  va_start (ap, fmt); vsnprintf (t, sizeof t, fmt, ap); va_end (ap);
  add_line (bufs[0], (long) time (NULL), kind, NULL, t, 0, 0);
}

/* JOIN for a list of channels separated by commas, in lines that are not too long */
static void join_list (const char *list)
{
  char chunk[400];
  size_t n = 0;
  const char *p = list;
  while (*p)
    {
      size_t l = strcspn (p, ",");
      if (l && l < 200)
	{
	  if (n && n + 1 + l > sizeof chunk - 1) { chunk[n] = 0; irc_join (conn, chunk, NULL); n = 0; }
	  if (n) chunk[n++] = ',';
	  memcpy (chunk + n, p, l); n += l;
	}
      p += l;
      if (*p == ',') p++;
    }
  if (n) { chunk[n] = 0; irc_join (conn, chunk, NULL); }
}

static void schedule_reconnect (void);

/* ---- the events of the core ---- */

static void chan_line (const irc_event *e, long when, int kind, const char *fmt, ...)
{
  char t[1024];
  va_list ap;
  buffer *b = e->target && *e->target ? find_buffer (e->target) : NULL;
  va_start (ap, fmt); vsnprintf (t, sizeof t, fmt, ap); va_end (ap);
  if (!b) b = bufs[0];
  add_line (b, when, kind, e->from, t, e->self, 0);
}

static void on_event (void *ud, const irc_event *e)
{
  long when = e->time ? parse_time (e->time) : (long) time (NULL);
  const char *own = irc_conn_nick (conn);
  char t[1100];
  (void) ud;
  switch (e->type)
    {
    case IRC_EV_CONNECTED:
      status_line (LK_INFO, "Connected to %s: %s", e->from ? e->from : "the server", e->text);
      snprintf (nicklabel, sizeof nicklabel, "%s", own);
      Icon_SetText (win_input, ic_nick, nicklabel);
      set_title ();
      backoff_cs = 500;
      if (demo_active) { irc_join (conn, "#riscos64", NULL); irc_join (conn, "#riscosdev", NULL); }
      else if (rejoin[0]) { join_list (rejoin); rejoin[0] = 0; }
      else if (cur_net.autojoin[0]) join_list (cur_net.autojoin);
      break;
    case IRC_EV_PRIVMSG: case IRC_EV_NOTICE: case IRC_EV_ACTION:
      {
	int kind = e->type == IRC_EV_NOTICE ? LK_NOTICE : e->type == IRC_EV_ACTION ? LK_ACTION : LK_TEXT;
	buffer *b;
	int hl = 0;
	const char *text = e->text;
	if (irc_is_channel_name (conn, e->target)) b = find_buffer (e->target);
	else if (!e->user && e->type == IRC_EV_NOTICE) b = bufs[0];				/* a notice from a server */
	else b = get_buffer (e->self ? e->target : (e->from ? e->from : "?"), BK_QUERY);
	if (!b) b = bufs[0];
	if (!e->self && e->type != IRC_EV_NOTICE) hl = mentions (e->text, own) || b->kind == BK_QUERY;
	if (e->type == IRC_EV_ACTION) { snprintf (t, sizeof t, "%s %s", e->from, e->text); text = t; }
	add_line (b, when, kind, e->from, text, e->self, hl);
      }
      break;
    case IRC_EV_JOIN:
      if (e->self)
	{
	  buffer *b = get_buffer (e->target, BK_CHANNEL);
	  b->joined = 1;
	  add_line (b, when, LK_JOIN, NULL, "You have joined the channel", 1, 0);
	  if (cur == 0) select_buffer (buffer_index (b));
	}
      else chan_line (e, when, LK_JOIN, "%s (%s@%s) has joined", e->from, e->user ? e->user : "?", e->host ? e->host : "?");
      nicks_dirty = 1;
      break;
    case IRC_EV_PART:
      if (e->self && e->target) { buffer *pb = find_buffer (e->target); if (pb) pb->joined = 0; }
      chan_line (e, when, LK_PART, "%s has left%s%s", e->self ? "You" : e->from, e->text && *e->text ? ": " : "", e->text ? e->text : "");
      nicks_dirty = 1;
      break;
    case IRC_EV_QUIT:
      chan_line (e, when, LK_QUIT, "%s has quit%s%s", e->from, e->text && *e->text ? ": " : "", e->text ? e->text : "");
      nicks_dirty = 1;
      break;
    case IRC_EV_NICK:
      chan_line (e, when, LK_NICK, e->self ? "You are now known as %s" : "%s is now known as %s", e->self ? e->arg : e->from, e->arg);
      if (e->self) { snprintf (nicklabel, sizeof nicklabel, "%s", e->arg); Icon_SetText (win_input, ic_nick, nicklabel); }
      nicks_dirty = 1;
      break;
    case IRC_EV_KICK:
      if (e->self && e->target) { buffer *kb = find_buffer (e->target); if (kb) kb->joined = 0; }
      chan_line (e, when, LK_PART, "%s was kicked by %s%s%s", e->self ? "You" : e->arg, e->from, e->text && *e->text ? ": " : "", e->text ? e->text : "");
      nicks_dirty = 1;
      break;
    case IRC_EV_TOPIC:
      {
	buffer *b = e->target ? find_buffer (e->target) : NULL;
	if (b) buffer_set_topic (b, e->text);
	if (e->numeric == 332) chan_line (e, when, LK_TOPIC, "The topic is: %s", e->text);
	else chan_line (e, when, LK_TOPIC, "%s has changed the topic to: %s", e->from, e->text);
	if (b == bufs[cur]) force_redraw (win_topic);
      }
      break;
    case IRC_EV_MODE:
      chan_line (e, when, LK_MODE, "%s sets mode %s", e->from ? e->from : "The server", e->text);
      nicks_dirty = 1;
      break;
    case IRC_EV_NAMES: case IRC_EV_ACCOUNT: case IRC_EV_AWAY: case IRC_EV_CHGHOST: case IRC_EV_SETNAME:
      nicks_dirty = 1;
      break;
    case IRC_EV_INVITE: status_line (LK_INFO, "%s invites you to %s", e->from, e->target); break;
    case IRC_EV_CTCP: status_line (LK_INFO, "CTCP %s %s from %s", e->reply ? "reply" : "request", e->arg, e->from ? e->from : "?"); break;
    case IRC_EV_SASL_OK: status_line (LK_INFO, "Logged in"); break;
    case IRC_EV_SASL_FAIL: status_line (LK_ERROR, "The login failed: %s", e->text); break;
    case IRC_EV_ERROR: status_line (LK_ERROR, "The server closed the connection: %s", e->text); break;
    case IRC_EV_TIMEOUT:
      status_line (LK_ERROR, "No answer from the server for a long time");
      if (srvlink) { link_closed = 1; if (want_connected) schedule_reconnect (); }
      break;
    case IRC_EV_NUMERIC:
      status_line (e->numeric >= 400 && e->numeric < 600 ? LK_ERROR : LK_INFO, "%s", e->text);
      break;
    default: break;
    }
}

/* ---- sending: the demo server is the transport ---- */

static void feed_from_server (void *ud, const char *lines)
{
  (void) ud;
  generation++;
  irc_conn_input (conn, lines, strlen (lines), Time_Monotonic ());
}

static void pump_output (void)
{
  char out[8192];
  size_t n;
  if (!demo_active)
    {
      while (srvlink && net_connected (srvlink) && net_pending (srvlink) < 16384 && (n = irc_conn_output (conn, Time_Monotonic (), out, sizeof out)) > 0)
	if (net_send (srvlink, out, n) != 0) break;
      return;
    }
  while ((n = irc_conn_output (conn, Time_Monotonic (), out, sizeof out)) > 0)
    {
      char *p = out;
      out[n] = 0;
      while (*p)
	{
	  char *eol = strstr (p, "\r\n");
	  if (!eol) break;
	  *eol = 0;
	  demo_from_client (p);
	  p = eol + 2;
	}
    }
}

/* ---- a connection to a real server ---- */

static void net_status_cb (void *ud, const char *text) { (void) ud; status_line (LK_INFO, "%s", text); }

static void net_connected_cb (void *ud)
{
  (void) ud;
  status_line (LK_INFO, "Connected: logging in as %s", current_nick ());
  irc_conn_start (conn, Time_Monotonic ());
  pump_output ();
}

static void net_data_cb (void *ud, const char *buf, size_t n)
{
  (void) ud;
  generation++;
  irc_conn_input (conn, buf, n, Time_Monotonic ());
}

static void net_closed_cb (void *ud, const char *why)
{
  (void) ud;
  status_line (LK_ERROR, "%s", why);
  link_closed = 1;
  if (!want_connected) return;
  if (!strncmp (why, "the certificate", 15)) { want_connected = 0; status_line (LK_ERROR, "Not trying again: check the server name and port in your choices, and that your connection is not being intercepted"); }
  else schedule_reconnect ();
}

/* the channels that we are in, and a line in each of them that says that the connection is gone */
static void schedule_reconnect (void)
{
  char list[sizeof rejoin];
  int i;
  size_t n = 0;
  list[0] = 0;
  for (i = 1; i < nbufs; i++)
    if (bufs[i]->kind == BK_CHANNEL && bufs[i]->joined)
      {
	size_t l = strlen (bufs[i]->name);
	if (n + l + 2 < sizeof list) { if (n) list[n++] = ','; memcpy (list + n, bufs[i]->name, l); n += l; list[n] = 0; }
	add_line (bufs[i], (long) time (NULL), LK_ERROR, NULL, "The connection to the server has ended", 0, 0);
	bufs[i]->joined = 0;
      }
  if (list[0]) snprintf (rejoin, sizeof rejoin, "%s", list);			/* a later failed attempt must not forget them */
  reconnect_at = Time_Monotonic () + backoff_cs;
  if (!reconnect_at) reconnect_at = 1;
  status_line (LK_INFO, "Trying again in %u seconds", backoff_cs / 100);
  backoff_cs = backoff_cs * 2 > 30000 ? 30000 : backoff_cs * 2;
}

static irc_config make_config (void)
{
  irc_config cfg;
  memset (&cfg, 0, sizeof cfg);
  cfg.nick = current_nick ();
  cfg.user = cur_net.user[0] ? cur_net.user : cfg_set.user;
  cfg.realname = cur_net.realname[0] ? cur_net.realname : cfg_set.realname;
  cfg.ctcp_version = "Natter 0.1 (RISC OS 64)";
  if (cur_net.sasl_user[0] && cur_net.sasl_pass[0]) { cfg.sasl_user = cur_net.sasl_user; cfg.sasl_pass = cur_net.sasl_pass; }
  if (cur_net.password[0]) cfg.pass = cur_net.password;
  return cfg;
}

static void stop_link (void)			/* the connection goes; whether it should come back is another matter */
{
  if (srvlink) { if (net_connected (srvlink)) { irc_quit (conn, "Natter"); pump_output (); } net_free (srvlink); srvlink = NULL; }
  link_closed = 0;
}

static void drop_link (void)			/* the user ends the connection: nothing is tried again */
{
  want_connected = 0; reconnect_at = 0; rejoin[0] = 0; backoff_cs = 500;
  stop_link ();
}

/* forget the conversations of the demonstration or of the last server: only the status buffer stays */
static void clear_buffers (void)
{
  int i;
  for (i = 1; i < nbufs; i++) { buffer_free (bufs[i]); bufs[i] = NULL; }
  nbufs = 1; cur = 0;
  nicks_dirty = 1;
  set_title ();
  set_main_extent ();
  force_redraw (win_main);
  refresh_lists ();
}

static void connect_network_ex (const netsettings *n, int keep)
{
  static const net_events evs = { net_status_cb, net_connected_cb, net_data_cb, net_closed_cb };
  irc_config cfg;
  stop_link ();
  demo_active = 0;
  if (!keep) clear_buffers ();
  want_connected = 1; reconnect_at = 0;
  cur_net = *n;
  irc_conn_free (conn);
  cfg = make_config ();
  conn = irc_conn_new (&cfg, on_event, NULL);
  snprintf (nicklabel, sizeof nicklabel, "%s", current_nick ());
  Icon_SetText (win_input, ic_nick, nicklabel);
  status_line (LK_INFO, "Connecting to %s (%s port %d%s)", n->name, n->host, net_port (n), n->tls ? " with TLS" : ", not encrypted");
  srvlink = net_open (n->host, net_port (n), n->tls, CA_FILE, 1, &evs, NULL);
  if (!srvlink) link_closed = 0;					/* net_open has reported the failure */
}

static void connect_network (const netsettings *n)
{
  rejoin[0] = 0; backoff_cs = 500;
  connect_network_ex (n, 0);
}

static void save_settings (void)
{
  mkdir ("<Choices$Write>.Natter", 0777);
  if (settings_save (&cfg_set, CHOICES_WRITE) == 0) status_line (LK_INFO, "The choices are saved in %s", CHOICES_WRITE);
  else status_line (LK_ERROR, "The choices could not be saved in %s", CHOICES_WRITE);
}

static void go_demo (void)
{
  irc_config cfg;
  drop_link ();
  demo_active = 1;
  clear_buffers ();
  irc_conn_free (conn);
  cfg = make_config ();
  conn = irc_conn_new (&cfg, on_event, NULL);
  status_line (LK_INFO, "The demonstration: the server is made up.");
  demo_start (feed_from_server, NULL, current_nick ());
  irc_conn_start (conn, Time_Monotonic ());
  pump_output ();
}

/* ---- the input line ---- */

static void remember (const char *line)
{
  if (!*line) return;
  if (nhist && !strcmp (history[nhist - 1], line)) { hpos = -1; return; }
  if (nhist == MAXHIST) { free (history[0]); memmove (history, history + 1, (MAXHIST - 1) * sizeof *history); nhist--; }
  history[nhist++] = strdup (line);
  hpos = -1;
}

static void run_command (char *line)
{
  char *cmd = line + 1, *arg;
  buffer *b = bufs[cur];
  arg = strchr (cmd, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; } else arg = cmd + strlen (cmd);
  if (!strcasecmp (cmd, "join") || !strcasecmp (cmd, "j")) { if (*arg) irc_join (conn, arg, NULL); else status_line (LK_ERROR, "Usage: /join #channel"); }
  else if (!strcasecmp (cmd, "part") || !strcasecmp (cmd, "leave")) { if (b->kind == BK_CHANNEL) irc_part (conn, *arg ? arg : b->name, NULL); else status_line (LK_ERROR, "This is not a channel"); }
  else if (!strcasecmp (cmd, "me")) { if (b->kind != BK_STATUS) irc_action (conn, b->name, arg); }
  else if (!strcasecmp (cmd, "nick"))
    {
      if (*arg) { snprintf (cfg_set.nick, sizeof cfg_set.nick, "%s", arg); cur_net.nick[0] = 0; if (!srvlink || net_connected (srvlink)) irc_set_nick (conn, arg); }
    }
  else if (!strcasecmp (cmd, "connect") || !strcasecmp (cmd, "server"))
    {
      char name[128]; int port = 0, tls = 1, idx; char *w, *save = NULL;
      name[0] = 0;
      for (w = strtok_r (arg, " ", &save); w; w = strtok_r (NULL, " ", &save))
	{
	  if (!strcasecmp (w, "notls")) tls = 0;
	  else if (!name[0]) snprintf (name, sizeof name, "%s", w);
	  else if (atoi (w) > 0) port = atoi (w);
	}
      if (!name[0] && cfg_set.nnets == 1) snprintf (name, sizeof name, "%s", cfg_set.nets[0].name);
      if (!name[0]) status_line (LK_ERROR, "Usage: /connect NAME (a network of the choices; /networks lists them) or /connect host [port] [notls]");
      else if ((idx = settings_find (&cfg_set, name)) >= 0) connect_network (&cfg_set.nets[idx]);
      else
	{
	  netsettings adhoc;
	  memset (&adhoc, 0, sizeof adhoc);
	  snprintf (adhoc.name, sizeof adhoc.name, "%s", name); snprintf (adhoc.host, sizeof adhoc.host, "%s", name);
	  adhoc.port = port; adhoc.tls = tls; adhoc.log = -1;
	  connect_network (&adhoc);
	}
    }
  else if (!strcasecmp (cmd, "networks"))
    {
      int i;
      if (!cfg_set.nnets) status_line (LK_INFO, "There are no networks in the choices. Connect with /connect host, then /savenet NAME.");
      for (i = 0; i < cfg_set.nnets; i++) status_line (LK_INFO, "%s: %s port %d%s%s", cfg_set.nets[i].name, cfg_set.nets[i].host, net_port (&cfg_set.nets[i]), cfg_set.nets[i].tls ? " with TLS" : "", cfg_set.nets[i].autoconnect ? ", connects at start" : "");
    }
  else if (!strcasecmp (cmd, "autojoin")) { snprintf (cur_net.autojoin, sizeof cur_net.autojoin, "%s", arg); status_line (LK_INFO, "The channels that are joined after connecting: %s (/savenet keeps them)", cur_net.autojoin[0] ? cur_net.autojoin : "none"); }
  else if (!strcasecmp (cmd, "savenet"))
    {
      if (!*arg) status_line (LK_ERROR, "Usage: /savenet NAME: keeps the network you are connected to under that name");
      else if (!cur_net.host[0]) status_line (LK_ERROR, "Not connected to a network yet");
      else
	{
	  netsettings *n = settings_add (&cfg_set, arg);
	  if (!n) status_line (LK_ERROR, "There is no room for another network");
	  else { char keep[48]; snprintf (keep, sizeof keep, "%s", n->name); *n = cur_net; snprintf (n->name, sizeof n->name, "%s", keep); save_settings (); }
	}
    }
  else if (!strcasecmp (cmd, "save")) save_settings ();
  else if (!strcasecmp (cmd, "choices")) choices_show (&cfg_set, settings_find (&cfg_set, cur_net.name));
  else if (!strcasecmp (cmd, "log"))
    {
      char path[400];
      if (!strcasecmp (arg, "on") || !strcasecmp (arg, "off")) { b->log = !strcasecmp (arg, "on"); }
      else if (!strcasecmp (arg, "all on") || !strcasecmp (arg, "all off")) { cfg_set.log = !strcasecmp (arg, "all on"); status_line (LK_INFO, "Logging is %s for everything that has no setting of its own (/save keeps this)", cfg_set.log ? "on" : "off"); }
      else if (*arg) { status_line (LK_ERROR, "Usage: /log (shows), /log on, /log off (this conversation), /log all on, /log all off"); goto logdone; }
      logger_path (lg, cur_net.name[0] ? cur_net.name : "Natter", b->kind, b->name, path, sizeof path);
      status_line (LK_INFO, "Logging of %s is %s%s%s", b->name, log_enabled (b) ? "on" : "off", log_enabled (b) ? ": " : "", log_enabled (b) ? path : "");
    logdone: ;
    }
  else if (!strcasecmp (cmd, "disconnect")) { if (srvlink) { drop_link (); status_line (LK_INFO, "Disconnected"); } else status_line (LK_INFO, "Not connected"); }
  else if (!strcasecmp (cmd, "demo")) go_demo ();
  else if (!strcasecmp (cmd, "msg") || !strcasecmp (cmd, "query") || !strcasecmp (cmd, "q"))
    {
      char *text = strchr (arg, ' ');
      if (text) *text++ = 0;
      if (*arg) { buffer *q = get_buffer (arg, BK_QUERY); select_buffer (buffer_index (q)); if (text && *text) irc_say (conn, arg, text); }
    }
  else if (!strcasecmp (cmd, "topic")) { if (b->kind == BK_CHANNEL) irc_set_topic (conn, b->name, *arg ? arg : NULL); }
  else if (!strcasecmp (cmd, "whois")) { if (*arg) { char l[200]; snprintf (l, sizeof l, "WHOIS %s", arg); irc_send_raw (conn, l); } else status_line (LK_ERROR, "Usage: /whois nick"); }
  else if (!strcasecmp (cmd, "mode")) { char l[400]; snprintf (l, sizeof l, "MODE %s%s%s", *arg && !strchr ("#&+!", arg[0]) && b->kind == BK_CHANNEL ? b->name : "", *arg && !strchr ("#&+!", arg[0]) && b->kind == BK_CHANNEL ? " " : "", arg); if (*arg) irc_send_raw (conn, l); }
  else if (b->kind == BK_CHANNEL && (!strcasecmp (cmd, "op") || !strcasecmp (cmd, "deop") || !strcasecmp (cmd, "voice") || !strcasecmp (cmd, "devoice")))
    {
      char l[300];
      if (!*arg) status_line (LK_ERROR, "Usage: /%s nick", cmd);
      else { snprintf (l, sizeof l, "MODE %s %c%c %s", b->name, !strncasecmp (cmd, "de", 2) ? '-' : '+', strstr (cmd, "voice") ? 'v' : 'o', arg); irc_send_raw (conn, l); }
    }
  else if (b->kind == BK_CHANNEL && !strcasecmp (cmd, "kick"))
    {
      char l[500], *why = strchr (arg, ' ');
      if (why) *why++ = 0;
      if (!*arg) status_line (LK_ERROR, "Usage: /kick nick [reason]");
      else { snprintf (l, sizeof l, "KICK %s %s :%s", b->name, arg, why && *why ? why : arg); irc_send_raw (conn, l); }
    }
  else if (b->kind == BK_CHANNEL && !strcasecmp (cmd, "ban"))
    {
      char l[300];
      const irc_user *u = *arg ? irc_conn_user (conn, arg) : NULL;
      if (!*arg) status_line (LK_ERROR, "Usage: /ban nick");
      else
	{
	  if (u && u->host) snprintf (l, sizeof l, "MODE %s +b *!*@%s", b->name, u->host); else snprintf (l, sizeof l, "MODE %s +b %s!*@*", b->name, arg);
	  irc_send_raw (conn, l);
	}
    }
  else if (!strcasecmp (cmd, "quit")) { want_connected = 0; reconnect_at = 0; irc_quit (conn, *arg ? arg : "Natter"); }
  else if (!strcasecmp (cmd, "raw") || !strcasecmp (cmd, "quote")) irc_send_raw (conn, arg);
  else if (!strcasecmp (cmd, "help")) status_line (LK_INFO, "Commands: /connect NAME or host [port] [notls], /choices, /networks, /savenet NAME, /autojoin #a,#b, /log on|off, /disconnect, /demo, /join, /part, /me, /nick, /msg, /query, /topic, /whois, /op /deop /voice /devoice, /kick, /ban, /mode, /quit, /raw");
  else status_line (LK_ERROR, "Unknown command: /%s (try /help)", cmd);
}

static void submit_input (void)
{
  char line[512];
  buffer *b = bufs[cur];
  Icon_GetText (win_input, ic_input, line);
  remember (line);
  Icon_SetText (win_input, ic_input, "");
  Icon_SetCaret (win_input, ic_input);
  if (!*line) return;
  if (line[0] == '/' && line[1] != '/') run_command (line);
  else if (b->kind == BK_STATUS) status_line (LK_ERROR, "This is the status window: join a channel with /join #channel");
  else irc_say (conn, b->name, line[0] == '/' ? line + 1 : line);
  pump_output ();
}

/* Tab: complete the nick that is being typed, and go on to the next one when it is pressed again */
static char tab_prefix[64]; static size_t tab_start, tab_count; static int tabbing;

static void complete_nick (void)
{
  char line[512];
  size_t len, i, seen = 0;
  Icon_GetText (win_input, ic_input, line);
  len = strlen (line);
  if (!tabbing)
    {
      size_t s = len;
      while (s > 0 && line[s - 1] != ' ') s--;
      tab_start = s;
      snprintf (tab_prefix, sizeof tab_prefix, "%s", line + s);
      tab_count = 0;
    }
  if (nicks_dirty) rebuild_nicks ();
  for (i = 0; i < nnicks; i++)
    {
      const char *nk = nicks[i].user->nick;
      if (strncasecmp (nk, tab_prefix, strlen (tab_prefix))) continue;
      if (seen++ < tab_count) continue;
      line[tab_start] = 0;
      strncat (line, nk, sizeof line - strlen (line) - 3);
      strcat (line, tab_start == 0 ? ": " : " ");
      Icon_SetText (win_input, ic_input, line);
      Icon_SetCaret (win_input, ic_input);
      tab_count++; tabbing = 1;
      return;
    }
  if (seen && tab_count > 0) { tab_count = 0; complete_nick (); return; }			/* ran out: start again from the first match */
  tabbing = 0;
}

static BOOL key_handler (event_pollblock *event, void *reference)
{
  int code = event->data.key.code;
  (void) reference;
  switch (code)
    {
    case 13: tabbing = 0; submit_input (); return TRUE;
    case 9: complete_nick (); return TRUE;
    case 0x18F:								/* up arrow: the previous line that was typed */
      if (nhist) { if (hpos < 0) hpos = nhist; if (hpos > 0) hpos--; Icon_SetText (win_input, ic_input, history[hpos]); Icon_SetCaret (win_input, ic_input); }
      return TRUE;
    case 0x18E:								/* down arrow */
      if (hpos >= 0) { hpos++; if (hpos >= nhist) { hpos = -1; Icon_SetText (win_input, ic_input, ""); } else Icon_SetText (win_input, ic_input, history[hpos]); Icon_SetCaret (win_input, ic_input); }
      return TRUE;
    case 0x19F: case 0x19E:						/* page up and down scroll the conversation */
      {
	window_state st;
	Wimp_GetWindowState (win_main, &st);
	st.openblock.scroll.y += code == 0x19F ? (st.openblock.screenrect.max.y - st.openblock.screenrect.min.y) - 2 * LH : -((st.openblock.screenrect.max.y - st.openblock.screenrect.min.y) - 2 * LH);
	Wimp_OpenWindow (&st.openblock);
	return TRUE;
      }
    }
  tabbing = 0;
  Wimp_ProcessKey (code);
  return TRUE;
}

/* ---- drawing ---- */

static void draw_scrollback (window_redrawblock *rb)
{
  buffer *b = bufs[cur];
  int ox = rb->rect.min.x - rb->scroll.x, oy = rb->rect.max.y - rb->scroll.y;
  int right = rb->rect.max.x - RIGHT_EDGE;
  int first = (oy - TOP_H - rb->cliprect.max.y) / LH, last = (oy - TOP_H - rb->cliprect.min.y) / LH, row;
  size_t line = 0; int rowin = 0;
  if (first < 0) first = 0;
  if (last >= (int) buffer_total_rows (b)) last = (int) buffer_total_rows (b) - 1;
  if (last > first + 400) last = first + 400;					/* a window is never taller than that in rows */
  LOG ("scrollback: clip %d,%d to %d,%d rows %d to %d of %d", rb->cliprect.min.x, rb->cliprect.min.y, rb->cliprect.max.x, rb->cliprect.max.y, first, last, (int) buffer_total_rows (b));
  for (row = first; row <= last; row++)
    {
      bline *l;
      int top, base, y0;
      const char *text; size_t from, to;
      char ts[16];
      unsigned bg;
      if (row == first) { if (!buffer_find_row (b, (size_t) row, &line, &rowin)) break; }
      else if (++rowin >= b->lines[line].nrows) { line++; rowin = 0; }
      l = &b->lines[line];
      top = oy - TOP_H - row * LH; y0 = top - LH; base = y0 + 11;
      bg = l->highlight ? COL_BAND : COL_WHITE;
      if (l->highlight) fill (rb->rect.min.x + LEFT_EDGE, y0, right - rb->rect.min.x - LEFT_EDGE, LH, COL_BAND);
      if (rowin == 0 && b->new_marker > 0 && (size_t) b->new_marker == line && (size_t) b->new_marker < b->n)	/* the mark of the new messages, above the first of them */
	{
	  fill (rb->rect.min.x + LEFT_EDGE, top - 1, right - rb->rect.min.x - LEFT_EDGE, 2, COL_MARK);
	}
      if (rowin == 0)
	{
	  clock_text (l->when, ts, sizeof ts);
	  paint (fnorm, COL_MUTED, bg, ox + LEFT_EDGE + PAD, base, ts, strlen (ts));
	  switch (l->kind)
	    {
	    case LK_TEXT: case LK_NOTICE:
	      if (l->nick) { int w = bold_width (l->nick); paint (fbold, l->self ? nickcol[2] : nickcol[l->colour], bg, ox + LEFT_EDGE + PAD + TS_W + NICK_W - w, base, l->nick, strlen (l->nick)); }
	      break;
	    case LK_ACTION: paint (fbold, nickcol[l->colour], bg, ox + LEFT_EDGE + PAD + TS_W + NICK_W - bold_width ("*"), base, "*", 1); break;
	    case LK_JOIN: paint (fbold, COL_MUTED, bg, ox + LEFT_EDGE + PAD + TS_W + NICK_W - bold_width ("-->"), base, "-->", 3); break;
	    case LK_PART: case LK_QUIT: paint (fbold, COL_MUTED, bg, ox + LEFT_EDGE + PAD + TS_W + NICK_W - bold_width ("<--"), base, "<--", 3); break;
	    default: paint (fbold, COL_MUTED, bg, ox + LEFT_EDGE + PAD + TS_W + NICK_W - bold_width ("--"), base, "--", 2); break;
	    }
	}
      text = l->text; from = l->starts[rowin]; to = rowin + 1 < l->nrows ? l->starts[rowin + 1] : strlen (text);
      while (to > from && text[to - 1] == ' ') to--;
      paint (l->kind == LK_ACTION ? fital : fnorm, l->kind == LK_ERROR ? COL_RED : (l->kind == LK_TEXT || l->kind == LK_ACTION || l->kind == LK_NOTICE || l->kind == LK_INFO) ? COL_TEXT : COL_MUTED, bg, ox + TEXT_X, base, text + from, to - from);
    }
}

static BOOL main_redraw (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  (void) reference;
  rb.window = event->data.openblock.window;
  LOG ("main redraw: begin");
  Wimp_RedrawWindow (&rb, &more);
  while (more) { draw_scrollback (&rb); Wimp_GetRectangle (&rb, &more); }
  LOG ("main redraw: end");
  return TRUE;
}

static BOOL left_redraw (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  (void) reference;
  rb.window = event->data.openblock.window;
  LOG ("left redraw");
  Wimp_RedrawWindow (&rb, &more);
  while (more)
    {
      int oy = rb.rect.max.y - rb.scroll.y, ox = rb.rect.min.x - rb.scroll.x, i;
      for (i = 0; i < nbufs; i++)
	{
	  buffer *b = bufs[i];
	  int top = oy - 4 - i * LISTLH, y0 = top - LISTLH, base = y0 + 11;
	  char name[96], badge[16];
	  unsigned fg = COL_TEXT;
	  if (top < rb.cliprect.min.y || y0 > rb.cliprect.max.y) continue;
	  if (i == cur) fill (rb.rect.min.x, y0, LEFT_W, LISTLH, COL_SEL);
	  if (b->kind == BK_STATUS) snprintf (name, sizeof name, "%s", irc_conn_network (conn) ? irc_conn_network (conn) : "Status"); else snprintf (name, sizeof name, "%s", b->name);
	  if (b->kind == BK_QUERY) fg = 0x40802000;
	  paint (b->unread && i != cur ? fbold : fnorm, fg, i == cur ? COL_SEL : COL_LISTBG, ox + (b->kind == BK_STATUS ? 12 : 30), base, name, strlen (name));
	  if (b->unread && i != cur)
	    {
	      int w;
	      snprintf (badge, sizeof badge, "%d", b->highlights ? b->highlights : b->unread);
	      w = bold_width (badge) + 16;
	      fill (ox + LEFT_W - w - 14, y0 + 6, w, LISTLH - 12, b->highlights ? COL_RED : 0xD0D0D000);
	      paint (fbold, b->highlights ? COL_WHITE : COL_TEXT, b->highlights ? COL_RED : 0xD0D0D000, ox + LEFT_W - w - 6, base, badge, strlen (badge));
	    }
	}
      Wimp_GetRectangle (&rb, &more);
    }
  return TRUE;
}

static BOOL right_redraw (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  (void) reference;
  if (nicks_dirty) rebuild_nicks ();
  LOG ("right redraw");
  rb.window = event->data.openblock.window;
  Wimp_RedrawWindow (&rb, &more);
  while (more)
    {
      int oy = rb.rect.max.y - rb.scroll.y, ox = rb.rect.min.x - rb.scroll.x;
      size_t i;
      char head[32];
      snprintf (head, sizeof head, "%lu users", (unsigned long) nnicks);
      if (bufs[cur]->kind == BK_CHANNEL) paint (fnorm, COL_MUTED, COL_LISTBG, ox + 12, oy - LISTLH + 8, head, strlen (head));
      for (i = 0; i < nnicks; i++)
	{
	  int top = oy - 4 - (int) (i + 1) * LISTLH, y0 = top - LISTLH, base = y0 + 11;
	  const irc_user *u = nicks[i].user;
	  irc_member m;
	  char sym[2] = { 0, 0 };
	  if (top < rb.cliprect.min.y || y0 > rb.cliprect.max.y) continue;
	  m.user = (irc_user *) u; m.prefix = nicks[i].prefix;
	  sym[0] = irc_member_symbol (conn, &m);
	  if (sym[0]) paint (fnorm, COL_MUTED, COL_LISTBG, ox + 12, base, sym, 1);
	  if (u->away) paint (fital, COL_MUTED, COL_LISTBG, ox + 32, base, u->nick, strlen (u->nick));
	  else paint (fnorm, nickcol[nick_colour (u->nick)], COL_LISTBG, ox + 32, base, u->nick, strlen (u->nick));
	}
      Wimp_GetRectangle (&rb, &more);
    }
  return TRUE;
}

static BOOL topic_redraw (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  buffer *b = bufs[cur];
  char tmp[300];
  (void) reference;
  rb.window = event->data.openblock.window;
  Wimp_RedrawWindow (&rb, &more);
  if (b->kind == BK_CHANNEL) snprintf (tmp, sizeof tmp, "%s", b->topic ? b->topic : "");
  else if (b->kind == BK_QUERY) snprintf (tmp, sizeof tmp, "Conversation with %s", b->name);
  else snprintf (tmp, sizeof tmp, "%s%s", irc_conn_server (conn) ? "Connected to " : "Not connected", irc_conn_server (conn) ? irc_conn_server (conn) : "");
  while (more)
    {
      int ox = rb.rect.min.x - rb.scroll.x, oy = rb.rect.max.y - rb.scroll.y;
      fill (rb.rect.min.x, oy - TOP_H, rb.rect.max.x - rb.rect.min.x, 2, 0x80808000);
      paint (fnorm, COL_TEXT, COL_WHITE, ox + 16, oy - 29, tmp, strlen (tmp));
      Wimp_GetRectangle (&rb, &more);
    }
  return TRUE;
}

/* ---- the Choices window ---- */

static void ch_saved (const settings *s)
{
  const char *old = logdir;
  char before[sizeof logdir];
  snprintf (before, sizeof before, "%s", old);
  cfg_set = *s;
  snprintf (logdir, sizeof logdir, "%s", cfg_set.logdir[0] ? cfg_set.logdir : "<Choices$Write>.Natter.Logs");
  if (strcmp (before, logdir)) { logger_free (lg); lg = logger_new (logdir, "."); }
  save_settings ();
}

static void ch_connect (int index) { if (index >= 0 && index < cfg_set.nnets) connect_network (&cfg_set.nets[index]); }

static void ch_open_logs (void)
{
  char cmd[300];
  mkdir ("<Choices$Write>.Natter", 0777); mkdir (logdir, 0777);
  snprintf (cmd, sizeof cmd, "Filer_OpenDir %s", logdir);
  Wimp_StartTask (cmd);
}

static const choices_callbacks ch_callbacks = { ch_saved, ch_connect, ch_open_logs };

/* ---- menus ---- */

static menu_ptr m_icon, m_chan, m_nick, m_nets;
static menu_ptr shown;					/* the menu that is open */
static int menu_buf = -1;				/* the conversation that a channel menu is about */
static char menu_nick[64];				/* the person that a nick menu is about */
static icon_handle bar_icon = -1;
static int have_icon;
static void quit_app (void);

/* The icon on the icon bar: the sprite !natter of the Wimp's pool (the file !Sprites is loaded by !Boot and !Run), or natterhl while something that mentions us is waiting.  The name of the
   sprite is in a buffer of ours (an indirected sprite), so that it can be changed. */
static char bar_sprite[16] = "!natter";

static void make_bar_icon (void)
{
  icon_createblock ic;
  os_error *e;
  memset (&ic, 0, sizeof ic);
  ic.window = iconbar_RIGHT;
  ic.icondata.workarearect.min.x = 0; ic.icondata.workarearect.min.y = 0; ic.icondata.workarearect.max.x = 68; ic.icondata.workarearect.max.y = 68;
  ic.icondata.flags.value = (7u << 24) | (1u << 28) | (3u << 12) | 0x2u | 0x8u | 0x10u | 0x100u;	/* sprite, centred, indirected, clicks */
  ic.icondata.data.indirectsprite.name = DL_PTR (bar_sprite);
  ic.icondata.data.indirectsprite.spritearea = (dl_ptr) 1;					/* the Wimp's sprite pool */
  ic.icondata.data.indirectsprite.nameisname = sizeof bar_sprite;
  e = Wimp_CreateIcon (&ic, &bar_icon);
  if (e) { LOG ("no icon on the icon bar: %s", e->errmess); bar_icon = -1; }				/* no error box: Natter works without it */
}

static void update_bar_icon (void)
{
  int i, hl = 0;
  const char *want;
  if (bar_icon < 0) return;
  for (i = 0; i < nbufs; i++) hl += bufs[i]->highlights;
  want = hl ? "natterhl" : "!natter";
  if (strcmp (want, bar_sprite))
    {
      snprintf (bar_sprite, sizeof bar_sprite, "%s", want);
      Icon_ForceRedraw (window_ICONBAR, bar_icon)
    }
}

static void close_buffer (int i)
{
  buffer *b;
  int k;
  if (i <= 0 || i >= nbufs) return;
  b = bufs[i];
  if (b->kind == BK_CHANNEL && b->joined) irc_part (conn, b->name, NULL);
  buffer_free (b);
  for (k = i; k < nbufs - 1; k++) bufs[k] = bufs[k + 1];
  nbufs--;
  if (cur == i) cur = i - 1; else if (cur > i) cur--;
  select_buffer (cur);
}

static void put_in_input (const char *text)
{
  Icon_SetText (win_input, ic_input, text);
  Icon_SetCaret (win_input, ic_input);
}

static void build_menus (void)
{
  m_icon = Menu_New ("Natter", "Info,Networks,Choices...|Quit");
  Menu_AddSubWindow (m_icon, 0, info_window_create ());						/* Info: the standard program information box */
  m_chan = Menu_New ("Conversation", "Join channel...,Part channel,Topic...|Log this conversation,Open log|Close conversation");
  m_nick = Menu_New ("Person", "Talk to,Who is|Give op,Take op,Give voice,Take voice|Kick,Ban|Insert in line");
}

static void show_icon_menu (int x)
{
  char desc[1200];
  int i;
  size_t n = 0;
  desc[0] = 0;
  for (i = 0; i < cfg_set.nnets && n < 1000; i++) n += (size_t) snprintf (desc + n, sizeof desc - n, "%s%s", i ? "," : "", cfg_set.nets[i].name);
  if (!cfg_set.nnets) snprintf (desc, sizeof desc, "~No networks");
  { menu_ptr fresh = Menu_New ("Connect to", desc); Menu_AddSubMenu (m_icon, 1, fresh); if (m_nets) Menu_FullDispose (m_nets); m_nets = fresh; }
  shown = m_icon;
  Menu_Show (m_icon, x - 64, -1);
}

static void show_chan_menu (int bufidx, int x, int y)
{
  buffer *b = bufs[bufidx];
  int chan = b->kind == BK_CHANNEL;
  menu_buf = bufidx;
  Menu_SetFlags (m_chan, 0, 0, 0);
  Menu_SetFlags (m_chan, 1, 0, chan ? 0 : 1);
  Menu_SetFlags (m_chan, 2, 0, chan ? 0 : 1);
  Menu_SetFlags (m_chan, 3, log_enabled (b), 0);
  Menu_SetFlags (m_chan, 5, 0, bufidx == 0 ? 1 : 0);
  shown = m_chan;
  Menu_Show (m_chan, x - 64, y);
}

static void show_nick_menu (const char *nick, int x, int y)
{
  int self = !irc_conn_casecmp (conn, nick, irc_conn_nick (conn));
  snprintf (menu_nick, sizeof menu_nick, "%s", nick);
  Menu_SetFlags (m_nick, 0, 0, self);
  shown = m_nick;
  Menu_Show (m_nick, x - 64, y);
}

static void open_log_file (buffer *b)
{
  char path[400], cmd[460];
  FILE *f;
  logger_flush (lg);
  logger_path (lg, cur_net.name[0] ? cur_net.name : "Natter", b->kind, b->name, path, sizeof path);
  f = fopen (path, "r");
  if (!f) { status_line (LK_INFO, "There is no log of %s yet (%s)", b->name, path); return; }
  fclose (f);
  status_line (LK_INFO, "Opening %s (if nothing opens, open a folder that holds your text editor first: the Filer must have seen it)", path);
  snprintf (cmd, sizeof cmd, "Filer_Run %s", path);
  Wimp_StartTask (cmd);
}

static BOOL menu_handler (event_pollblock *event, void *reference)
{
  int *sel = event->data.selection;
  (void) reference;
  if (choices_handle_menu (sel)) return TRUE;						/* the popup of the Choices window */
  if (shown == m_icon)
    {
      switch (sel[0])
	{
	case 0: status_line (LK_INFO, "Natter 0.1 for RISC OS 64: an IRC client. Logs: %s", logdir); break;
	case 1: if (sel[1] >= 0 && sel[1] < cfg_set.nnets) connect_network (&cfg_set.nets[sel[1]]); break;
	case 2: choices_show (&cfg_set, settings_find (&cfg_set, cur_net.name)); break;
	case 3: quit_app (); break;
	}
    }
  else if (shown == m_chan && menu_buf >= 0 && menu_buf < nbufs)
    {
      buffer *b = bufs[menu_buf];
      switch (sel[0])
	{
	case 0: put_in_input ("/join #"); break;
	case 1: if (b->kind == BK_CHANNEL) irc_part (conn, b->name, NULL); break;
	case 2: { char t[400]; snprintf (t, sizeof t, "/topic %s", b->topic ? b->topic : ""); put_in_input (t); } break;
	case 3: b->log = !log_enabled (b); status_line (LK_INFO, "Logging of %s is %s", b->name, b->log ? "on" : "off"); break;
	case 4: open_log_file (b); break;
	case 5: close_buffer (menu_buf); break;
	}
    }
  else if (shown == m_nick && menu_nick[0])
    {
      char t[200];
      switch (sel[0])
	{
	case 0: select_buffer (buffer_index (get_buffer (menu_nick, BK_QUERY))); break;
	case 1: snprintf (t, sizeof t, "WHOIS %s", menu_nick); irc_send_raw (conn, t); break;
	case 2: case 3: case 4: case 5:
	  if (bufs[cur]->kind == BK_CHANNEL) { snprintf (t, sizeof t, "MODE %s %c%c %s", bufs[cur]->name, sel[0] % 2 ? '-' : '+', sel[0] < 4 ? 'o' : 'v', menu_nick); irc_send_raw (conn, t); }
	  break;
	case 6: snprintf (t, sizeof t, "/kick %s ", menu_nick); put_in_input (t); break;
	case 7: snprintf (t, sizeof t, "/ban %s", menu_nick); put_in_input (t); break;
	case 8: { char line[512]; Icon_GetText (win_input, ic_input, line); strncat (line, menu_nick, sizeof line - strlen (line) - 2); strcat (line, " "); put_in_input (line); } break;
	}
    }
  pump_output ();
  return TRUE;
}

/* ---- the mouse ---- */

static BOOL left_click (event_pollblock *event, void *reference)
{
  window_state st;
  int row;
  (void) reference;
  Wimp_GetWindowState (win_left, &st);
  row = ((st.openblock.screenrect.max.y - st.openblock.scroll.y) - event->data.mouse.pos.y - 4) / LISTLH;
  if (event->data.mouse.button.data.menu) { if (row >= 0 && row < nbufs) show_chan_menu (row, event->data.mouse.pos.x, event->data.mouse.pos.y); return TRUE; }
  if (row >= 0 && row < nbufs && event->data.mouse.pos.y > 0) select_buffer (row);
  Icon_SetCaret (win_input, ic_input);
  return TRUE;
}

/* the nick list: a double click on a nick opens a conversation with that person */
static BOOL right_click (event_pollblock *event, void *reference)
{
  window_state st;
  int row;
  (void) reference;
  Wimp_GetWindowState (win_right, &st);
  row = ((st.openblock.screenrect.max.y - st.openblock.scroll.y) - event->data.mouse.pos.y - 4) / LISTLH - 1;	/* the first row is the number of users */
  if (event->data.mouse.button.data.menu)
    {
      if (row >= 0 && (size_t) row < nnicks) show_nick_menu (nicks[row].user->nick, event->data.mouse.pos.x, event->data.mouse.pos.y);
      return TRUE;
    }
  if (event->data.mouse.button.data.select && row >= 0 && (size_t) row < nnicks && event->data.mouse.pos.y > 0)
    {
      const char *nk = nicks[row].user->nick;
      if (irc_conn_casecmp (conn, nk, irc_conn_nick (conn))) select_buffer (buffer_index (get_buffer (nk, BK_QUERY)));
    }
  Icon_SetCaret (win_input, ic_input);
  return TRUE;
}

static BOOL focus_click (event_pollblock *event, void *reference)
{
  (void) reference;
  if (event->data.mouse.button.data.menu && event->data.mouse.window == win_main) { show_chan_menu (cur, event->data.mouse.pos.x, event->data.mouse.pos.y); return TRUE; }
  Icon_SetCaret (win_input, ic_input);
  return TRUE;
}

static BOOL main_open_handler (event_pollblock *event, void *reference)
{
  window_openblock *ob = &event->data.openblock;
  (void) reference;
  main_h = ob->screenrect.max.y - ob->screenrect.min.y;
  win_width = ob->screenrect.max.x - ob->screenrect.min.x;
  set_main_extent ();
  {									/* not further than the content: the extent is bigger than that */
    int least = content_height () > main_h ? main_h - content_height () : 0;
    if (ob->scroll.y < least) ob->scroll.y = least;
    if (ob->scroll.y > 0) ob->scroll.y = 0;
    ob->scroll.x = 0;
  }
  set_pane_extent (win_left, nbufs + 1, LEFT_W);
  set_pane_extent (win_right, (int) nnicks + 1, RIGHT_W);
  Pane2_OpenWindow (win_main, ob);					/* the main window and the panes, at the size that was asked for */
  return TRUE;
}

static void quit_app (void)
{
  drop_link ();
  logger_free (lg); lg = NULL;
  Event_CloseDown ();
  exit (0);
}

static BOOL close_handler (event_pollblock *event, void *reference)
{
  (void) event; (void) reference;
  if (have_icon) { Pane2_Hide (win_main); return TRUE; }		/* the icon on the icon bar brings the window back; Quit in its menu ends Natter */
  quit_app ();
  return TRUE;
}

static BOOL icon_click (event_pollblock *event, void *reference)
{
  (void) reference;
  if (event->data.mouse.button.data.menu) { show_icon_menu (event->data.mouse.pos.x); return TRUE; }
  Pane2_Show (win_main, open_WHEREVER);
  Icon_SetCaret (win_input, ic_input);
  return TRUE;
}

/* ---- timing ---- */

static BOOL null_handler (event_pollblock *event, void *reference)
{
  static unsigned last_tick;
  unsigned now = Time_Monotonic ();
  window_state st;
  (void) event; (void) reference;
  if (demo_active) demo_tick (now);
  if (srvlink) { net_poll (srvlink); if (link_closed) { net_free (srvlink); srvlink = NULL; link_closed = 0; } }
  if (reconnect_at && !srvlink && (int) (now - reconnect_at) >= 0)
    {
      reconnect_at = 0;
      status_line (LK_INFO, "Connecting again to %s", cur_net.name);
      connect_network_ex (&cur_net, 1);
    }
  pump_output ();
  if (now - last_tick >= 100) { last_tick = now; irc_conn_tick (conn, now); }
  { static unsigned last_flush; if (lg && now - last_flush >= 300) { last_flush = now; logger_flush (lg); } }
  { static unsigned last_icon; if (now - last_icon >= 100) { last_icon = now; update_bar_icon (); } }
  Wimp_GetWindowState (win_main, &st);						/* the window may have been resized */
  {
    int w = st.openblock.screenrect.max.x - st.openblock.screenrect.min.x;
    int h = st.openblock.screenrect.max.y - st.openblock.screenrect.min.y;
    if (w != win_width || h != main_h)
      {
	LOG ("main window is now %d x %d", w, h);
	win_width = w; main_h = h;
	layout_width = w - TEXT_X - RIGHT_EDGE - PAD;
	if (layout_width < 100) layout_width = 100;
	relayout_current ();
	nicks_dirty = 1;
	refresh_lists ();							/* new extents for the lists ... */
	Pane2_OpenWindow (win_main, NULL);					/* ... and the panes are opened again at their full height */
	force_redraw (win_main);
      }
  }
  if (nicks_dirty && bufs[cur]->kind == BK_CHANNEL) { rebuild_nicks (); refresh_lists (); }
  else if (nicks_dirty) { rebuild_nicks (); }
  return TRUE;
}

/* ---- creating the windows ---- */

static window_handle make_window (int x0, int y0, int x1, int y1, unsigned flags, int back, int wx, int wy, const char *title, int minw, int minh, int wtype)
{
  window_block wb;
  window_handle h = 0;
  memset (&wb, 0, sizeof wb);
  wb.screenrect.min.x = x0; wb.screenrect.min.y = y0; wb.screenrect.max.x = x1; wb.screenrect.max.y = y1;
  wb.behind = -1;
  wb.flags.value = flags;
  wb.colours.cols.titlefore = 7; wb.colours.cols.titleback = 2; wb.colours.cols.workfore = 7; wb.colours.cols.workback = (unsigned char) back;
  wb.colours.cols.scrollouter = 3; wb.colours.cols.scrollinner = 1; wb.colours.cols.titlefocus = 12;
  wb.workarearect.min.x = 0; wb.workarearect.max.x = wx; wb.workarearect.max.y = 0; wb.workarearect.min.y = -wy;
  wb.workflags.value = (unsigned) wtype << 12;				/* the button type of the work area: 3 = clicks are reported (0 would report none), 5 = double clicks too */
  wb.spritearea = 1;
  wb.minsize.x = minw; wb.minsize.y = minh;				/* the Wimp opens a window at least this big: only the main window needs a real minimum */
  if (title && title == titlebuf)					/* indirected title text, so that Window_SetTitle works */
    {
      snprintf (titlebuf, sizeof titlebuf, "Natter");
      wb.titleflags.value = 0x119;
      wb.title.indirecttext.buffer = DL_PTR (titlebuf); wb.title.indirecttext.validstring = (dl_ptr) -1; wb.title.indirecttext.bufflen = sizeof titlebuf;
    }
  else { wb.titleflags.value = 0x19; strncpy (wb.title.text, title ? title : "", 11); }
  wb.numicons = 0;
  Wimp_CreateWindow (&wb, &h);
  return h;
}

static void make_windows (void)
{
  wimp_point off, size;
  icon_createblock ic;
  win_main = make_window (80, 120, 1480, 860, 0xBF000002u, 0, 1400, 600, titlebuf, MIN_W, 400, 3);	/* vertical scroll bar, size box, title bar with indirected text; not redrawn by the Wimp */
  Pane2_AddMain (win_main);
  /* the panes: flags 0x90000000 is a window with a vertical scroll bar and nothing else, 0x80000000 one with nothing: neither can be moved */
  win_left = make_window (80, 120, 80 + LEFT_W, 700, 0x90000000u | 0x20u, 1, LEFT_W, 600, "", 20, 20, 3);
  win_right = make_window (1100, 120, 1100 + RIGHT_W, 700, 0x90000000u | 0x20u, 1, RIGHT_W, 600, "", 20, 20, 5);
  win_topic = make_window (80, 800, 1400, 800 + TOP_H, 0x80000000u | 0x20u, 0, 4000, TOP_H, "", 20, 20, 3);
  win_input = make_window (80, 120, 1400, 120 + BOT_H, 0x80000000u | 0x20u | 0x10u, 1, 4000, BOT_H, "", 20, 20, 3);	/* 0x10: the Wimp redraws it (it has only icons): a window that asks to be redrawn and is not gets asked for ever */
  /* the first pane that is added is the one in front: the input line and the topic, then the lists, which stretch down to the bottom of the window behind the input line */
  off.x = 0; off.y = 0; size.x = 1400; size.y = BOT_H;
  Pane2_AddPane (win_main, win_input, &off, &size, pane2_FIXED | pane2_HRESIZE | pane2_HSTRETCH);
  off.x = 0; off.y = 0; size.x = 1400; size.y = TOP_H;
  Pane2_AddPane (win_main, win_topic, &off, &size, pane2_PANETOP | pane2_MAINTOP | pane2_FIXED | pane2_HRESIZE | pane2_HSTRETCH);
  off.x = 0; off.y = TOP_H; size.x = LEFT_W; size.y = 600;
  Pane2_AddPane (win_main, win_left, &off, &size, pane2_PANETOP | pane2_MAINTOP | pane2_FIXED | pane2_VRESIZE | pane2_VSTRETCH);
  off.x = -RIGHT_EDGE; off.y = TOP_H; size.x = RIGHT_W; size.y = 600;
  Pane2_AddPane (win_main, win_right, &off, &size, pane2_PANETOP | pane2_MAINTOP | pane2_FIXED | pane2_VRESIZE | pane2_VSTRETCH | pane2_MAINRIGHT);
  /* the two icons of the input line: our nick, and the text */
  memset (&ic, 0, sizeof ic);
  ic.window = win_input;
  ic.icondata.workarearect.min.x = 8; ic.icondata.workarearect.min.y = -BOT_H + 8; ic.icondata.workarearect.max.x = 230; ic.icondata.workarearect.max.y = -8;
  ic.icondata.flags.value = (7u << 24) | (1u << 0) | (1u << 8) | (1u << 4) | (1u << 9);	/* text, indirected, vertically centred, right justified */
  strcpy (nicklabel, "natter_user");
  ic.icondata.data.indirecttext.buffer = DL_PTR (nicklabel);
  ic.icondata.data.indirecttext.validstring = (dl_ptr) -1;
  ic.icondata.data.indirecttext.bufflen = sizeof nicklabel;
  Wimp_CreateIcon (&ic, &ic_nick);
  memset (&ic, 0, sizeof ic);
  ic.window = win_input;
  ic.icondata.workarearect.min.x = 240; ic.icondata.workarearect.min.y = -BOT_H + 8; ic.icondata.workarearect.max.x = 3990; ic.icondata.workarearect.max.y = -8;
  ic.icondata.flags.value = (7u << 24) | (1u << 0) | (1u << 2) | (1u << 8) | (1u << 4) | (15u << 12);	/* text, border, indirected, vertically centred, writable */
  inputbuf[0] = 0;
  ic.icondata.data.indirecttext.buffer = DL_PTR (inputbuf);
  ic.icondata.data.indirecttext.validstring = (dl_ptr) -1;
  ic.icondata.data.indirecttext.bufflen = sizeof inputbuf;
  Wimp_CreateIcon (&ic, &ic_input);
}

static void log_geometry (const char *when)
{
  window_handle w[5];
  const char *name[5] = { "main", "left", "right", "topic", "input" };
  int i;
  w[0] = win_main; w[1] = win_left; w[2] = win_right; w[3] = win_topic; w[4] = win_input;
  for (i = 0; i < 5; i++)
    {
      window_state st;
      Wimp_GetWindowState (w[i], &st);
      LOG ("%s %s: %d,%d to %d,%d (%d x %d) scroll %d,%d open %d", when, name[i], st.openblock.screenrect.min.x, st.openblock.screenrect.min.y, st.openblock.screenrect.max.x, st.openblock.screenrect.max.y,
	   st.openblock.screenrect.max.x - st.openblock.screenrect.min.x, st.openblock.screenrect.max.y - st.openblock.screenrect.min.y, st.openblock.scroll.x, st.openblock.scroll.y, (int) st.flags.data.open);
    }
}

static void open_fonts (void)
{
  if (Font_FindFont ((font_handle *) &fnorm_w, "Homerton.Medium\\EUTF8", 11 * 16, 11 * 16, 0, 0) && Font_FindFont ((font_handle *) &fnorm_w, "Homerton.Medium", 11 * 16, 11 * 16, 0, 0))
    Error_ReportFatal (0, "Natter needs the font Homerton.Medium");
  if (Font_FindFont ((font_handle *) &fbold_w, "Homerton.Bold\\EUTF8", 11 * 16, 11 * 16, 0, 0)) fbold_w = fnorm_w;
  if (Font_FindFont ((font_handle *) &fital_w, "Homerton.Medium.Oblique\\EUTF8", 11 * 16, 11 * 16, 0, 0)) fital_w = fnorm_w;
}

int main (void)
{
  irc_config cfg;
  {									/* the log goes to the NAS in a new file for each run: it can be read from there even if the machine freezes */
    snprintf (logpath, sizeof logpath, "LanMan98::RISCOS.$.Development.GCC.riscos64.irc64.log.nl%ld", (long) time (NULL));
    logf = fopen (logpath, "w");
    if (!logf) { logpath[0] = 0; logf = fopen ("NVMe::NVMe.$.natterlog", "w"); }
  }
  LOG ("Natter starting");
  Event_Initialise ("Natter");
  EventMsg_Initialise ();
  event_mask.data.null = 0;
  read_timezone ();
  open_fonts ();
  LOG ("fonts open: %d %d %d", (int) fnorm, (int) fbold, (int) fital);
  make_windows ();
  LOG ("windows made: main %x left %x right %x topic %x input %x", (unsigned) win_main, (unsigned) win_left, (unsigned) win_right, (unsigned) win_topic, (unsigned) win_input);
  Event_Claim (event_REDRAW, win_main, event_ANY, main_redraw, NULL);
  Event_Claim (event_REDRAW, win_left, event_ANY, left_redraw, NULL);
  Event_Claim (event_REDRAW, win_right, event_ANY, right_redraw, NULL);
  Event_Claim (event_REDRAW, win_topic, event_ANY, topic_redraw, NULL);
  Event_Claim (event_CLICK, win_left, event_ANY, left_click, NULL);
  Event_Claim (event_CLICK, win_main, event_ANY, focus_click, NULL);
  Event_Claim (event_CLICK, win_right, event_ANY, right_click, NULL);
  Event_Claim (event_CLICK, win_topic, event_ANY, focus_click, NULL);
  Event_Claim (event_OPEN, win_left, event_ANY, Handler_OpenWindow, NULL);
  Event_Claim (event_OPEN, win_right, event_ANY, Handler_OpenWindow, NULL);
  Event_Claim (event_KEY, win_input, event_ANY, key_handler, NULL);
  Event_Claim (event_OPEN, win_main, event_ANY, main_open_handler, NULL);
  Event_Claim (event_MENU, event_ANY, event_ANY, menu_handler, NULL);
  Event_Claim (event_CLOSE, win_main, event_ANY, close_handler, NULL);
  Event_Claim (event_NULL, event_ANY, event_ANY, null_handler, NULL);
  build_menus ();
  choices_init (&ch_callbacks);
  make_bar_icon ();
  if (bar_icon >= 0) { have_icon = 1; Event_Claim (event_CLICK, window_ICONBAR, bar_icon, icon_click, NULL); }
  LOG ("icon bar icon %d", (int) bar_icon);
  settings_load (&cfg_set, CHOICES_READ);
  snprintf (logdir, sizeof logdir, "%s", cfg_set.logdir[0] ? cfg_set.logdir : "<Choices$Write>.Natter.Logs");
  lg = logger_new (logdir, ".");
  if (cfg_set.nnets == 0)								/* no choices yet: a first network to start from */
    {
      netsettings *n = settings_add (&cfg_set, "Libera.Chat");
      if (n) { snprintf (n->host, sizeof n->host, "irc.libera.chat"); n->tls = 1; }
    }
  cfg = make_config ();
  conn = irc_conn_new (&cfg, on_event, NULL);
  bufs[0] = buffer_new ("Status", BK_STATUS, 0); nbufs = 1; cur = 0;
  layout_width = 800;
  bufs[0]->layout_width = layout_width;
  Pane2_Show (win_main, open_CENTERED);
  LOG ("windows open");
  { window_state st; Wimp_GetWindowState (win_main, &st); main_h = st.openblock.screenrect.max.y - st.openblock.screenrect.min.y; win_width = 0; }
  log_geometry ("opened");
  set_title ();
  demo_active = 0;
  status_line (LK_INFO, "Natter 0.1 for RISC OS 64. /help lists the commands; /networks the networks of your choices; /connect NAME connects; /demo shows a made-up conversation.");
  status_line (LK_INFO, cfg_set.log ? "Conversations are logged in %s (/log off stops it for one, /log all off for all)" : "Logging is off (/log on turns it on for one conversation, /log all on for all)", logdir);
  {
    int k, started = 0;
    for (k = 0; k < cfg_set.nnets && !started; k++) if (cfg_set.nets[k].autoconnect) { connect_network (&cfg_set.nets[k]); started = 1; }
    if (!started) status_line (LK_INFO, "Not connected. Try /connect %s", cfg_set.nets[0].name);
  }
  Icon_SetCaret (win_input, ic_input);
  LOG ("caret set");
  log_geometry ("before polling");
  LOG ("entering the poll loop");
  {
    unsigned long polls = 0;
    for (;;)
      {
	if (polls < 30) LOG ("poll %lu", polls);
	Event_PollIdle (5);
	if (polls < 30) LOG ("poll %lu returned event %d (window %x)", polls, (int) event_lastevent.type, (unsigned) event_lastevent.data.openblock.window);
	else if (event_lastevent.type != 0 && event_lastevent.type != 1)				/* anything but a null event or a redraw */
	  {
	    if (event_lastevent.type == 6) LOG ("event: mouse click, window %x icon %d button %d at %d,%d", (unsigned) event_lastevent.data.mouse.window, (int) event_lastevent.data.mouse.icon, (int) event_lastevent.data.mouse.button.value, event_lastevent.data.mouse.pos.x, event_lastevent.data.mouse.pos.y);
	    else LOG ("event: type %d window %x", (int) event_lastevent.type, (unsigned) event_lastevent.data.openblock.window);
	    log_sync ();
	  }
	polls++;
      }
  }
  return 0;
}
