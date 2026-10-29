/* net.h - one connection to an IRC server: TCP, with TLS (OpenSSL) if asked.  Non-blocking: nothing waits except the lookup of the host name; net_poll is called from the idle loop of the
   application and moves the connection on (connect, TLS handshake, reading, writing).  The server's certificate and its name are checked (a CA file is given) unless net_open is told not to. */
#ifndef NATTER_NET_H
#define NATTER_NET_H

#include <stddef.h>

typedef struct net net;

typedef struct
{
  void (*status) (void *ud, const char *text);			/* progress: "Connecting to ...", for the user to read */
  void (*connected) (void *ud);					/* the connection is ready for the protocol (after the TLS handshake, if any) */
  void (*data) (void *ud, const char *buf, size_t n);		/* bytes from the server */
  void (*closed) (void *ud, const char *why);			/* the connection has ended or could not be made: why says so; the net is finished (call net_free) */
} net_events;

/* Looks up the host (this waits for the resolver) and starts the connection.  cafile: the file of CA certificates for TLS (NULL: the default places).  verify: 0 turns the check of the
   certificate off (for testing only).  Returns NULL, and calls closed, if the lookup fails. */
net *net_open (const char *host, int port, int tls, const char *cafile, int verify, const net_events *ev, void *ud);
void net_poll (net *n);						/* call about ten times a second, or whenever there is time */
int net_send (net *n, const char *data, size_t len);		/* queues bytes for the server; returns 0, or -1 if the queue is full or the connection is not up */
int net_connected (const net *n);
size_t net_pending (const net *n);				/* bytes queued and not yet sent */
void net_free (net *n);						/* closes the connection if it is open, and frees everything */

#endif
