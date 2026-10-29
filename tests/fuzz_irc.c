/* fuzz_irc.c - feeds the protocol core with random bytes and with mutated server lines while it is also being used (commands, ticks, reconnects), to be run under AddressSanitizer
   and UBSan (make test).  There is nothing to compare: the run must end without a report.   fuzz_irc [iterations [seed]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irc.h"

static unsigned long long state = 88172645463325252ULL;
static unsigned rnd (void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return (unsigned) (state >> 11); }

static const char *corpus[] = {
  ":irc.example CAP * LS * :multi-prefix echo-message server-time sasl=PLAIN,EXTERNAL",
  ":irc.example CAP * LS :away-notify account-notify extended-join chghost cap-notify setname",
  ":irc.example CAP testnick ACK :server-time multi-prefix sasl",
  ":irc.example CAP testnick NAK :server-time",
  ":irc.example CAP testnick NEW :away-notify=x batch",
  ":irc.example CAP testnick DEL :sasl",
  "AUTHENTICATE +",
  ":irc.example 903 testnick :SASL authentication successful",
  ":irc.example 904 testnick :SASL authentication failed",
  ":irc.example 001 testnick :Welcome to the network",
  ":irc.example 005 testnick CASEMAPPING=ascii NETWORK=Test NICKLEN=20 PREFIX=(qaohv)~&@%+ CHANTYPES=#+ STATUSMSG=@+ :are supported",
  ":irc.example 005 testnick -NICKLEN CHANTYPES= PREFIX :are supported",
  ":irc.example 433 * testnick :Nickname is already in use",
  ":irc.example 421 * CAP :Unknown command",
  ":irc.example 332 testnick #chan :The topic",
  "PING :irc.example",
  "@time=2026-10-29T21:30:00.000Z;msgid=abc;account=bob;+draft/x=a\\sb\\:c :bob!b@h PRIVMSG #chan :hello world",
  ":bob!b@h PRIVMSG testnick :\001VERSION\001",
  ":bob!b@h PRIVMSG #chan :\001ACTION waves\001",
  ":bob!b@h NOTICE testnick :\001PING 123\001",
  ":bob!b@h JOIN #chan aliceacct :Real Name",
  ":bob!b@h PART #chan :bye",
  ":bob!b@h QUIT :Quit: gone",
  ":bob!b@h NICK bobby",
  ":testnick!t@h NICK other",
  ":op!o@h KICK #chan testnick :out",
  ":op!o@h TOPIC #chan :new topic",
  ":op!o@h MODE #chan +ov alice bob",
  ":op!o@h INVITE testnick #secret",
  "ERROR :Closing Link",
  "X a b c d e f g h i j k l m n o p q r s t u v w",
};
#define NCORPUS ((int) (sizeof corpus / sizeof *corpus))

static unsigned long events;

static void touch (const char *s, size_t *sum) { if (s) *sum += strlen (s); }
static void on_event (void *ud, const irc_event *e)
{
  size_t *sum = ud;
  int i;
  events++;
  touch (e->time, sum); touch (e->msgid, sum); touch (e->from, sum); touch (e->user, sum); touch (e->host, sum);
  touch (e->target, sum); touch (e->text, sum); touch (e->arg, sum); touch (e->account, sum);
  if (e->msg)
    {
      touch (e->msg->command, sum);
      for (i = 0; i < e->msg->nparams; i++) touch (e->msg->params[i], sum);
      for (i = 0; i < e->msg->ntags; i++) { touch (e->msg->tags[i].key, sum); touch (e->msg->tags[i].value, sum); }
    }
}

static void mutate (char *line, size_t *n, size_t cap)
{
  int k, times = 1 + (int) (rnd () % 4);
  for (k = 0; k < times && *n > 0; k++)
    {
      size_t at = rnd () % *n;
      switch (rnd () % 6)
	{
	case 0: line[at] = (char) rnd (); break;			/* a random byte */
	case 1: *n = at; break;						/* cut off */
	case 2: if (*n + 1 < cap) { memmove (line + at + 1, line + at, *n - at); line[at] = (char) rnd (); (*n)++; } break;
	case 3: line[at] = ' '; break;
	case 4: line[at] = ":@;=\\\r\n\001*"[rnd () % 9]; break;
	case 5: if (*n > 1) { memmove (line + at, line + at + 1, *n - at - 1); (*n)--; } break;
	}
    }
}

int main (int argc, char **argv)
{
  long iterations = argc > 1 ? atol (argv[1]) : 100000, it;
  size_t sum = 0;
  irc_config cfg;
  irc_conn *c;
  unsigned now = 0;
  static char out[1 << 16];
  if (argc > 2) state ^= (unsigned long long) atoll (argv[2]) * 0x9E3779B97F4A7C15ULL;
  memset (&cfg, 0, sizeof cfg);
  cfg.nick = "testnick"; cfg.realname = "Fuzz"; cfg.sasl_user = "acct"; cfg.sasl_pass = "pw"; cfg.ctcp_version = "fuzz"; cfg.burst = 3;
  c = irc_conn_new (&cfg, on_event, &sum);
  irc_conn_start (c, now);
  for (it = 0; it < iterations; it++)
    {
      char buf[4096];
      size_t n = 0, i;
      now += rnd () % 300;
      switch (rnd () % 10)
	{
	case 0: case 1: case 2: case 3: case 4:			/* a mutated line of the corpus */
	  {
	    const char *src = corpus[rnd () % (unsigned) NCORPUS];
	    n = strlen (src); memcpy (buf, src, n);
	    mutate (buf, &n, sizeof buf - 4);
	    buf[n++] = '\r'; buf[n++] = '\n';
	  }
	  break;
	case 5:							/* random bytes of any length, with many newlines */
	  n = rnd () % 600;
	  for (i = 0; i < n; i++) buf[i] = (rnd () % 20 == 0) ? '\n' : (char) rnd ();
	  break;
	case 6:							/* an unmutated line */
	  { const char *src = corpus[rnd () % (unsigned) NCORPUS]; n = strlen (src); memcpy (buf, src, n); buf[n++] = '\n'; }
	  break;
	case 7:							/* commands with strange text */
	  {
	    char text[300];
	    size_t tl = rnd () % 290;
	    for (i = 0; i < tl; i++) text[i] = (char) (1 + rnd () % 255);
	    text[tl] = 0;
	    switch (rnd () % 7)
	      {
	      case 0: irc_say (c, "#chan", text); break;
	      case 1: irc_action (c, text, "x"); break;
	      case 2: irc_send_raw (c, text); break;
	      case 3: irc_join (c, text, text); break;
	      case 4: irc_set_topic (c, text, text); break;
	      case 5: irc_notice (c, "bob", text); break;
	      case 6: irc_part (c, text, text); break;
	      }
	  }
	  break;
	case 8:
	  irc_conn_tick (c, now);
	  break;
	case 9:
	  if (rnd () % 50 == 0) irc_conn_start (c, now);			/* a reconnect now and then */
	  else { size_t o = irc_conn_output (c, now, out, rnd () % 2 ? sizeof out : 64); for (i = 0; i < o; i++) sum += (unsigned char) out[i]; }
	  break;
	}
      /* the input arrives in pieces */
      for (i = 0; i < n; )
	{
	  size_t piece = 1 + rnd () % 200;
	  if (piece > n - i) piece = n - i;
	  irc_conn_input (c, buf + i, piece, now);
	  i += piece;
	}
      if (it % 97 == 0) (void) irc_is_channel_name (c, "#x");
      if (getenv ("FUZZ_PROGRESS") && it % 1000 == 0) fprintf (stderr, "it %ld\n", it);
    }
  irc_conn_free (c);
  printf ("fuzz: %ld iterations, %lu events, checksum %zu: no report\n", iterations, events, sum);
  return 0;
}
