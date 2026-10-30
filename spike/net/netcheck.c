/* netcheck.c - a connection to a real server through net.c, to see on the machine what the application will do: the lookup of the name, the connection, the TLS handshake and the check of the certificate.
   netcheck HOST PORT CAFILE      (TLS is always used; the certificate is checked against CAFILE) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "net.h"

static int up, closed;
static void on_status (void *ud, const char *t) { (void) ud; printf ("  status: %s\n", t); fflush (stdout); }
static void on_connected (void *ud) { (void) ud; up = 1; }
static void on_data (void *ud, const char *b, size_t n) { (void) ud; (void) b; (void) n; }
static void on_closed (void *ud, const char *why) { (void) ud; closed = 1; printf ("  closed: %s\n", why); }

int main (int argc, char **argv)
{
  net_events ev = { on_status, on_connected, on_data, on_closed };
  net *n;
  time_t end = time (NULL) + 40;
  if (argc < 4) { printf ("usage: netcheck HOST PORT CAFILE\n"); return 2; }
  n = net_open (argv[1], atoi (argv[2]), 1, argv[3], 1, &ev, NULL);
  if (!n) { printf ("net_open failed\n"); return 1; }
  while (!up && !closed && time (NULL) < end) { net_poll (n); usleep (5000); }
  printf (up ? "RESULT: TLS connection up and the certificate accepted\n" : "RESULT: no connection\n");
  net_free (n);
  return up ? 0 : 1;
}
