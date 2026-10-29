/* test_net.c - the client side of the tests of net.c (see tests/net/run.sh).
   test_net MODE HOST PORT TLS CAFILE VERIFY [WORD]
   MODE greet: connects, expects the greeting and an answer to PING.  bulk: also receives 500000 bytes and sends 300000.  fail: the connection must end without being up, with WORD in the reason.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "net.h"

static char got[1 << 20]; static size_t ngot;
static int is_up, is_closed; static char why[512];
static void on_status (void *ud, const char *t) { (void) ud; fprintf (stderr, "  status: %s\n", t); }
static void on_connected (void *ud) { (void) ud; is_up = 1; }
static void on_data (void *ud, const char *b, size_t n) { (void) ud; if (ngot + n < sizeof got) { memcpy (got + ngot, b, n); ngot += n; got[ngot] = 0; } }
static void on_closed (void *ud, const char *w) { (void) ud; is_closed = 1; snprintf (why, sizeof why, "%s", w); fprintf (stderr, "  closed: %s\n", w); }

static int wait_for (net *n, int (*cond) (void), int seconds)
{
  time_t end = time (NULL) + seconds;
  while (time (NULL) < end && !is_closed)
    {
      net_poll (n);
      if (cond ()) return 1;
      usleep (5000);
    }
  return cond ();
}
static int c_up (void) { return is_up; }
static int c_hello (void) { return strstr (got, "hello\r\n") != NULL; }
static int c_pong (void) { return strstr (got, "PONG srv :abc\r\n") != NULL; }
static int c_bulk (void) { return strstr (got, "\r\nOK\r\n") != NULL; }
static int c_gotsent (void) { return strstr (got, "GOT 300000\r\n") != NULL; }

int main (int argc, char **argv)
{
  net_events ev = { on_status, on_connected, on_data, on_closed };
  net *n;
  const char *mode = argv[1];
  int port, tls, verify;
  if (argc < 7) { fprintf (stderr, "usage\n"); return 2; }
  port = atoi (argv[3]); tls = atoi (argv[4]); verify = atoi (argv[6]);
  n = net_open (argv[2], port, tls, argv[5][0] ? argv[5] : NULL, verify, &ev, NULL);
  if (!strcmp (mode, "fail"))
    {
      if (n) { time_t end = time (NULL) + 10; while (time (NULL) < end && !is_closed) { net_poll (n); usleep (5000); } }
      if (is_up) { printf ("FAIL: the connection came up\n"); return 1; }
      if (!is_closed) { printf ("FAIL: not closed\n"); return 1; }
      if (argc > 7 && !strstr (why, argv[7])) { printf ("FAIL: the reason \"%s\" does not have \"%s\"\n", why, argv[7]); return 1; }
      printf ("OK: %s\n", why);
      net_free (n);
      return 0;
    }
  if (!n) { printf ("FAIL: net_open: %s\n", why); return 1; }
  if (!wait_for (n, c_up, 10)) { printf ("FAIL: not connected (%s)\n", why); return 1; }
  if (!wait_for (n, c_hello, 5)) { printf ("FAIL: no greeting\n"); return 1; }
  if (net_send (n, "PING :abc\r\n", 11) != 0) { printf ("FAIL: send\n"); return 1; }
  if (!wait_for (n, c_pong, 5)) { printf ("FAIL: no PONG (got: %s)\n", got); return 1; }
  if (!strcmp (mode, "bulk"))
    {
      size_t sent = 0, total = 300000, i;
      char *chunk = malloc (4000);
      size_t before;
      memset (chunk, 'y', 4000);
      ngot = 0; got[0] = 0;
      net_send (n, "BULK 500000\r\n", 13);
      if (!wait_for (n, c_bulk, 20)) { printf ("FAIL: bulk not received: %zu bytes\n", ngot); return 1; }
      for (i = 0; i < ngot && got[i] == 'x'; i++) ;
      if (i != 500000) { printf ("FAIL: %zu x bytes, expected 500000\n", i); return 1; }
      net_send (n, "SEND\r\n", 6);
      ngot = 0; got[0] = 0;
      before = 0;
      while (sent < total)
	{
	  size_t piece = total - sent < 4000 ? total - sent : 4000;
	  if (net_pending (n) < 30000 && net_send (n, chunk, piece) == 0) sent += piece;
	  net_poll (n);
	  if (is_closed) { printf ("FAIL: closed while sending: %s\n", why); return 1; }
	  if (net_pending (n) >= 30000) usleep (2000);
	  (void) before;
	}
      net_send (n, "\r\nEND\r\n", 7);
      if (!wait_for (n, c_gotsent, 20)) { printf ("FAIL: the server did not count 300000 (got: %s)\n", got); return 1; }
      free (chunk);
    }
  net_send (n, "QUIT\r\n", 6);
  net_free (n);
  printf ("OK\n");
  return 0;
}
