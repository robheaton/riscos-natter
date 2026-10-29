/* test_irc.c - tests of the protocol core: parsing, lines, names, and scripted conversations with a pretend server.  Build and run with make test in core/. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irc.h"

static int checks, fails;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (!a_ || !b_ || strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CONTAINS(hay, needle) do { const char *h_ = (hay), *n_ = (needle); checks++; if (!strstr (h_, n_)) { fails++; printf ("FAIL %s:%d: \"%s\" is not in:\n%s\n", __FILE__, __LINE__, n_, h_); } } while (0)
#define ABSENT(hay, needle) do { const char *h_ = (hay), *n_ = (needle); checks++; if (strstr (h_, n_)) { fails++; printf ("FAIL %s:%d: \"%s\" should not be in:\n%s\n", __FILE__, __LINE__, n_, h_); } } while (0)

/* ---- parsing ---- */

static void parse (const char *src, irc_msg *m, char *buf, size_t n, int *rc)
{
  snprintf (buf, n, "%s", src);
  *rc = irc_parse (buf, m);
}

static void test_parse (void)
{
  irc_msg m;
  char b[IRC_MAX_LINE];
  int rc;

  parse ("PING :irc.example.org", &m, b, sizeof b, &rc);
  CHECK_INT (rc, 0); CHECK_STR (m.command, "PING"); CHECK_INT (m.nparams, 1); CHECK_STR (m.params[0], "irc.example.org"); CHECK (m.prefix == NULL);

  parse (":nick!user@host.example PRIVMSG #chan :hello world: and more", &m, b, sizeof b, &rc);
  CHECK_INT (rc, 0); CHECK_STR (m.prefix, "nick"); CHECK_STR (m.nick, "nick"); CHECK_STR (m.user, "user"); CHECK_STR (m.host, "host.example");
  CHECK_STR (m.command, "PRIVMSG"); CHECK_INT (m.nparams, 2); CHECK_STR (m.params[0], "#chan"); CHECK_STR (m.params[1], "hello world: and more");

  parse (":irc.example.org 001 me :Welcome to the network", &m, b, sizeof b, &rc);
  CHECK_STR (m.nick, "irc.example.org"); CHECK (m.user == NULL); CHECK (m.host == NULL); CHECK_STR (m.command, "001"); CHECK_STR (m.params[1], "Welcome to the network");

  parse (":nick@host QUIT", &m, b, sizeof b, &rc);
  CHECK_INT (rc, 0); CHECK_STR (m.nick, "nick"); CHECK (m.user == NULL); CHECK_STR (m.host, "host"); CHECK_INT (m.nparams, 0);

  parse ("quit :bye", &m, b, sizeof b, &rc);
  CHECK_STR (m.command, "QUIT");						/* the command is put in upper case */

  parse ("JOIN   #a    #b   :  spaced out ", &m, b, sizeof b, &rc);
  CHECK_INT (m.nparams, 3); CHECK_STR (m.params[0], "#a"); CHECK_STR (m.params[1], "#b"); CHECK_STR (m.params[2], "  spaced out ");

  parse ("TOPIC #c :", &m, b, sizeof b, &rc);
  CHECK_INT (m.nparams, 2); CHECK_STR (m.params[1], "");			/* an empty trailing parameter is a parameter */

  parse ("X a b c d e f g h i j k l m n o p q r s", &m, b, sizeof b, &rc);
  CHECK_INT (m.nparams, IRC_MAX_PARAMS); CHECK_STR (m.params[IRC_MAX_PARAMS - 1], "p q r s");	/* the 16th takes the rest */

  /* tags */
  parse ("@time=2026-10-29T21:30:00.000Z;msgid=abc123;+draft/reply=x\\sy\\:z\\\\w;flag;empty= :n!u@h PRIVMSG #c :hi", &m, b, sizeof b, &rc);
  CHECK_INT (rc, 0); CHECK_INT (m.ntags, 5);
  CHECK_STR (irc_tag_get (&m, "time"), "2026-10-29T21:30:00.000Z"); CHECK_STR (irc_tag_get (&m, "msgid"), "abc123");
  CHECK_STR (irc_tag_get (&m, "+draft/reply"), "x y;z\\w");			/* \s \: \\ are unescaped */
  CHECK_STR (irc_tag_get (&m, "flag"), ""); CHECK_STR (irc_tag_get (&m, "empty"), ""); CHECK (irc_tag_get (&m, "nope") == NULL);
  CHECK_STR (m.nick, "n"); CHECK_STR (m.params[1], "hi");

  parse ("@a=b PING :x", &m, b, sizeof b, &rc);				/* tags and no prefix */
  CHECK_INT (rc, 0); CHECK_STR (m.command, "PING"); CHECK_STR (irc_tag_get (&m, "a"), "b");

  parse ("@a=trailing\\ PING :x", &m, b, sizeof b, &rc);			/* a lone backslash at the end of a value is dropped */
  CHECK_STR (irc_tag_get (&m, "a"), "trailing");

  /* not messages */
  parse ("", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);
  parse ("@tags=only", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);
  parse (":prefixonly", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);
  parse (": PING", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);
  parse ("   ", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);
  parse ("@a=b ", &m, b, sizeof b, &rc); CHECK_INT (rc, -1);

  /* too many tags: the rest are ignored, nothing overflows */
  {
    char big[IRC_MAX_LINE];
    size_t n = 0;
    int i;
    big[n++] = '@';
    for (i = 0; i < 100; i++) n += (size_t) snprintf (big + n, sizeof big - n, "k%d=v;", i);
    snprintf (big + n, sizeof big - n, "z=1 PING :x");
    rc = irc_parse (big, &m);
    CHECK_INT (rc, 0); CHECK_INT (m.ntags, IRC_MAX_TAGS); CHECK_STR (m.command, "PING");
  }
}

/* ---- lines ---- */

static char got[64][IRC_MAX_LINE]; static int ngot;
static void collect (void *ud, char *line) { (void) ud; if (ngot < 64) snprintf (got[ngot++], sizeof got[0], "%s", line); }

static void test_lines (void)
{
  irc_linebuf lb;
  char big[IRC_MAX_LINE + 2000];
  irc_linebuf_init (&lb);
  ngot = 0;
  irc_linebuf_feed (&lb, "PING :a\r\nPIN", 12, collect, NULL);
  irc_linebuf_feed (&lb, "G :b\nNOTICE x :c", 16, collect, NULL);
  irc_linebuf_feed (&lb, "\r\n\r\n\n", 5, collect, NULL);
  CHECK_INT (ngot, 3); CHECK_STR (got[0], "PING :a"); CHECK_STR (got[1], "PING :b"); CHECK_STR (got[2], "NOTICE x :c");	/* empty lines are skipped; CR LF and LF both end a line */

  ngot = 0;
  memset (big, 'a', sizeof big);
  irc_linebuf_feed (&lb, big, sizeof big, collect, NULL);			/* far too long and no newline: nothing comes out ... */
  CHECK_INT (ngot, 0);
  irc_linebuf_feed (&lb, "tail\nPING :ok\n", 14, collect, NULL);			/* ... the rest of that line is dropped and the next line is fine */
  CHECK_INT (ngot, 1); CHECK_STR (got[0], "PING :ok");

  ngot = 0;
  irc_linebuf_feed (&lb, "A\0B\n", 4, collect, NULL);				/* a NUL byte does not cut the line short */
  CHECK_INT (ngot, 1); CHECK_STR (got[0], "A B");

  ngot = 0;
  {
    size_t i;
    const char *s = "PRIVMSG #c :one byte at a time\r\n";
    for (i = 0; i < strlen (s); i++) irc_linebuf_feed (&lb, s + i, 1, collect, NULL);
    CHECK_INT (ngot, 1); CHECK_STR (got[0], "PRIVMSG #c :one byte at a time");
  }
}

/* ---- names ---- */

static void test_names (void)
{
  CHECK (irc_casecmp ("Nick[]\\~", "nick{}|^", IRC_CASE_RFC1459) == 0);
  CHECK (irc_casecmp ("Nick[]", "nick{}", IRC_CASE_ASCII) != 0);
  CHECK (irc_casecmp ("NICK", "nick", IRC_CASE_ASCII) == 0);
  CHECK (irc_casecmp ("a~", "a^", IRC_CASE_STRICT) != 0);
  CHECK (irc_casecmp ("a[", "a{", IRC_CASE_STRICT) == 0);
  CHECK (irc_casecmp ("abc", "abd", IRC_CASE_ASCII) < 0);
  CHECK (irc_casecmp ("abc", "ab", IRC_CASE_ASCII) > 0);
  CHECK (irc_casecmp ("", "", IRC_CASE_RFC1459) == 0);

  CHECK_INT (irc_utf8_trim ("hello", 5, 3), 3);
  CHECK_INT (irc_utf8_trim ("hello", 5, 10), 5);
  CHECK_INT (irc_utf8_trim ("h\xc3\xa9llo", 6, 2), 1);				/* would end inside the two bytes of e-acute */
  CHECK_INT (irc_utf8_trim ("h\xc3\xa9llo", 6, 3), 3);
  CHECK_INT (irc_utf8_trim ("\xe2\x80\x93x", 4, 1), 0);				/* a three-byte character does not fit at all */
  CHECK_INT (irc_utf8_trim ("\xe2\x80\x93x", 4, 2), 0);
  CHECK_INT (irc_utf8_trim ("\xe2\x80\x93x", 4, 3), 3);
}

/* ---- scripted conversations ---- */

typedef struct { int type, self, reply, numeric, chan_members; char from[64], target[64], text[1024], arg[64], time[40], account[40], msgid[40]; } ev_t;
static ev_t evs[256]; static int nev;
static irc_conn *g_conn;						/* the connection of the test that is running: the callback looks at the state of the channel */

static void on_event (void *ud, const irc_event *e)
{
  ev_t *x;
  (void) ud;
  if (nev >= 256) return;
  x = &evs[nev++];
  memset (x, 0, sizeof *x);
  x->type = e->type; x->self = e->self; x->reply = e->reply; x->numeric = e->numeric;
  x->chan_members = -1;
  if (g_conn && e->target)
    {
      const irc_channel *ch = irc_conn_channel (g_conn, e->target);
      if (ch) x->chan_members = (int) irc_channel_nmembers (ch);			/* -1: not a channel that we are in */
    }
  #define COPY(f, s) if (e->s) snprintf (x->f, sizeof x->f, "%s", e->s)
  COPY (from, from); COPY (target, target); COPY (text, text); COPY (arg, arg); COPY (time, time); COPY (account, account); COPY (msgid, msgid);
  #undef COPY
}

static ev_t *last_ev (int type)			/* the last event of a type, or NULL */
{
  int i;
  for (i = nev - 1; i >= 0; i--) if (evs[i].type == type) return &evs[i];
  return NULL;
}
static int count_ev (int type) { int i, n = 0; for (i = 0; i < nev; i++) if (evs[i].type == type) n++; return n; }

static const char *drain (irc_conn *c, unsigned now)
{
  static char buf[8][65536];
  static int k;
  char *b = buf[k++ & 7];
  size_t n = irc_conn_output (c, now, b, sizeof buf[0] - 1);
  b[n] = 0;
  return b;
}

static void server (irc_conn *c, unsigned now, const char *lines)		/* what the server sends (with \n between lines: CR is added) */
{
  char tmp[16384];
  size_t n = 0;
  const char *p;
  for (p = lines; *p && n < sizeof tmp - 3; p++) { if (*p == '\n') tmp[n++] = '\r'; tmp[n++] = *p; }
  irc_conn_input (c, tmp, n, now);
}

static irc_config basic (void)
{
  irc_config cfg;
  memset (&cfg, 0, sizeof cfg);
  cfg.nick = "testnick"; cfg.user = "tuser"; cfg.realname = "Test Nick"; cfg.ctcp_version = "natter-test 0.1";
  return cfg;
}

static void test_registration_without_cap (void)
{
  irc_config cfg = basic ();
  irc_conn *c = irc_conn_new (&cfg, on_event, NULL);
  const char *o;
  nev = 0;
  irc_conn_start (c, 0);
  o = drain (c, 0);
  CHECK_STR (o, "CAP LS 302\r\nNICK testnick\r\nUSER tuser 0 * :Test Nick\r\n");
  CHECK (!irc_conn_registered (c));
  server (c, 10, ":irc.example 421 * CAP :Unknown command\n:irc.example 001 testnick :Welcome to the Example network, testnick\n");
  CHECK (irc_conn_registered (c));
  CHECK_STR (irc_conn_server (c), "irc.example"); CHECK_STR (irc_conn_nick (c), "testnick");
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 0);					/* the 421 for CAP is not shown */
  CHECK (last_ev (IRC_EV_CONNECTED) != NULL);
  CHECK_STR (last_ev (IRC_EV_CONNECTED)->text, "Welcome to the Example network, testnick");
  CHECK_STR (drain (c, 20), "");						/* no CAP END is sent when the server did not know CAP */
  irc_conn_free (c);
}

static void test_no_caps_option (void)
{
  irc_config cfg = basic ();
  irc_conn *c;
  cfg.caps = ""; cfg.pass = "secret pass";
  c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0);
  CHECK_STR (drain (c, 0), "PASS :secret pass\r\nNICK testnick\r\nUSER tuser 0 * :Test Nick\r\n");	/* no CAP LS at all */
  irc_conn_free (c);
}

static void test_cap_sasl (void)
{
  irc_config cfg = basic ();
  irc_conn *c;
  const char *o;
  cfg.sasl_user = "acct"; cfg.sasl_pass = "hunter2";
  c = irc_conn_new (&cfg, on_event, NULL);
  nev = 0;
  irc_conn_start (c, 0);
  drain (c, 0);
  /* the list comes in two messages: nothing is asked for before the last one */
  server (c, 1, ":irc CAP * LS * :multi-prefix echo-message server-time\n");
  CHECK_STR (drain (c, 1), "");
  server (c, 2, ":irc CAP * LS :sasl=PLAIN,EXTERNAL away-notify unknown-thing\n");
  o = drain (c, 2);
  CHECK_STR (o, "CAP REQ :server-time multi-prefix away-notify echo-message sasl\r\n");	/* only what we want and the server has, in our order */
  server (c, 3, ":irc CAP testnick ACK :server-time multi-prefix away-notify sasl\n");
  CHECK (irc_conn_cap_enabled (c, "sasl")); CHECK (irc_conn_cap_enabled (c, "server-time")); CHECK (!irc_conn_cap_enabled (c, "echo-message"));	/* asked for, not acknowledged */
  CHECK_STR (drain (c, 3), "AUTHENTICATE PLAIN\r\n");
  server (c, 4, "AUTHENTICATE +\n");
  o = drain (c, 4);
  CHECK_STR (o, "AUTHENTICATE AGFjY3QAaHVudGVyMg==\r\n");				/* base64 of NUL acct NUL hunter2 */
  server (c, 5, ":irc 900 testnick testnick!u@h acct :You are now logged in as acct\n:irc 903 testnick :SASL authentication successful\n");
  CHECK_INT (count_ev (IRC_EV_SASL_OK), 1);
  CHECK_STR (drain (c, 5), "CAP END\r\n");
  server (c, 6, ":irc 001 testnick :Welcome\n");
  CHECK (irc_conn_registered (c));
  CHECK_STR (drain (c, 6), "");
  irc_conn_free (c);
}

static void test_sasl_fail (void)
{
  irc_config cfg = basic ();
  irc_conn *c;
  cfg.sasl_user = "acct"; cfg.sasl_pass = "wrong";
  /* not required: carry on without */
  c = irc_conn_new (&cfg, on_event, NULL);
  nev = 0;
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick ACK :sasl\n"); drain (c, 2);
  server (c, 3, "AUTHENTICATE +\n"); drain (c, 3);
  server (c, 4, ":irc 904 testnick :SASL authentication failed\n");
  CHECK_INT (count_ev (IRC_EV_SASL_FAIL), 1); CHECK_INT (last_ev (IRC_EV_SASL_FAIL)->numeric, 904);
  CHECK_STR (drain (c, 4), "CAP END\r\n");
  irc_conn_free (c);
  /* required: leave */
  cfg.sasl_required = 1;
  c = irc_conn_new (&cfg, on_event, NULL);
  nev = 0;
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick ACK :sasl\n"); drain (c, 2);
  server (c, 3, "AUTHENTICATE +\n"); drain (c, 3);
  server (c, 4, ":irc 904 testnick :SASL authentication failed\n");
  CHECK_STR (drain (c, 4), "QUIT :SASL login failed\r\n");
  irc_conn_free (c);
  /* the server has sasl but only EXTERNAL: no PLAIN login, just CAP END */
  c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl=EXTERNAL\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick ACK :sasl\n");
  CHECK_STR (drain (c, 2), "CAP END\r\n");
  irc_conn_free (c);
  /* the server refuses the request (NAK) */
  cfg.sasl_required = 0;
  c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl server-time\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick NAK :server-time sasl\n");
  CHECK_STR (drain (c, 2), "CAP END\r\n");
  CHECK (!irc_conn_cap_enabled (c, "sasl"));
  irc_conn_free (c);
}

static void test_sasl_chunks (void)
{
  irc_config cfg = basic ();
  irc_conn *c;
  char pass[400], expect[2048];
  const char *o;
  size_t i;
  /* user "u", password of 296 bytes: 1 + 1 + 1 + 296 = 299 ... make the payload exactly 300 bytes = 400 base64 characters */
  for (i = 0; i < 297; i++) pass[i] = 'p';
  pass[297] = 0;
  cfg.sasl_user = "u"; cfg.sasl_pass = pass;
  c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick ACK :sasl\n"); drain (c, 2);
  server (c, 3, "AUTHENTICATE +\n");
  o = drain (c, 3);
  /* 400 characters in one line, then a line with just "+" */
  snprintf (expect, sizeof expect, "AUTHENTICATE ");
  CHECK (strncmp (o, expect, strlen (expect)) == 0);
  {
    const char *first = o + strlen ("AUTHENTICATE "), *eol = strstr (first, "\r\n");
    CHECK (eol != NULL); CHECK_INT (eol - first, 400);
    CHECK_STR (eol + 2, "AUTHENTICATE +\r\n");
  }
  irc_conn_free (c);
  /* a longer password: 400 characters then the rest */
  for (i = 0; i < 397; i++) pass[i] = 'q';
  pass[397] = 0;
  c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :sasl\n"); drain (c, 1);
  server (c, 2, ":irc CAP testnick ACK :sasl\n"); drain (c, 2);
  server (c, 3, "AUTHENTICATE +\n");
  o = drain (c, 3);
  {
    const char *l1 = o + strlen ("AUTHENTICATE "), *e1 = strstr (l1, "\r\n"), *l2, *e2;
    CHECK_INT (e1 - l1, 400);
    l2 = e1 + 2 + strlen ("AUTHENTICATE "); e2 = strstr (l2, "\r\n");
    CHECK (e2 != NULL); CHECK (e2 - l2 > 0 && e2 - l2 < 400);
    CHECK_STR (e2 + 2, "");
  }
  irc_conn_free (c);
}

static void test_nick_in_use (void)
{
  irc_config cfg = basic ();
  irc_conn *c = irc_conn_new (&cfg, on_event, NULL);
  nev = 0;
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc 433 * testnick :Nickname is already in use\n");
  CHECK_STR (drain (c, 1), "NICK testnick_\r\n");
  server (c, 2, ":irc 433 * testnick_ :Nickname is already in use\n");
  CHECK_STR (drain (c, 2), "NICK testnick__\r\n");
  server (c, 3, ":irc 001 testnick__ :Welcome\n");
  CHECK_STR (irc_conn_nick (c), "testnick__");
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 0);
  server (c, 4, ":irc 433 testnick__ other :Nickname is already in use\n");	/* after registration it is only reported */
  CHECK_STR (drain (c, 4), "");
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 1); CHECK_INT (last_ev (IRC_EV_NUMERIC)->numeric, 433);
  irc_conn_free (c);
}

static irc_conn *registered (irc_config *cfg)	/* a connection that is registered as testnick, with the events cleared */
{
  irc_conn *c = irc_conn_new (cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc 421 * CAP :Unknown command\n:irc 001 testnick :Welcome\n");
  nev = 0;
  return c;
}

static void test_ping_and_ctcp (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  ev_t *e;
  server (c, 5, "PING :irc.example\n");
  CHECK_STR (drain (c, 5), "PONG :irc.example\r\n");

  server (c, 6, ":bob!b@h PRIVMSG #chan :hello there\n");
  e = last_ev (IRC_EV_PRIVMSG);
  CHECK (e != NULL); CHECK_STR (e->from, "bob"); CHECK_STR (e->target, "#chan"); CHECK_STR (e->text, "hello there"); CHECK (!e->self);

  server (c, 7, ":bob!b@h PRIVMSG #chan :\001ACTION waves\001\n");
  e = last_ev (IRC_EV_ACTION);
  CHECK (e != NULL); CHECK_STR (e->text, "waves");
  server (c, 8, ":bob!b@h PRIVMSG #chan :\001ACTION waves again\n");		/* a missing closing \001 is allowed */
  CHECK_STR (last_ev (IRC_EV_ACTION)->text, "waves again");

  server (c, 9, ":bob!b@h PRIVMSG testnick :\001VERSION\001\n");
  e = last_ev (IRC_EV_CTCP);
  CHECK (e != NULL); CHECK_STR (e->arg, "VERSION"); CHECK_STR (e->text, ""); CHECK (!e->reply);
  CHECK_STR (drain (c, 9), "NOTICE bob :\001VERSION natter-test 0.1\001\r\n");
  server (c, 10, ":bob!b@h PRIVMSG testnick :\001PING 12345 678\001\n");
  CHECK_STR (drain (c, 10), "NOTICE bob :\001PING 12345 678\001\r\n");
  server (c, 11, ":bob!b@h PRIVMSG #chan :\001VERSION\001\n");			/* a CTCP to a channel is shown but not answered */
  CHECK_STR (drain (c, 11), "");
  server (c, 12, ":bob!b@h NOTICE testnick :\001VERSION other client 1.0\001\n");	/* a reply is shown and not answered */
  e = last_ev (IRC_EV_CTCP);
  CHECK (e->reply); CHECK_STR (e->text, "other client 1.0");
  CHECK_STR (drain (c, 12), "");

  server (c, 13, "@time=2026-10-29T21:30:00.000Z;msgid=m1;account=bobacct :bob!b@h NOTICE testnick :a notice\n");
  e = last_ev (IRC_EV_NOTICE);
  CHECK_STR (e->text, "a notice"); CHECK_STR (e->time, "2026-10-29T21:30:00.000Z"); CHECK_STR (e->msgid, "m1"); CHECK_STR (e->account, "bobacct");

  server (c, 14, ":testnick!t@h PRIVMSG #chan :my own words\n");			/* echo-message */
  CHECK (last_ev (IRC_EV_PRIVMSG)->self);
  irc_conn_free (c);

  cfg.ctcp_version = NULL;							/* no version text: no answer */
  c = registered (&cfg);
  server (c, 5, ":bob!b@h PRIVMSG testnick :\001VERSION\001\n");
  CHECK_STR (drain (c, 5), "");
  irc_conn_free (c);
}

static void test_events (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  ev_t *e;
  server (c, 5, ":alice!a@h JOIN #chan\n");
  e = last_ev (IRC_EV_JOIN); CHECK_STR (e->from, "alice"); CHECK_STR (e->target, "#chan"); CHECK (!e->self); CHECK (e->account[0] == 0);
  server (c, 6, ":alice!a@h JOIN #chan aliceacct :Alice A\n");			/* extended-join */
  e = last_ev (IRC_EV_JOIN); CHECK_STR (e->account, "aliceacct"); CHECK_STR (e->text, "Alice A");
  server (c, 7, ":bob!b@h JOIN #chan * :Bob B\n");
  e = last_ev (IRC_EV_JOIN); CHECK (e->account[0] == 0); CHECK_STR (e->text, "Bob B");
  server (c, 8, ":testnick!t@h JOIN #chan\n");
  CHECK (last_ev (IRC_EV_JOIN)->self);
  server (c, 9, ":alice!a@h PART #chan :see you\n");
  e = last_ev (IRC_EV_PART); CHECK_STR (e->target, "#chan"); CHECK_STR (e->text, "see you");
  server (c, 10, ":alice!a@h QUIT :Ping timeout: 240 seconds\n");
  CHECK_STR (last_ev (IRC_EV_QUIT)->text, "Ping timeout: 240 seconds");
  server (c, 11, ":alice!a@h NICK alicia\n");
  e = last_ev (IRC_EV_NICK); CHECK_STR (e->from, "alice"); CHECK_STR (e->arg, "alicia"); CHECK (!e->self);
  server (c, 12, ":TestNick!t@h NICK newname\n");				/* our own nick, in other case: also ours */
  e = last_ev (IRC_EV_NICK); CHECK (e->self); CHECK_STR (irc_conn_nick (c), "newname");
  server (c, 13, ":op!o@h KICK #chan newname :behave\n");
  e = last_ev (IRC_EV_KICK); CHECK_STR (e->target, "#chan"); CHECK_STR (e->arg, "newname"); CHECK_STR (e->text, "behave"); CHECK (e->self);
  server (c, 14, ":op!o@h KICK #chan bob\n");
  e = last_ev (IRC_EV_KICK); CHECK (!e->self); CHECK_STR (e->text, "");
  server (c, 15, ":op!o@h TOPIC #chan :new topic: with colon\n");
  e = last_ev (IRC_EV_TOPIC); CHECK_STR (e->target, "#chan"); CHECK_STR (e->text, "new topic: with colon");
  server (c, 16, ":op!o@h MODE #chan +ov alice bob\n");
  e = last_ev (IRC_EV_MODE); CHECK_STR (e->target, "#chan"); CHECK_STR (e->text, "+ov alice bob");
  server (c, 17, ":op!o@h INVITE newname #secret\n");
  e = last_ev (IRC_EV_INVITE); CHECK_STR (e->arg, "newname"); CHECK_STR (e->target, "#secret");
  server (c, 18, ":irc 332 newname #chan :The topic of the channel\n");
  e = last_ev (IRC_EV_TOPIC); CHECK_INT (e->numeric, 332); CHECK_STR (e->target, "#chan"); CHECK_STR (e->text, "The topic of the channel");	/* the topic that comes with a join  */
  server (c, 18, ":irc 372 newname :- a line of the message of the day\n");
  e = last_ev (IRC_EV_NUMERIC); CHECK_INT (e->numeric, 372); CHECK_STR (e->target, "newname"); CHECK_STR (e->text, "- a line of the message of the day");
  server (c, 19, "ERROR :Closing Link: (Quit)\n");
  CHECK_STR (last_ev (IRC_EV_ERROR)->text, "Closing Link: (Quit)");
  server (c, 20, ":irc UNKNOWNCMD x y\n");					/* ignored without harm */
  server (c, 21, "garbage without a command\n");
  irc_conn_free (c);
}

static void test_isupport (void)
{
  irc_config cfg = basic ();
  irc_conn *c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc 001 testnick :Welcome\n"
		":irc 005 testnick CASEMAPPING=ascii NETWORK=TestNet NICKLEN=20 PREFIX=(qaohv)~&@%+ CHANTYPES=#+ STATUSMSG=@+ WHOX :are supported by this server\n");
  CHECK_STR (irc_conn_network (c), "TestNet"); CHECK_STR (irc_conn_isupport (c, "NICKLEN"), "20"); CHECK_STR (irc_conn_isupport (c, "PREFIX"), "(qaohv)~&@%+");
  CHECK_STR (irc_conn_isupport (c, "WHOX"), ""); CHECK (irc_conn_isupport (c, "NOPE") == NULL);
  CHECK_INT (irc_conn_casemap (c), IRC_CASE_ASCII);
  CHECK (irc_is_channel_name (c, "#chan")); CHECK (irc_is_channel_name (c, "+chan")); CHECK (!irc_is_channel_name (c, "&chan"));	/* CHANTYPES=#+ */
  CHECK (irc_is_channel_name (c, "@#chan")); CHECK (irc_is_channel_name (c, "@+chan")); CHECK (!irc_is_channel_name (c, "nick")); CHECK (!irc_is_channel_name (c, ""));
  CHECK (irc_conn_casecmp (c, "Nick[]", "nick{}") != 0);				/* ascii mapping: [] and {} differ */
  server (c, 2, ":irc 005 testnick -WHOX CASEMAPPING=rfc1459 :are supported by this server\n");
  CHECK (irc_conn_isupport (c, "WHOX") == NULL); CHECK (irc_conn_casecmp (c, "Nick[]", "nick{}") == 0);
  irc_conn_free (c);
  cfg = basic ();
  c = irc_conn_new (&cfg, on_event, NULL);
  CHECK (irc_is_channel_name (c, "#x")); CHECK (irc_is_channel_name (c, "&x")); CHECK (!irc_is_channel_name (c, "+x"));	/* defaults: # and & */
  irc_conn_free (c);
}

static void test_flood_limit (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  const char *o;
  int i, lines;
  for (i = 0; i < 8; i++) { char t[32]; snprintf (t, sizeof t, "msg %d", i); irc_say (c, "#chan", t); }
  CHECK (irc_conn_wants_output (c, 100));
  o = drain (c, 100);
  for (lines = 0, i = 0; o[i]; i++) if (o[i] == '\n') lines++;
  CHECK_INT (lines, 5);								/* the burst */
  CONTAINS (o, "PRIVMSG #chan :msg 4\r\n"); ABSENT (o, "msg 5");
  CHECK (!irc_conn_wants_output (c, 150));
  CHECK_STR (drain (c, 150), "");							/* half an interval: nothing */
  server (c, 150, "PING :x\n");
  CHECK_STR (drain (c, 150), "PONG :x\r\n");					/* a PONG does not wait for the limit */
  CHECK_STR (drain (c, 200), "PRIVMSG #chan :msg 5\r\n");				/* one line for each interval of 1 second (100 cs) */
  CHECK_STR (drain (c, 250), "");
  CHECK_STR (drain (c, 300), "PRIVMSG #chan :msg 6\r\n");
  CHECK_STR (drain (c, 1000), "PRIVMSG #chan :msg 7\r\n");
  for (i = 0; i < 9; i++) irc_say (c, "#chan", "x");
  for (lines = 0, o = drain (c, 5000), i = 0; o[i]; i++) if (o[i] == '\n') lines++;	/* a long wait gives the burst back, and no more */
  CHECK_INT (lines, 5);
  /* a small output buffer: only whole lines go in */
  {
    char small[40];
    size_t n = irc_conn_output (c, 1300, small, sizeof small);
    CHECK (n <= sizeof small); CHECK (n == 0 || (small[n - 1] == '\n' && small[n - 2] == '\r'));
  }
  irc_conn_free (c);
}

static int max_line (const char *o)		/* the longest line without CR LF */
{
  int best = 0, cur = 0;
  for (; *o; o++) { if (*o == '\r') { if (cur > best) best = cur; cur = 0; } else if (*o != '\n') cur++; }
  return best;
}

static void test_commands (void)
{
  irc_config cfg = basic ();
  irc_conn *c;
  const char *o;
  char longtext[4000], *p;
  int i;
  cfg.burst = 100;
  c = registered (&cfg);
  irc_join (c, "#chan", NULL); irc_join (c, "#key", "sec ret"); irc_part (c, "#chan", "bye now"); irc_part (c, "#x", NULL);
  irc_set_nick (c, "other"); irc_set_topic (c, "#chan", "a topic"); irc_set_topic (c, "#chan", NULL);
  irc_action (c, "#chan", "dances");
  o = drain (c, 1000);
  CHECK_STR (o, "JOIN #chan\r\nJOIN #key sec\r\nPART #chan :bye now\r\nPART #x\r\nNICK other\r\nTOPIC #chan :a topic\r\nTOPIC #chan\r\nPRIVMSG #chan :\001ACTION dances\001\r\n");
  irc_conn_free (c);

  cfg.burst = 100;
  c = registered (&cfg);
  /* injection: a CR or LF ends the text, so no second command can be smuggled in; a newline in a say is a line of its own */
  irc_say (c, "#chan", "first\r\nQUIT :pwned");
  irc_send_raw (c, "PRIVMSG #chan :ok\r\nQUIT :pwned");
  irc_join (c, "#a\r\nQUIT", NULL);
  irc_say (c, "bob\r\nQUIT :x", "hi");
  o = drain (c, 1000);
  CONTAINS (o, "PRIVMSG #chan :first\r\n"); CONTAINS (o, "PRIVMSG #chan :QUIT :pwned\r\n"); CONTAINS (o, "PRIVMSG #chan :ok\r\n"); CONTAINS (o, "JOIN #a\r\n"); CONTAINS (o, "PRIVMSG bob :hi\r\n");
  { const char *q = o; int bad = 0; while ((q = strstr (q, "\r\nQUIT")) != NULL) { bad++; q++; } CHECK_INT (bad, 0); CHECK (strncmp (o, "QUIT", 4) != 0); }
  irc_conn_free (c);

  /* long texts are cut at spaces, never inside a character, and every line fits in 512 bytes with its CR LF */
  cfg.burst = 100;
  c = registered (&cfg);
  for (p = longtext, i = 0; p < longtext + sizeof longtext - 20; i++) p += sprintf (p, "word%d caf\xc3\xa9 ", i);
  irc_say (c, "#chan", longtext);
  o = drain (c, 1000);
  CHECK (max_line (o) <= 510);
  CHECK (max_line (o) > 300);
  {
    /* all the words come back, in order, none broken */
    const char *q = o;
    int n = 0, expect = 0;
    while ((q = strstr (q, "PRIVMSG #chan :")) != NULL) { q += 15; n++; }
    CHECK (n >= 10);
    for (q = o; *q; q++) if ((unsigned char) *q >= 0x80 && ((unsigned char) *q & 0xC0) == 0xC0) { if ((unsigned char) q[1] < 0x80 || (unsigned char) q[1] > 0xBF) expect = 1; }
    CHECK_INT (expect, 0);								/* every lead byte is followed by a continuation byte */
    CONTAINS (o, "word0 caf\xc3\xa9"); CONTAINS (o, "word20 caf\xc3\xa9");
  }
  /* a word longer than the room is cut hard, inside no character */
  {
    char word[1500];
    memset (word, 0, sizeof word);
    for (i = 0; i < 700; i++) strcat (word, "\xc3\xa9");
    irc_say (c, "#chan", word);
    o = drain (c, 1000);
    CHECK (max_line (o) <= 510);
    { const char *q = o; int bad = 0; for (; *q; q++) if ((unsigned char) *q == 0xC3 && (unsigned char) q[1] != 0xA9) bad++; CHECK_INT (bad, 0); }
  }
  irc_conn_free (c);

  /* an over-long raw line is cut at 510 bytes */
  c = registered (&cfg);
  { char raw[900]; memset (raw, 'a', sizeof raw); memcpy (raw, "PRIVMSG #c :", 12); raw[899] = 0; irc_send_raw (c, raw); }
  CHECK (max_line (drain (c, 1000)) == 510);
  irc_conn_free (c);

  /* a word in the middle that is longer than a line used to make the line builder loop for ever */
  c = registered (&cfg);
  { char w[3000]; memset (w, 'x', sizeof w); w[2999] = 0; irc_join (c, w, w); irc_part (c, w, w); irc_say (c, w, "hi"); }
  o = drain (c, 1000);
  CHECK (max_line (o) <= 510); CHECK (strlen (o) > 500);
  irc_conn_free (c);

  /* a quit */
  c = registered (&cfg);
  irc_quit (c, "Leaving");
  CHECK_STR (drain (c, 1000), "QUIT :Leaving\r\n");
  irc_conn_free (c);
}

static void test_idle (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  nev = 0;
  irc_conn_tick (c, 1000);
  CHECK_STR (drain (c, 1000), "");
  irc_conn_tick (c, 9000 + 100);							/* 90 s of silence since the registration (time 1) */
  CHECK_STR (drain (c, 9100), "PING :natter\r\n");
  irc_conn_tick (c, 9200);
  CHECK_INT (count_ev (IRC_EV_TIMEOUT), 0);
  server (c, 9300, ":irc PONG irc :natter\n");					/* any input resets it */
  irc_conn_tick (c, 9400);
  CHECK_STR (drain (c, 9400), "");
  irc_conn_tick (c, 9300 + 9000);
  CHECK_STR (drain (c, 18300), "PING :natter\r\n");
  irc_conn_tick (c, 18300 + 5999);
  CHECK_INT (count_ev (IRC_EV_TIMEOUT), 0);
  irc_conn_tick (c, 18300 + 6000);
  CHECK_INT (count_ev (IRC_EV_TIMEOUT), 1);
  irc_conn_tick (c, 18300 + 7000);
  CHECK_INT (count_ev (IRC_EV_TIMEOUT), 1);					/* only once */
  irc_conn_free (c);

  /* the time counter wraps around: 32 bits of centiseconds are 497 days */
  c = registered (&cfg);
  server (c, 0xFFFFFF00u, ":irc PONG irc x\n");
  irc_conn_tick (c, 0xFFFFFF00u + 8000);
  CHECK_STR (drain (c, 0xFFFFFF00u + 8000), "");
  irc_conn_tick (c, 0xFFFFFF00u + 9000);						/* wrapped past 0 */
  CHECK_STR (drain (c, 0xFFFFFF00u + 9000), "PING :natter\r\n");
  irc_conn_free (c);
}

static void test_reconnect (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  server (c, 5, ":irc 005 testnick NETWORK=TestNet CASEMAPPING=ascii :x\n:testnick!t@h NICK renamed\n");
  irc_say (c, "#chan", "pending");
  CHECK_STR (irc_conn_nick (c), "renamed");
  irc_conn_start (c, 100);							/* a new connection: everything is as at the start */
  CHECK_STR (irc_conn_nick (c), "testnick"); CHECK (!irc_conn_registered (c)); CHECK (irc_conn_network (c) == NULL); CHECK_INT (irc_conn_casemap (c), IRC_CASE_RFC1459);
  CHECK_STR (drain (c, 100), "CAP LS 302\r\nNICK testnick\r\nUSER tuser 0 * :Test Nick\r\n");
  irc_conn_free (c);
}

static void test_cap_new_del (void)
{
  irc_config cfg = basic ();
  irc_conn *c = irc_conn_new (&cfg, on_event, NULL);
  irc_conn_start (c, 0); drain (c, 0);
  server (c, 1, ":irc CAP * LS :server-time\n");
  CHECK_STR (drain (c, 1), "CAP REQ :server-time\r\n");
  server (c, 2, ":irc CAP testnick ACK :server-time\n");
  CHECK_STR (drain (c, 2), "CAP END\r\n");
  server (c, 3, ":irc 001 testnick :Welcome\n");
  server (c, 4, ":irc CAP testnick NEW :away-notify=x echo-message\n");		/* a capability that the server offers later and we want */
  CHECK_STR (drain (c, 4), "CAP REQ :away-notify echo-message\r\n");
  server (c, 5, ":irc CAP testnick ACK :away-notify\n");
  CHECK (irc_conn_cap_enabled (c, "away-notify"));
  CHECK_STR (drain (c, 5), "");
  server (c, 6, ":irc CAP testnick DEL :away-notify server-time\n");
  CHECK (!irc_conn_cap_enabled (c, "away-notify")); CHECK (!irc_conn_cap_enabled (c, "server-time"));
  server (c, 7, ":irc CAP testnick ACK :-server-time\n");				/* a capability switched off by the server */
  CHECK (!irc_conn_cap_enabled (c, "server-time"));
  irc_conn_free (c);
}


/* ---- channels ---- */

static const char *names_of (irc_conn *c, const irc_channel *ch)	/* "&admin %half @op ..." in the order of the list */
{
  static char buf[8][4096];
  static int k;
  char *b = buf[k++ & 7];
  size_t i, n = 0;
  b[0] = 0;
  for (i = 0; i < irc_channel_nmembers (ch); i++)
    {
      const irc_member *m = irc_channel_member (c, ch, i);
      char sym = irc_member_symbol (c, m);
      n += (size_t) snprintf (b + n, 4096 - n, "%s%s%c%s", n ? " " : "", "", sym ? sym : '-', m->user->nick);
    }
  return b;
}

static int sorted (irc_conn *c, const irc_channel *ch)
{
  size_t i;
  for (i = 1; i < irc_channel_nmembers (ch); i++)
    if (irc_conn_casecmp (c, irc_channel_member (c, ch, i - 1)->user->nick, irc_channel_member (c, ch, i)->user->nick) >= 0) return 0;
  return 1;
}

static irc_conn *in_network (irc_config *cfg)	/* registered, with a 005 line that has the usual PREFIX and CHANMODES */
{
  irc_conn *c = registered (cfg);
  server (c, 2, ":irc 005 testnick CASEMAPPING=rfc1459 PREFIX=(qaohv)~&@%+ CHANTYPES=# CHANMODES=beI,k,l,imnpst :are supported\n");
  g_conn = c;
  nev = 0;
  return c;
}

static void test_channels (void)
{
  irc_config cfg = basic ();
  irc_conn *c = in_network (&cfg);
  const irc_channel *ch;
  const irc_member *m;
  ev_t *e;
  char ms[128];

  /* we join: the channel exists at once, the modes are asked for, the list comes with 353 and ends with 366 */
  server (c, 10, ":testnick!t@h.example JOIN #Chan\n");
  CHECK_INT (irc_conn_nchannels (c), 1);
  ch = irc_conn_channel (c, "#chan");						/* names are case-insensitive */
  CHECK (ch != NULL); CHECK_STR (irc_channel_name (ch), "#Chan"); CHECK_INT (irc_channel_nmembers (ch), 1); CHECK (!irc_channel_synced (ch));
  CHECK_STR (drain (c, 10), "MODE #Chan\r\n");
  CHECK_INT (last_ev (IRC_EV_JOIN)->self, 1); CHECK_INT (last_ev (IRC_EV_JOIN)->chan_members, 1);
  server (c, 11, ":irc 353 testnick = #Chan :@op +voice testnick plain @+bob!bu@bh.example\n:irc 353 testnick = #Chan :~owner &admin %half plain\n");
  CHECK (!irc_channel_synced (ch));
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 0);					/* the list is not shown as numerics */
  server (c, 12, ":irc 366 testnick #Chan :End of /NAMES list.\n");
  CHECK (irc_channel_synced (ch)); CHECK_INT (count_ev (IRC_EV_NAMES), 1); CHECK_STR (last_ev (IRC_EV_NAMES)->target, "#Chan");
  CHECK_INT (irc_channel_nmembers (ch), 8);					/* plain was listed twice */
  CHECK_STR (names_of (c, ch), "&admin @bob %half @op ~owner -plain -testnick +voice");
  CHECK (sorted (c, ch));
  m = irc_channel_find (c, ch, "BOB");
  CHECK (m != NULL); CHECK_STR (m->user->user, "bu"); CHECK_STR (m->user->host, "bh.example"); CHECK_INT (irc_member_rank (c, m), 2);	/* @ and + (multi-prefix): the highest counts */
  CHECK_INT (irc_member_rank (c, irc_channel_find (c, ch, "plain")), 5); CHECK_INT (irc_member_symbol (c, irc_channel_find (c, ch, "plain")), 0);
  CHECK_INT (irc_member_rank (c, irc_channel_find (c, ch, "owner")), 0);
  CHECK (irc_channel_find (c, ch, "nobody") == NULL);
  CHECK_STR (irc_conn_prefix_modes (c), "qaohv"); CHECK_STR (irc_conn_prefix_symbols (c), "~&@%+");

  /* others come and go; the state is right when the event arrives */
  server (c, 13, ":alice!au@ah JOIN #Chan aliceacct :Alice A\n");
  CHECK_INT (irc_channel_nmembers (ch), 9); CHECK_INT (last_ev (IRC_EV_JOIN)->chan_members, 9);
  CHECK_STR (irc_conn_user (c, "alice")->account, "aliceacct"); CHECK_STR (irc_conn_user (c, "alice")->realname, "Alice A"); CHECK_STR (irc_conn_user (c, "alice")->host, "ah");
  server (c, 14, ":zed!z@h JOIN #unknown\n");					/* a channel that we are not in: the event comes, nothing is kept */
  CHECK (irc_conn_user (c, "zed") == NULL); CHECK_INT (last_ev (IRC_EV_JOIN)->chan_members, -1);
  server (c, 15, ":alice!au@ah PART #Chan :bye\n");
  CHECK_INT (irc_channel_nmembers (ch), 8); CHECK_INT (last_ev (IRC_EV_PART)->chan_members, 8); CHECK (irc_conn_user (c, "alice") == NULL);	/* nobody shares a channel with us any more */
  server (c, 16, ":op!o@h KICK #Chan plain :behave\n");
  CHECK_INT (irc_channel_nmembers (ch), 7); CHECK (irc_channel_find (c, ch, "plain") == NULL); CHECK_INT (last_ev (IRC_EV_KICK)->self, 0);
  server (c, 17, ":alice!au@ah PART #Chan\n");					/* leaving a channel that one is not in: harmless */
  CHECK_INT (irc_channel_nmembers (ch), 7);

  /* a second channel; a person in both */
  server (c, 18, ":testnick!t@h.example JOIN #two\n:irc 353 testnick = #two :testnick bob voice\n:irc 366 testnick #two :End\n");
  CHECK_INT (irc_conn_nchannels (c), 2);
  CHECK_STR (irc_channel_name (irc_conn_channel_at (c, 0)), "#Chan"); CHECK_STR (irc_channel_name (irc_conn_channel_at (c, 1)), "#two");	/* in the order of joining */
  nev = 0;
  server (c, 19, ":bob!bu@bh.example NICK robert\n");
  CHECK_INT (count_ev (IRC_EV_NICK), 2);					/* one event for each channel */
  CHECK_STR (evs[0].target, "#Chan"); CHECK_STR (evs[1].target, "#two"); CHECK_STR (evs[0].arg, "robert"); CHECK_STR (evs[0].from, "bob");
  CHECK (irc_conn_user (c, "bob") == NULL); CHECK (irc_conn_user (c, "robert") != NULL);
  CHECK_STR (names_of (c, ch), "&admin %half @op ~owner @robert -testnick +voice");	/* renamed, in the new place, prefixes kept */
  CHECK (sorted (c, ch)); CHECK (sorted (c, irc_conn_channel (c, "#two"))); CHECK_INT (irc_conn_selfcheck (c), 0);
  nev = 0;
  server (c, 20, ":robert!bu@bh.example QUIT :Ping timeout\n");
  CHECK_INT (count_ev (IRC_EV_QUIT), 2); CHECK_STR (evs[0].target, "#Chan"); CHECK_STR (evs[1].target, "#two"); CHECK_STR (evs[0].text, "Ping timeout");
  CHECK_INT (evs[0].chan_members, 6);
  CHECK (irc_conn_user (c, "robert") == NULL); CHECK_INT (irc_channel_nmembers (ch), 6); CHECK_INT (irc_channel_nmembers (irc_conn_channel (c, "#two")), 2);
  nev = 0;
  server (c, 21, ":stranger!s@h QUIT :gone\n");					/* somebody we do not know: one event, no target */
  CHECK_INT (count_ev (IRC_EV_QUIT), 1); CHECK_STR (evs[0].target, "");
  nev = 0;
  server (c, 22, ":testnick!t@h.example NICK ourselves\n");			/* our own nick: in both channels, and ours is updated */
  CHECK_INT (count_ev (IRC_EV_NICK), 2); CHECK_INT (evs[0].self, 1); CHECK_STR (irc_conn_nick (c), "ourselves");
  CHECK (irc_channel_find (c, ch, "ourselves") != NULL); CHECK (irc_channel_find (c, ch, "testnick") == NULL);

  /* modes of members and of the channel */
  server (c, 23, ":op!o@h MODE #Chan +o half\n");
  CHECK_INT (irc_member_rank (c, irc_channel_find (c, ch, "half")), 2);		/* o is the third mode of qaohv: it wins over the h that half already has */
  CHECK_INT (irc_member_symbol (c, irc_channel_find (c, ch, "half")), '@');
  server (c, 24, ":op!o@h MODE #Chan -o+v op half\n");
  CHECK_INT (irc_member_symbol (c, irc_channel_find (c, ch, "op")), 0);		/* op lost @ and has no other prefix */
  CHECK_INT (irc_member_symbol (c, irc_channel_find (c, ch, "half")), '@');	/* half has o, h and v now */
  CHECK_STR (last_ev (IRC_EV_MODE)->text, "-o+v op half");
  server (c, 24, ":op!o@h MODE #Chan -o half\n");
  CHECK_INT (irc_member_symbol (c, irc_channel_find (c, ch, "half")), '%');
  server (c, 25, ":op!o@h MODE #Chan +nt-b+k *!*@bad.example secret\n");		/* b has an argument that is not kept; k has one that is */
  irc_channel_modestring (ch, ms, sizeof ms); CHECK_STR (ms, "+knt secret");
  server (c, 26, ":op!o@h MODE #Chan +l 50\n");
  irc_channel_modestring (ch, ms, sizeof ms); CHECK_STR (ms, "+klnt secret 50");
  server (c, 27, ":op!o@h MODE #Chan -k+i *\n");				/* -k takes an argument (type B), -l none (type C) */
  irc_channel_modestring (ch, ms, sizeof ms); CHECK_STR (ms, "+ilnt 50");
  server (c, 28, ":op!o@h MODE ourselves +i\n");				/* a user mode: shown, nothing kept */
  CHECK_STR (last_ev (IRC_EV_MODE)->target, "ourselves");
  nev = 0;
  server (c, 29, ":irc 324 ourselves #Chan +ntk newkey\n");			/* the reply to our MODE query replaces the modes, and is shown as well */
  irc_channel_modestring (ch, ms, sizeof ms); CHECK_STR (ms, "+knt newkey");
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 1);
  { char tiny[4]; size_t n = irc_channel_modestring (ch, tiny, sizeof tiny); CHECK (n <= 3); CHECK_INT (tiny[n], 0); }	/* a small buffer is respected */

  /* topics */
  server (c, 30, ":irc 332 ourselves #Chan :The first topic\n:irc 333 ourselves #Chan setter!s@h 1700000000\n");
  CHECK_STR (irc_channel_topic (ch), "The first topic"); CHECK_STR (irc_channel_topic_by (ch), "setter!s@h"); CHECK (irc_channel_topic_time (ch) == 1700000000UL);
  server (c, 31, ":op!o@h TOPIC #Chan :A new topic\n");
  CHECK_STR (irc_channel_topic (ch), "A new topic"); CHECK_STR (irc_channel_topic_by (ch), "op!o@h");
  server (c, 32, ":irc 331 ourselves #Chan :No topic is set\n");
  CHECK_STR (irc_channel_topic (ch), "");

  /* the notifications */
  nev = 0;
  server (c, 33, ":voice!v@vh ACCOUNT voiceacct\n");
  e = last_ev (IRC_EV_ACCOUNT); CHECK_STR (e->arg, "voiceacct"); CHECK_STR (irc_conn_user (c, "voice")->account, "voiceacct");
  server (c, 34, ":voice!v@vh ACCOUNT *\n");
  CHECK (irc_conn_user (c, "voice")->account == NULL); CHECK_STR (last_ev (IRC_EV_ACCOUNT)->arg, "");
  server (c, 35, ":voice!v@vh AWAY :gone fishing\n");
  CHECK_INT (irc_conn_user (c, "voice")->away, 1); CHECK_STR (last_ev (IRC_EV_AWAY)->text, "gone fishing");
  server (c, 36, ":voice!v@vh AWAY\n");
  CHECK_INT (irc_conn_user (c, "voice")->away, 0); CHECK_STR (last_ev (IRC_EV_AWAY)->text, "");
  server (c, 37, ":voice!v@vh CHGHOST newuser new.host.example\n");
  CHECK_STR (irc_conn_user (c, "voice")->user, "newuser"); CHECK_STR (irc_conn_user (c, "voice")->host, "new.host.example"); CHECK_STR (last_ev (IRC_EV_CHGHOST)->arg, "newuser");
  server (c, 38, ":voice!newuser@new.host.example SETNAME :A New Name\n");
  CHECK_STR (irc_conn_user (c, "voice")->realname, "A New Name"); CHECK_STR (last_ev (IRC_EV_SETNAME)->text, "A New Name");
  server (c, 39, ":irc 352 ourselves #Chan wu wh.example irc voice G :0 Who Real Name\n");
  CHECK_INT (irc_conn_user (c, "voice")->away, 1); CHECK_STR (irc_conn_user (c, "voice")->realname, "Who Real Name"); CHECK_STR (irc_conn_user (c, "voice")->user, "wu");
  CHECK_INT (last_ev (IRC_EV_NUMERIC)->numeric, 352);				/* a WHO reply is shown too */

  /* a NAMES for a channel that we are not in is only shown */
  nev = 0;
  server (c, 40, ":irc 353 ourselves = #other :a b c\n:irc 366 ourselves #other :End\n");
  CHECK_INT (count_ev (IRC_EV_NUMERIC), 2); CHECK_INT (irc_conn_nchannels (c), 2);

  /* our own PART: the channel can still be read in the callback, and is gone afterwards */
  nev = 0;
  server (c, 41, ":ourselves!t@h.example PART #two :leaving\n");
  CHECK_INT (last_ev (IRC_EV_PART)->self, 1); CHECK_INT (last_ev (IRC_EV_PART)->chan_members, 2);	/* ourselves and voice are in #two */
  CHECK_INT (irc_conn_nchannels (c), 1); CHECK (irc_conn_channel (c, "#two") == NULL); CHECK_INT (irc_conn_selfcheck (c), 0);
  CHECK_INT (irc_conn_user (c, "voice") != NULL, 1);				/* still in #Chan */
  server (c, 42, ":op!o@h KICK #Chan ourselves :out\n");
  CHECK_INT (last_ev (IRC_EV_KICK)->self, 1); CHECK_INT (irc_conn_nchannels (c), 0); CHECK (irc_conn_user (c, "voice") == NULL);

  /* joining again starts from nothing */
  server (c, 43, ":ourselves!t@h.example JOIN #Chan\n:irc 353 ourselves = #Chan :ourselves other\n:irc 366 ourselves #Chan :End\n");
  ch = irc_conn_channel (c, "#chan");
  CHECK_INT (irc_channel_nmembers (ch), 2); CHECK_STR (irc_channel_topic (ch), ""); CHECK (irc_channel_synced (ch));
  /* a new connection forgets all of it */
  irc_conn_start (c, 100);
  CHECK_INT (irc_conn_nchannels (c), 0); CHECK (irc_conn_user (c, "other") == NULL); CHECK_STR (irc_conn_prefix_modes (c), "ov");
  g_conn = NULL;
  irc_conn_free (c);						/* the leak checker of the sanitizer looks at what is left */
}

static void test_channel_edge_cases (void)
{
  irc_config cfg = basic ();
  irc_conn *c = in_network (&cfg);
  const irc_channel *ch;
  server (c, 10, ":testnick!t@h JOIN #c\n:irc 353 testnick = #c :@a b c\n:irc 366 testnick #c :End\n");
  ch = irc_conn_channel (c, "#c");
  CHECK_INT (irc_channel_nmembers (ch), 4);
  /* a nick change to a name that somebody has: the old holder (a missed quit) is forgotten */
  server (c, 11, ":b!x@y NICK c\n");
  CHECK_INT (irc_channel_nmembers (ch), 3); CHECK (sorted (c, ch)); CHECK_STR (names_of (c, ch), "@a -c -testnick");
  /* a nick that only differs in case, and the [ ] \ ~ letters of rfc1459 */
  server (c, 12, ":c!x@y NICK C[x]\n");
  CHECK (irc_channel_find (c, ch, "c{x}") != NULL);
  server (c, 13, ":c{x}!x@y QUIT\n");
  CHECK_INT (irc_channel_nmembers (ch), 2);
  /* rubbish in the middle of a list and in modes: nothing breaks */
  server (c, 14, ":irc 353 testnick = #c :  ! !! @ ~~ @!u@h x!y\n:irc 366 testnick #c :End\n");
  CHECK (sorted (c, ch)); CHECK_INT (irc_conn_selfcheck (c), 0);
  server (c, 15, ":op!o@h MODE #c +ovvv-kl-\n:op!o@h MODE #c +\n:op!o@h MODE #c\n");
  server (c, 16, ":irc 324 testnick #c\n:irc 332 testnick\n:irc 333 testnick #c\n:irc 352 testnick\n");
  CHECK_INT (irc_conn_selfcheck (c), 0);
  server (c, 17, ":x!y@z ACCOUNT\n:x!y@z AWAY\n:x!y@z CHGHOST\n:x!y@z SETNAME\n:x!y@z NICK\n:x!y@z KICK\n:x!y@z KICK #c\n:x!y@z JOIN\n:x!y@z PART\n:x!y@z TOPIC\n");
  g_conn = NULL;
  irc_conn_free (c);
}

static void test_casemapping_changes (void)
{
  irc_config cfg = basic ();
  irc_conn *c = registered (&cfg);
  const irc_channel *ch;
  server (c, 2, ":irc 005 testnick CASEMAPPING=ascii CHANTYPES=# :are supported\n");
  CHECK_INT (irc_conn_casemap (c), IRC_CASE_ASCII);
  server (c, 3, ":testnick!t@h JOIN #c\n:irc 353 testnick = #c :a[x] a{x} B\n:irc 366 testnick #c :End\n");
  ch = irc_conn_channel (c, "#c");
  CHECK_INT (irc_channel_nmembers (ch), 4);					/* ascii: a[x] and a{x} are two people */
  server (c, 4, ":irc 005 testnick CASEMAPPING=rfc1459 :are supported\n");	/* the mapping is fixed once we know somebody: this would put the list out of order */
  CHECK_INT (irc_conn_casemap (c), IRC_CASE_ASCII); CHECK_INT (irc_conn_selfcheck (c), 0);
  server (c, 5, ":a[x]!u@h QUIT\n:b!u@h QUIT\n");
  CHECK_INT (irc_channel_nmembers (ch), 2); CHECK_INT (irc_conn_selfcheck (c), 0);
  irc_conn_start (c, 100);							/* a new connection used to clear the people with the default mapping instead: freed memory was used */
  CHECK_INT (irc_conn_casemap (c), IRC_CASE_RFC1459); CHECK_INT (irc_conn_nchannels (c), 0);
  irc_conn_free (c);
}

static void test_big_channel (void)
{
  irc_config cfg = basic ();
  irc_conn *c = in_network (&cfg);
  const irc_channel *ch;
  char line[2048];
  int i, k, total = 30000;
  server (c, 10, ":testnick!t@h JOIN #big\n");
  for (i = 0; i < total; )
    {
      size_t n = (size_t) snprintf (line, sizeof line, ":irc 353 testnick = #big :");
      for (k = 0; k < 40 && i < total; k++, i++) n += (size_t) snprintf (line + n, sizeof line - n, "%s%s%05d_n", k ? " " : "", i % 97 == 0 ? "@" : i % 31 == 0 ? "+" : "", (i * 7919) % total);
      line[n++] = '\n'; line[n] = 0;
      irc_conn_input (c, line, n, 11);
    }
  server (c, 12, ":irc 366 testnick #big :End\n");
  ch = irc_conn_channel (c, "#big");
  CHECK_INT (irc_channel_nmembers (ch), total + 1);					/* the numbers are all different, and we are in it too */
  CHECK (sorted (c, ch));
  for (i = 0; i < 1000; i++) { snprintf (line, sizeof line, ":%05d_n!u@h QUIT :bye\n", (i * 13) % total); server (c, 13, line); }
  CHECK (sorted (c, ch));
  for (i = 0; i < 1000; i++) { snprintf (line, sizeof line, ":%05d_n!u@h NICK new%05d\n", (i * 29 + 5) % total, i); server (c, 14, line); }
  CHECK (sorted (c, ch));
  CHECK (irc_channel_nmembers (ch) <= (size_t) total + 1 && irc_channel_nmembers (ch) >= (size_t) total - 1100);
  CHECK_INT (irc_conn_selfcheck (c), 0);
  g_conn = NULL;
  irc_conn_free (c);
}

int main (void)
{
  test_parse ();
  test_lines ();
  test_names ();
  test_registration_without_cap ();
  test_no_caps_option ();
  test_cap_sasl ();
  test_sasl_fail ();
  test_sasl_chunks ();
  test_nick_in_use ();
  test_ping_and_ctcp ();
  test_events ();
  test_isupport ();
  test_flood_limit ();
  test_commands ();
  test_idle ();
  test_reconnect ();
  test_cap_new_del ();
  test_channels ();
  test_channel_edge_cases ();
  test_casemapping_changes ();
  test_big_channel ();
  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
