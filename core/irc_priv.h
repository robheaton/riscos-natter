/* irc_priv.h - what the files of the core share.  Not for users of the library: see irc.h. */
#ifndef NATTER_IRC_PRIV_H
#define NATTER_IRC_PRIV_H

#include <stdlib.h>
#include <string.h>
#include "irc.h"

#define MAX_PAYLOAD 510			/* a line without its CR LF */
#define MAX_KV 512			/* limits against a server (or a fuzzer) that sends endless tokens or capabilities */
#define MAX_ENABLED 256
#define MAX_QUEUE 1000			/* lines waiting in the flood-limited queue; more are dropped */

/* the helpers of irc_conn.c, with names that cannot clash with the program that uses the library */
#define xrealloc irc__xrealloc
#define xstrdup irc__xstrdup
#define xstrndup irc__xstrndup
#define set_str irc__set_str
#define strvec_add irc__strvec_add
#define strvec_clear irc__strvec_clear
#define strvec_has irc__strvec_has
#define strvec_remove irc__strvec_remove
#define kv_set irc__kv_set
#define kv_remove irc__kv_remove
#define kv_get irc__kv_get
#define kv_clear irc__kv_clear
#define qline irc__qline
#define emit irc__emit
#define ev_init irc__ev_init
#define param irc__param

typedef struct { char *name, *value; } kv;
typedef struct { char **v; size_t n, cap; } strvec;
typedef struct { char **lines; size_t n, cap, head; } lineq;

struct irc_conn
{
  /* configuration (copies) */
  char *cfg_nick, *cfg_user, *cfg_realname, *cfg_pass, *cfg_sasl_user, *cfg_sasl_pass, *cfg_version, *cfg_caps;
  int sasl_required, burst;
  unsigned interval, ping_idle, ping_timeout;
  irc_event_fn fn;
  void *ud;

  /* connection state */
  irc_linebuf lb;
  unsigned now;				/* the time of the call that is running */
  char *nick;				/* ours, as far as we know */
  char *server, *network;
  int registered, started, quitting;
  int casemap;
  kv *isupport; size_t nisupport, capisupport;
  char *chantypes, *statusmsg;
  lineq prio, normal;
  int tokens; unsigned last_refill;
  unsigned last_input, ping_sent_at; int ping_sent, timed_out;

  /* capabilities */
  int cap_done;				/* CAP END has been sent (or negotiation is not used) */
  int cap_pending;			/* REQ lines sent and not yet answered */
  kv *avail; size_t navail, capavail;	/* from LS */
  int ls_open;				/* a multi-line LS is in progress */
  strvec enabled, new_names;
  int sasl_active;
  char *sasl_payload; size_t sasl_len, sasl_sent;

  /* channels and people */
  irc_user **users; size_t nusers, capusers;			/* everybody we share a channel with, sorted by the folded nick */
  irc_channel **channels; size_t nchannels, capchannels;
  char *prefix_modes, *prefix_syms;				/* PREFIX=(ov)@+ from the 005 line */
  char *chanmodes[4];						/* CHANMODES=A,B,C,D */

  char *scratch; size_t scratch_cap;
};

struct irc_channel
{
  char *name, *topic, *topic_by;
  unsigned long topic_time;
  irc_member *members; size_t n, cap;	/* sorted by the folded nick, unless unsorted is set (while a NAMES list is arriving) */
  int synced, names_open, unsorted;
  kv *modes; size_t nmodes, capmodes;	/* the modes that are on: letter -> argument ("" for none) */
};

void *xrealloc (void *p, size_t n);
char *xstrdup (const char *s);
char *xstrndup (const char *s, size_t n);
void set_str (char **dst, const char *s);
void strvec_add (strvec *v, const char *s);
void strvec_clear (strvec *v);
int strvec_has (const strvec *v, const char *s);
void strvec_remove (strvec *v, const char *s);
void kv_set (kv **a, size_t *n, size_t *cap, const char *name, const char *value);
void kv_remove (kv *a, size_t *n, const char *name);
const char *kv_get (const kv *a, size_t n, const char *name);
void kv_clear (kv *a, size_t *n);
void qline (irc_conn *c, int prio, const char *cmd, const char *p1, const char *p2, const char *trailing);
void emit (irc_conn *c, irc_event *e);
void ev_init (irc_conn *c, irc_event *e, int type, const irc_msg *m);
const char *param (const irc_msg *m, int i);

/* irc_chan.c */
void irc__chan_reset (irc_conn *c);			/* forget all channels and people */
void irc__chan_free (irc_conn *c);
int irc__chan_handle (irc_conn *c, irc_msg *m);		/* 1 if the message was dealt with (state and events), 0 if the caller goes on */
void irc__chan_set_prefix (irc_conn *c, const char *value);	/* PREFIX=(ov)@+ */
void irc__chan_set_chanmodes (irc_conn *c, const char *value);	/* CHANMODES=A,B,C,D */

#endif
