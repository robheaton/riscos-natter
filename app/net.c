/* net.c - see net.h. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include "net.h"

#define OUT_MAX 65536			/* the most that is queued for sending */
#define CONNECT_SECONDS 30		/* a connection or handshake that takes longer than this has failed */
#define READ_CHUNK 8192
#define READS_PER_POLL 8

enum { S_CONNECTING, S_HANDSHAKE, S_OPEN, S_DONE };

struct net
{
  int fd, state, tls, verify;
  SSL_CTX *ctx; SSL *ssl;
  struct addrinfo *res, *cur;
  char *host, *cafile;
  net_events ev; void *ud;
  time_t started;
  char *out; size_t outlen, outcap;
};

static void say (net *n, const char *fmt, ...)
{
  char t[512];
  va_list ap;
  if (!n->ev.status) return;
  va_start (ap, fmt); vsnprintf (t, sizeof t, fmt, ap); va_end (ap);
  n->ev.status (n->ud, t);
}

/* the text of the last error of OpenSSL, or of the certificate check */
static void tls_error_text (net *n, char *buf, size_t size)
{
  unsigned long e = ERR_get_error ();
  long v = n->ssl ? SSL_get_verify_result (n->ssl) : X509_V_OK;
  if (v != X509_V_OK) snprintf (buf, size, "the certificate of the server was not accepted: %s", X509_verify_cert_error_string (v));
  else if (e) { char t[200]; ERR_error_string_n (e, t, sizeof t); snprintf (buf, size, "TLS failed: %s", t); }
  else snprintf (buf, size, "TLS failed");
  ERR_clear_error ();
}

static void finish (net *n, const char *why)			/* the connection is over */
{
  if (n->state == S_DONE) return;
  n->state = S_DONE;
  if (n->ssl) { SSL_free (n->ssl); n->ssl = NULL; }
  if (n->fd >= 0) { close (n->fd); n->fd = -1; }
  if (n->ev.closed) n->ev.closed (n->ud, why);
}

static int set_nonblocking (int fd)
{
  int fl = fcntl (fd, F_GETFL, 0);
  return fl < 0 ? -1 : fcntl (fd, F_SETFL, fl | O_NONBLOCK);
}

/* starts a connection to the address that cur points at, and moves cur on to the next one; 0 if started, -1 if there is no address left */
static int try_next_address (net *n)
{
  while (n->cur)
    {
      struct addrinfo *a = n->cur;
      char ip[80] = "";
      n->cur = a->ai_next;
      if (a->ai_family == AF_INET) inet_ntop (AF_INET, &((struct sockaddr_in *) a->ai_addr)->sin_addr, ip, sizeof ip);
      else if (a->ai_family == AF_INET6) inet_ntop (AF_INET6, &((struct sockaddr_in6 *) a->ai_addr)->sin6_addr, ip, sizeof ip);
      n->fd = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
      if (n->fd < 0) continue;
      if (set_nonblocking (n->fd) < 0) { close (n->fd); n->fd = -1; continue; }
      say (n, "Connecting to %s (%s)", n->host, ip);
      if (connect (n->fd, a->ai_addr, a->ai_addrlen) == 0 || errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN)
	{
	  n->state = S_CONNECTING; n->started = time (NULL);
	  return 0;
	}
      say (n, "Could not connect to %s: %s", ip, strerror (errno));
      close (n->fd); n->fd = -1;
    }
  return -1;
}

static int start_tls (net *n)
{
  struct in6_addr a6; struct in_addr a4;
  n->ctx = SSL_CTX_new (TLS_client_method ());
  if (!n->ctx) return -1;
  SSL_CTX_set_min_proto_version (n->ctx, TLS1_2_VERSION);
  SSL_CTX_set_mode (n->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
  if (n->verify)
    {
      if (n->cafile ? SSL_CTX_load_verify_locations (n->ctx, n->cafile, NULL) != 1 : SSL_CTX_set_default_verify_paths (n->ctx) != 1)
	{
	  say (n, "Could not read the certificates in %s", n->cafile ? n->cafile : "the default places");
	  return -1;
	}
      SSL_CTX_set_verify (n->ctx, SSL_VERIFY_PEER, NULL);
    }
  n->ssl = SSL_new (n->ctx);
  if (!n->ssl) return -1;
  SSL_set_fd (n->ssl, n->fd);
  if (inet_pton (AF_INET, n->host, &a4) == 1 || inet_pton (AF_INET6, n->host, &a6) == 1)
    { if (n->verify) X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (n->ssl), n->host); }		/* an address, not a name: no SNI, the certificate must name the address */
  else
    {
      SSL_set_tlsext_host_name (n->ssl, n->host);							/* SNI */
      if (n->verify) X509_VERIFY_PARAM_set1_host (SSL_get0_param (n->ssl), n->host, 0);	/* the name in the certificate must be the name that we asked for */
    }
  SSL_set_connect_state (n->ssl);
  return 0;
}

net *net_open (const char *host, int port, int tls, const char *cafile, int verify, const net_events *ev, void *ud)
{
  net *n = calloc (1, sizeof *n);
  struct addrinfo hints;
  char portstr[16];
  int rc;
  if (!n) return NULL;
  n->fd = -1; n->tls = tls; n->verify = verify; n->ev = *ev; n->ud = ud;
  n->host = strdup (host); n->cafile = cafile ? strdup (cafile) : NULL;
#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);
#endif
  memset (&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  snprintf (portstr, sizeof portstr, "%d", port);
  say (n, "Looking up %s", host);
  rc = getaddrinfo (host, portstr, &hints, &n->res);
  if (rc != 0)
    {
      char why[200];
      snprintf (why, sizeof why, "Could not find %s: %s", host, gai_strerror (rc));
      n->state = S_DONE;
      if (ev->closed) ev->closed (ud, why);
      net_free (n);
      return NULL;
    }
  n->cur = n->res;
  if (try_next_address (n) < 0)
    {
      n->state = S_DONE;
      if (ev->closed) ev->closed (ud, "Could not connect to any address of the server");
      net_free (n);
      return NULL;
    }
  return n;
}

static void flush_out (net *n)
{
  while (n->outlen && n->state == S_OPEN)
    {
      long w;
      if (n->ssl)
	{
	  int e;
	  w = SSL_write (n->ssl, n->out, (int) n->outlen);
	  if (w <= 0)
	    {
	      e = SSL_get_error (n->ssl, (int) w);
	      if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) return;
	      { char why[300]; tls_error_text (n, why, sizeof why); finish (n, why); }
	      return;
	    }
	}
      else
	{
	  w = (long) send (n->fd, n->out, n->outlen, 0);
	  if (w < 0)
	    {
	      if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) return;
	      { char why[200]; snprintf (why, sizeof why, "Could not send: %s", strerror (errno)); finish (n, why); }
	      return;
	    }
	}
      memmove (n->out, n->out + w, n->outlen - (size_t) w);
      n->outlen -= (size_t) w;
    }
}

static void read_some (net *n)
{
  int reads;
  char buf[READ_CHUNK];
  for (reads = 0; reads < READS_PER_POLL && n->state == S_OPEN; reads++)
    {
      long r;
      if (n->ssl)
	{
	  int e;
	  r = SSL_read (n->ssl, buf, (int) sizeof buf);
	  if (r <= 0)
	    {
	      e = SSL_get_error (n->ssl, (int) r);
	      if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return;
	      if (e == SSL_ERROR_ZERO_RETURN) { finish (n, "The server closed the connection"); return; }
	      if (e == SSL_ERROR_SYSCALL && errno == 0) { finish (n, "The server closed the connection"); return; }
	      { char why[300]; tls_error_text (n, why, sizeof why); finish (n, why); }
	      return;
	    }
	}
      else
	{
	  r = (long) recv (n->fd, buf, sizeof buf, 0);
	  if (r == 0) { finish (n, "The server closed the connection"); return; }
	  if (r < 0)
	    {
	      if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) return;
	      { char why[200]; snprintf (why, sizeof why, "Could not read: %s", strerror (errno)); finish (n, why); }
	      return;
	    }
	}
      if (n->ev.data) n->ev.data (n->ud, buf, (size_t) r);
    }
}

void net_poll (net *n)
{
  if (!n || n->state == S_DONE) return;
  switch (n->state)
    {
    case S_CONNECTING:
      {
	fd_set w;
	struct timeval tv;
	tv.tv_sec = 0; tv.tv_usec = 0;
	FD_ZERO (&w); FD_SET (n->fd, &w);
	if (select (n->fd + 1, NULL, &w, NULL, &tv) > 0)
	  {
	    int err = 0; socklen_t len = sizeof err;
	    if (getsockopt (n->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
	    if (err == 0)
	      {
		if (n->tls)
		  {
		    say (n, "Connected, starting TLS");
		    if (start_tls (n) < 0) { char why[300]; tls_error_text (n, why, sizeof why); finish (n, why); return; }
		    n->state = S_HANDSHAKE; n->started = time (NULL);
		  }
		else { n->state = S_OPEN; if (n->ev.connected) n->ev.connected (n->ud); }
	      }
	    else
	      {
		say (n, "Could not connect: %s", strerror (err));
		close (n->fd); n->fd = -1;
		if (try_next_address (n) < 0) finish (n, "Could not connect to the server");
	      }
	  }
	else if (time (NULL) - n->started > CONNECT_SECONDS)
	  {
	    say (n, "No answer from the server");
	    close (n->fd); n->fd = -1;
	    if (try_next_address (n) < 0) finish (n, "The server did not answer");
	  }
      }
      break;
    case S_HANDSHAKE:
      {
	int r = SSL_connect (n->ssl);
	if (r == 1)
	  {
	    n->state = S_OPEN;
	    say (n, "TLS is up (%s, %s)", SSL_get_version (n->ssl), SSL_get_cipher_name (n->ssl));
	    if (n->ev.connected) n->ev.connected (n->ud);
	  }
	else
	  {
	    int e = SSL_get_error (n->ssl, r);
	    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
	      {
		if (time (NULL) - n->started > CONNECT_SECONDS) finish (n, "The TLS handshake took too long");
	      }
	    else { char why[300]; tls_error_text (n, why, sizeof why); finish (n, why); }
	  }
      }
      break;
    case S_OPEN:
      flush_out (n);
      if (n->state == S_OPEN) read_some (n);
      if (n->state == S_OPEN) flush_out (n);
      break;
    default: break;
    }
}

int net_send (net *n, const char *data, size_t len)
{
  if (!n || n->state != S_OPEN) return -1;
  if (n->outlen + len > OUT_MAX) return -1;
  if (n->outlen + len > n->outcap)
    {
      size_t cap = n->outcap ? n->outcap : 1024;
      char *p;
      while (cap < n->outlen + len) cap *= 2;
      p = realloc (n->out, cap);
      if (!p) return -1;
      n->out = p; n->outcap = cap;
    }
  memcpy (n->out + n->outlen, data, len); n->outlen += len;
  flush_out (n);
  return 0;
}

int net_connected (const net *n) { return n && n->state == S_OPEN; }
size_t net_pending (const net *n) { return n ? n->outlen : 0; }

void net_free (net *n)
{
  if (!n) return;
  if (n->ssl) { SSL_shutdown (n->ssl); SSL_free (n->ssl); }
  if (n->ctx) SSL_CTX_free (n->ctx);
  if (n->fd >= 0) close (n->fd);
  if (n->res) freeaddrinfo (n->res);
  free (n->host); free (n->cafile); free (n->out);
  free (n);
}
