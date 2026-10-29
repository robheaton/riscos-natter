/* demo.c - see demo.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "demo.h"

static demo_feed_fn feed_fn;
static void *feed_ud;
static char me[32] = "natter_user";
static unsigned next_event_cs;
static unsigned step;
static int started, echo, registered;

#define MAXCH 8
static char chans[MAXCH][32];
static int nchans;

static void stamp (char *buf, size_t n)
{
  time_t t = time (NULL);
  struct tm *g = gmtime (&t);
  if (g) strftime (buf, n, "@time=%Y-%m-%dT%H:%M:%S.000Z ", g); else buf[0] = 0;
}

/* sends one line with a server-time tag */
static void send_line (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#include <stdarg.h>
static void send_line (const char *fmt, ...)
{
  char tag[48], body[1024], line[1100];
  va_list ap;
  va_start (ap, fmt); vsnprintf (body, sizeof body, fmt, ap); va_end (ap);
  stamp (tag, sizeof tag);
  snprintf (line, sizeof line, "%s%s\n", tag, body);
  feed_fn (feed_ud, line);
}

static int has_chan (const char *c) { int i; for (i = 0; i < nchans; i++) if (!strcmp (chans[i], c)) return 1; return 0; }

static void topic_of (const char *c, char *buf, size_t n)
{
  if (!strcmp (c, "#riscos64")) snprintf (buf, n, "Native C, C++ and modules for RISC OS 64 | logs: example.org/riscos64/log");
  else if (!strcmp (c, "#riscosdev")) snprintf (buf, n, "Development of RISC OS 5 and its applications");
  else snprintf (buf, n, "A channel made up for the demonstration");
}

static void join_channel (const char *c)
{
  char topic[200];
  if (has_chan (c) || nchans >= MAXCH) return;
  snprintf (chans[nchans++], sizeof chans[0], "%s", c);
  topic_of (c, topic, sizeof topic);
  send_line (":%s!natter@demo.example JOIN %s", me, c);
  send_line (":irc.demo 332 %s %s :%s", me, c, topic);
  send_line (":irc.demo 333 %s %s alice!a@h 1761700000", me, c);
  send_line (":irc.demo 353 %s = %s :@alice @bob +carol dave erin %s frank grace heather ivan judith", me, c, me);
  send_line (":irc.demo 366 %s %s :End of /NAMES list.", me, c);
}

void demo_start (demo_feed_fn feed, void *ud, const char *nick)
{
  feed_fn = feed; feed_ud = ud;
  snprintf (me, sizeof me, "%s", nick);
  started = 1; registered = 0; echo = 0; nchans = 0; step = 0; next_event_cs = 0;
  srand ((unsigned) time (NULL));
}

static void register_client (void)
{
  registered = 1;
  send_line (":irc.demo 001 %s :Welcome to the Natter demonstration network, %s", me, me);
  send_line (":irc.demo 005 %s CASEMAPPING=rfc1459 NETWORK=Demo PREFIX=(qaohv)~&@%%+ CHANTYPES=# CHANMODES=beI,k,l,imnpst NICKLEN=30 :are supported by this server", me);
  send_line (":irc.demo 375 %s :- irc.demo message of the day -", me);
  send_line (":irc.demo 372 %s :- This server does not exist: it is a part of Natter that makes up a conversation", me);
  send_line (":irc.demo 372 %s :- so that its windows can be tried without a network.", me);
  send_line (":irc.demo 376 %s :End of /MOTD command.", me);
}

void demo_from_client (const char *line)
{
  char cmd[16], a[256], b[800];
  const char *p = line;
  size_t n = 0;
  while (*p && *p != ' ' && n < sizeof cmd - 1) cmd[n++] = *p++;
  cmd[n] = 0;
  while (*p == ' ') p++;
  a[0] = b[0] = 0;
  if (!strcmp (cmd, "CAP"))
    {
      if (!strncmp (p, "LS", 2)) send_line (":irc.demo CAP * LS :server-time multi-prefix echo-message away-notify extended-join account-notify userhost-in-names");
      else if (!strncmp (p, "REQ", 3)) { const char *l = strchr (p, ':'); send_line (":irc.demo CAP %s ACK :%s", me, l ? l + 1 : ""); if (l && strstr (l, "echo-message")) echo = 1; }
      else if (!strncmp (p, "END", 3)) register_client ();
      return;
    }
  if (!strcmp (cmd, "NICK")) { snprintf (a, sizeof a, "%s", p); if (registered) { send_line (":%s!natter@demo.example NICK %s", me, a); snprintf (me, sizeof me, "%s", a); } return; }
  if (!strcmp (cmd, "PING")) { send_line (":irc.demo PONG irc.demo %s", p); return; }
  if (!strcmp (cmd, "JOIN"))
    {
      char *copy = strdup (p), *c, *save = copy;
      while ((c = strsep (&save, ",")) != NULL) if (*c) join_channel (c);
      free (copy);
      return;
    }
  if (!strcmp (cmd, "PART")) { char *sp; snprintf (a, sizeof a, "%s", p); sp = strchr (a, ' '); if (sp) *sp = 0; { int i; for (i = 0; i < nchans; i++) if (!strcmp (chans[i], a)) { memmove (chans[i], chans[i + 1], (size_t) (nchans - i - 1) * sizeof chans[0]); nchans--; break; } } send_line (":%s!natter@demo.example PART %s", me, a); return; }
  if (!strcmp (cmd, "PRIVMSG") || !strcmp (cmd, "NOTICE"))
    {
      const char *colon = strchr (p, ':');
      size_t tl = (size_t) (colon ? colon - p : 0);
      if (!colon) return;
      if (tl > sizeof a - 1) tl = sizeof a - 1;
      memcpy (a, p, tl); a[tl] = 0;
      while (tl && a[tl - 1] == ' ') a[--tl] = 0;
      snprintf (b, sizeof b, "%s", colon + 1);
      if (echo) send_line (":%s!natter@demo.example %s %s :%s", me, cmd, a, b);
      if (strchr ("#&", a[0]) == NULL && strcmp (a, me))				/* a private message to somebody: the demo person answers */
	send_line (":%s!x@demo.example PRIVMSG %s :You said: %s", a, me, b);
      return;
    }
  if (!strcmp (cmd, "TOPIC"))
    {
      const char *colon = strchr (p, ':');
      if (colon) { size_t tl = (size_t) (colon - p); memcpy (a, p, tl); a[tl] = 0; while (tl && a[tl - 1] == ' ') a[--tl] = 0; send_line (":%s!natter@demo.example TOPIC %s :%s", me, a, colon + 1); }
      return;
    }
  if (!strcmp (cmd, "MODE") && strchr (p, ' ') == NULL) { send_line (":irc.demo 324 %s %s +nt", me, p); return; }
}

static const char *lines[][3] = {
  /* channel, sender, text; a text that starts with "\001" is an action, "+" "-" are a join or quit, "!" a nick change, "~" a topic */
  { "#riscos64", "alice", "Has anyone tried the native compiler on the Pi 5?" },
  { "#riscos64", "bob", "Yes, zlib builds in about a minute." },
  { "#riscos64", "dave", "\001waves and reads the scrollback" },
  { "#riscos64", "carol", "%ME%: the sockets fix is in the SDK now." },
  { "#riscosdev", "heather", "New ROM image is up for testing." },
  { "#riscos64", "alice", "Great, checking the release notes. Caf\xc3\xa9 at 3?" },
  { "#riscos64", "erin", "-Ping timeout: 240 seconds" },
  { "#riscos64", "bob", "The log is at https://example.org/riscos64/log" },
  { "#riscos64", "frank", "Does it handle UTF-8 in nicks and topics? \xe2\x80\x93 caf\xc3\xa9, na\xc3\xafve" },
  { "#riscosdev", "ivan", "\001pushes a fix for the Wimp redraw" },
  { "#riscos64", "judith", "+" },
  { "#riscos64", "grace", "A long line to see the wrapping at work: the quick brown fox jumps over the lazy dog, and then it does it again, because the window can be as narrow as you like." },
  { "#riscos64", "alice", "!alicia" },
  { "#riscos64", "bob", "@carol: please look at the new build" },
  { "#riscos64", "dave", "~Native C, C++ and modules for RISC OS 64 | the demo changed the topic" },
  { "bob", "bob", "Got a minute? %ME%, I have a question about the module kit." },
  { "#riscosdev", "heather", "%ME%: did you see the thread on the forum?" },
  { "#riscos64", "carol", "\001is away for a while" },
};
#define NLINES ((unsigned) (sizeof lines / sizeof *lines))

void demo_tick (unsigned now)
{
  unsigned i;
  if (!started || !registered || !nchans) return;
  if (!next_event_cs) { next_event_cs = now + 300; return; }
  if ((int) (now - next_event_cs) < 0) return;
  next_event_cs = now + 250 + (unsigned) (rand () % 350);
  i = step++ % NLINES;
  {
    const char *c = lines[i][0], *who = lines[i][1], *t = lines[i][2];
    char text[600], *me_at;
    snprintf (text, sizeof text, "%s", t);
    while ((me_at = strstr (text, "%ME%")) != NULL)
      {
	char rest[600];
	snprintf (rest, sizeof rest, "%s", me_at + 4);
	snprintf (me_at, sizeof text - (size_t) (me_at - text), "%s%s", me, rest);
      }
    if (!has_chan (c) && strchr ("#&", c[0])) return;
    if (text[0] == '\001') send_line (":%s!x@demo.example PRIVMSG %s :\001ACTION %s\001", who, strchr ("#&", c[0]) ? c : me, text + 1);
    else if (text[0] == '+') send_line (":%s!x@demo.example JOIN %s", who, c);
    else if (text[0] == '-') send_line (":%s!x@demo.example QUIT :%s", who, text + 1);
    else if (text[0] == '!') send_line (":%s!x@demo.example NICK %s", who, text + 1);
    else if (text[0] == '~') send_line (":%s!x@demo.example TOPIC %s :%s", who, c, text + 1);
    else if (text[0] == '@') send_line (":%s!x@demo.example MODE %s +o %s", "alice", c, text + 1);
    else send_line (":%s!x@demo.example PRIVMSG %s :%s", who, strchr ("#&", c[0]) ? c : me, text);
  }
}
