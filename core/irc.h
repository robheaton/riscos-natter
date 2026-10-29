/* irc.h - the protocol core of Natter: an IRC client connection with no operating system calls in it.

   You give it the bytes that arrive from the server (irc_conn_input), you take the bytes that may be sent (irc_conn_output) and you get the things that happen as
   events through a callback.  Time is passed in by the caller, in centiseconds (OS_ReadMonotonicTime on RISC OS), so everything can be tested on any machine with scripted
   conversations.  Plain C99.  A failed malloc stops the program (abort): there is no sensible way to go on with half a connection.

   Handles the IRCv3 capability negotiation (CAP LS 302, REQ, ACK, NAK, NEW, DEL), SASL PLAIN, the tags of IRCv3 messages (server-time, msgid, account), nick collisions during
   registration, PING, the 005 server settings, CTCP (ACTION, and automatic replies to VERSION and PING), idle detection, and a send queue with a flood limit.
   What it does not do yet: channel and user lists (the next step), DCC, SASL EXTERNAL and SCRAM, batches. */
#ifndef NATTER_IRC_H
#define NATTER_IRC_H

#include <stddef.h>

#define IRC_MAX_LINE 8704		/* 8191 bytes of tags + 512 bytes of message, with some room */
#define IRC_MAX_PARAMS 16
#define IRC_MAX_TAGS 32

/* ---- messages ---- */

typedef struct { const char *key, *value; } irc_tag;	/* value is "" for a tag without one, and already unescaped */

typedef struct
{
  irc_tag tags[IRC_MAX_TAGS];
  int ntags;
  const char *prefix;				/* the whole prefix, or NULL */
  const char *nick, *user, *host;		/* the parts of the prefix (nick is the server name for a server); NULL if absent */
  const char *command;				/* upper case: PRIVMSG, or three digits */
  const char *params[IRC_MAX_PARAMS];
  int nparams;					/* the trailing parameter is the last one; a 17th and later parameter is folded into the 16th */
} irc_msg;

/* Splits a line (without its CR LF) into its parts.  The line is changed in place and irc_msg points into it.  Returns 0, or -1 for a line that is not a message. */
int irc_parse (char *line, irc_msg *m);
const char *irc_tag_get (const irc_msg *m, const char *key);	/* NULL if the tag is not there */

/* Collects the bytes of a connection into lines.  A line that is longer than IRC_MAX_LINE is dropped as a whole. */
typedef struct { char buf[IRC_MAX_LINE]; size_t len; int skipping; } irc_linebuf;
typedef void (*irc_line_fn) (void *ud, char *line);
void irc_linebuf_init (irc_linebuf *lb);
void irc_linebuf_feed (irc_linebuf *lb, const char *data, size_t n, irc_line_fn fn, void *ud);	/* calls fn for each complete non-empty line */

/* ---- names ---- */

enum { IRC_CASE_ASCII, IRC_CASE_RFC1459, IRC_CASE_STRICT };	/* the CASEMAPPING of the server */
int irc_fold (int c, int casemap);
int irc_casecmp (const char *a, const char *b, int casemap);	/* like strcmp on the folded names */
size_t irc_utf8_trim (const char *s, size_t len, size_t max);	/* the largest n <= max (and <= len) such that s[0..n) does not end inside a UTF-8 character */

/* ---- the connection ---- */

typedef struct irc_conn irc_conn;

typedef struct
{
  const char *nick, *user, *realname;
  const char *pass;				/* the server password (PASS), or NULL */
  const char *sasl_user, *sasl_pass;		/* SASL PLAIN when both are set */
  int sasl_required;				/* quit if the login fails (default: carry on without) */
  const char *ctcp_version;			/* the answer to CTCP VERSION; NULL = do not answer */
  const char *caps;				/* the capabilities to ask for, separated by spaces; NULL = the default list; "" = no CAP negotiation at all */
  int burst;					/* flood limit: this many lines at once (0 = 5) ... */
  unsigned interval_cs;				/* ... then one more line for each interval (0 = 100 centiseconds) */
  unsigned ping_idle_cs;			/* send a PING after this much silence (0 = 9000) ... */
  unsigned ping_timeout_cs;			/* ... and report IRC_EV_TIMEOUT if nothing arrives within this time after it (0 = 6000) */
} irc_config;

enum irc_event_type
{
  IRC_EV_CONNECTED = 1,	/* registered (001): from = server, text = the welcome text, target = our nick */
  IRC_EV_PRIVMSG,	/* text to target (a channel or our nick) from from; self is set when it is our own (echo-message) */
  IRC_EV_NOTICE,	/* the same, as a NOTICE */
  IRC_EV_ACTION,	/* /me: text is the action */
  IRC_EV_CTCP,		/* any other CTCP: arg = the command, text = the rest, reply = 1 if it came in a NOTICE */
  IRC_EV_JOIN,		/* from joined target; account is the account of extended-join (NULL if none), text the real name */
  IRC_EV_PART,		/* from left target, text = the reason */
  IRC_EV_QUIT,		/* from left the network, text = the reason */
  IRC_EV_NICK,		/* from is now called arg (our own nick is updated before the event) */
  IRC_EV_KICK,		/* from kicked arg out of target, text = the reason; self is set when arg is us */
  IRC_EV_TOPIC,		/* from set the topic of target to text */
  IRC_EV_MODE,		/* from changed modes of target: text = the modes and their arguments */
  IRC_EV_INVITE,	/* from invites arg to target */
  IRC_EV_NUMERIC,	/* any other numeric reply: numeric = the code, target = our nick, text = the other parameters joined with spaces; msg has them one by one */
  IRC_EV_ERROR,		/* the server's ERROR (it is closing the connection): text */
  IRC_EV_CAP,		/* capability negotiation: arg = LS, ACK, NAK, NEW or DEL, text = the capability names */
  IRC_EV_SASL_OK,
  IRC_EV_SASL_FAIL,	/* text = the server's words; the numeric field has the code */
  IRC_EV_TIMEOUT	/* nothing has been heard for ping_idle_cs + ping_timeout_cs: close the connection and reconnect */
};

typedef struct
{
  int type;
  const char *time;			/* the server-time tag ("2026-10-29T21:30:00.000Z"), or NULL */
  const char *msgid;			/* the msgid tag, or NULL */
  const char *from;			/* the nick of the sender, or the name of the server */
  const char *user, *host;		/* NULL for a server */
  const char *target;
  const char *text;
  const char *arg;
  const char *account;			/* the account tag or the account of extended-join, or NULL */
  int numeric;
  int self;				/* the event is about our own nick */
  int reply;
  const irc_msg *msg;			/* the message as it came; the pointers in all of this are valid only during the callback */
} irc_event;

typedef void (*irc_event_fn) (void *ud, const irc_event *ev);

irc_conn *irc_conn_new (const irc_config *cfg, irc_event_fn fn, void *ud);
void irc_conn_free (irc_conn *c);

/* The socket is connected: queues the registration (CAP LS 302, PASS, NICK, USER).  Call again after a reconnect: everything is reset except the configuration. */
void irc_conn_start (irc_conn *c, unsigned now_cs);
/* Bytes from the server (any amount, any split). */
void irc_conn_input (irc_conn *c, const char *data, size_t n, unsigned now_cs);
/* Takes the lines that may be sent now (whole lines with CR LF) into buf, at most size bytes: returns how many bytes.  The flood limit applies to chat lines, not to registration, PONG and QUIT. */
size_t irc_conn_output (irc_conn *c, unsigned now_cs, char *buf, size_t size);
int irc_conn_wants_output (irc_conn *c, unsigned now_cs);	/* irc_conn_output would return something */
/* Call every second or so: idle detection (PING, IRC_EV_TIMEOUT). */
void irc_conn_tick (irc_conn *c, unsigned now_cs);

/* Commands.  Text that you pass is cleaned: a CR or LF ends a line (a command cannot be smuggled in), a long text is cut into several messages at spaces and never inside a UTF-8 character. */
void irc_send_raw (irc_conn *c, const char *line);			/* one line without CR LF; cut at the first CR or LF, 510 bytes at most */
void irc_join (irc_conn *c, const char *channel, const char *key);	/* key may be NULL */
void irc_part (irc_conn *c, const char *channel, const char *reason);
void irc_say (irc_conn *c, const char *target, const char *text);	/* PRIVMSG; text with newlines becomes one message per line */
void irc_notice (irc_conn *c, const char *target, const char *text);
void irc_action (irc_conn *c, const char *target, const char *text);	/* /me */
void irc_set_nick (irc_conn *c, const char *nick);
void irc_set_topic (irc_conn *c, const char *channel, const char *topic);	/* topic NULL asks for the topic */
void irc_quit (irc_conn *c, const char *reason);

/* What is known about the connection. */
const char *irc_conn_nick (irc_conn *c);
const char *irc_conn_server (irc_conn *c);			/* the name of the server that sent the welcome, or NULL */
const char *irc_conn_network (irc_conn *c);			/* NETWORK of the 005 line, or NULL */
int irc_conn_registered (irc_conn *c);
int irc_conn_casemap (irc_conn *c);
const char *irc_conn_isupport (irc_conn *c, const char *key);	/* the value of a 005 token ("" if it has none), or NULL if the server did not send it */
int irc_conn_cap_enabled (irc_conn *c, const char *name);
int irc_is_channel_name (irc_conn *c, const char *name);	/* by CHANTYPES; a leading STATUSMSG character (@#chan) is allowed */
int irc_conn_casecmp (irc_conn *c, const char *a, const char *b);	/* with the server's CASEMAPPING */

#endif
