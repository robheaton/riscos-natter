/* irc_conn.c - the connection: registration, capabilities, SASL, events, the send queue.  See irc.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irc_priv.h"

#define SASL_CHUNK 400
#define MAX_AUTO_QUEUE 20		/* automatic CTCP replies are only made while the queue is short */
#define DEFAULT_CAPS "server-time message-tags multi-prefix userhost-in-names away-notify account-notify extended-join chghost cap-notify invite-notify setname echo-message sasl"

/* ---- small helpers ---- */

void *xrealloc (void *p, size_t n) { p = realloc (p, n ? n : 1); if (!p) abort (); return p; }
char *xstrdup (const char *s) { size_t n; char *r; if (!s) return NULL; n = strlen (s) + 1; r = xrealloc (NULL, n); memcpy (r, s, n); return r; }
char *xstrndup (const char *s, size_t n) { char *r = xrealloc (NULL, n + 1); memcpy (r, s, n); r[n] = 0; return r; }

void strvec_add (strvec *v, const char *s)
{
  if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 8; v->v = xrealloc (v->v, v->cap * sizeof *v->v); }
  v->v[v->n++] = xstrdup (s);
}
void strvec_clear (strvec *v) { size_t i; for (i = 0; i < v->n; i++) free (v->v[i]); v->n = 0; }
int strvec_has (const strvec *v, const char *s) { size_t i; for (i = 0; i < v->n; i++) if (!strcmp (v->v[i], s)) return 1; return 0; }
void strvec_remove (strvec *v, const char *s)
{
  size_t i;
  for (i = 0; i < v->n; i++)
    if (!strcmp (v->v[i], s)) { free (v->v[i]); memmove (&v->v[i], &v->v[i + 1], (v->n - i - 1) * sizeof *v->v); v->n--; return; }
}

void kv_set (kv **a, size_t *n, size_t *cap, const char *name, const char *value)
{
  size_t i;
  for (i = 0; i < *n; i++)
    if (!strcmp ((*a)[i].name, name)) { free ((*a)[i].value); (*a)[i].value = xstrdup (value); return; }
  if (*n >= MAX_KV) return;
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 16; *a = xrealloc (*a, *cap * sizeof **a); }
  (*a)[*n].name = xstrdup (name); (*a)[*n].value = xstrdup (value); (*n)++;
}
void kv_remove (kv *a, size_t *n, const char *name)
{
  size_t i;
  for (i = 0; i < *n; i++)
    if (!strcmp (a[i].name, name)) { free (a[i].name); free (a[i].value); memmove (&a[i], &a[i + 1], (*n - i - 1) * sizeof *a); (*n)--; return; }
}
const char *kv_get (const kv *a, size_t n, const char *name) { size_t i; for (i = 0; i < n; i++) if (!strcmp (a[i].name, name)) return a[i].value; return NULL; }
void kv_clear (kv *a, size_t *n) { size_t i; for (i = 0; i < *n; i++) { free (a[i].name); free (a[i].value); } *n = 0; }

static void lineq_push (lineq *q, char *line)		/* takes the line */
{
  if (q->n - q->head >= MAX_QUEUE) { free (line); return; }
  if (q->head == q->n && q->head) { q->n = q->head = 0; }
  if (q->n == q->cap) { q->cap = q->cap ? q->cap * 2 : 16; q->lines = xrealloc (q->lines, q->cap * sizeof *q->lines); }
  q->lines[q->n++] = line;
}
static char *lineq_peek (lineq *q) { return q->head < q->n ? q->lines[q->head] : NULL; }
static void lineq_pop (lineq *q) { free (q->lines[q->head]); q->lines[q->head++] = NULL; if (q->head == q->n) q->head = q->n = 0; }
static void lineq_clear (lineq *q) { while (lineq_peek (q)) lineq_pop (q); }

void set_str (char **dst, const char *s) { free (*dst); *dst = xstrdup (s); }

/* ---- the queue of lines to send ---- */

/* Builds "cmd [p1 [p2]] [:trailing]" with the rules of the protocol: the words in the middle have no space (they end at the first one), nothing has a CR, LF or NUL, and the line
   is cut at 510 bytes inside a character.  prio lines are not held back by the flood limit. */
void qline (irc_conn *c, int prio, const char *cmd, const char *p1, const char *p2, const char *trailing)
{
  char buf[MAX_PAYLOAD + 8];
  size_t n = 0, room = MAX_PAYLOAD;
  const char *parts[2];
  int i;
  parts[0] = p1; parts[1] = p2;
  #define PUTC(ch) do { if (n < room) buf[n++] = (ch); } while (0)		/* (the argument must have no side effect: it is not evaluated when the line is full) */
  while (*cmd && *cmd != '\r' && *cmd != '\n') { PUTC (*cmd); cmd++; }
  for (i = 0; i < 2; i++)
    {
      const char *s = parts[i];
      if (!s) continue;
      PUTC (' ');
      if (*s == ':') PUTC ('*');					/* a word in the middle cannot start with a colon */
      while (*s && *s != ' ' && *s != '\r' && *s != '\n') { PUTC (*s); s++; }
    }
  if (trailing)
    {
      size_t tl = 0;
      PUTC (' '); PUTC (':');
      while (trailing[tl] && trailing[tl] != '\r' && trailing[tl] != '\n') tl++;
      if (n + tl > room) tl = irc_utf8_trim (trailing, tl, room - n);
      memcpy (buf + n, trailing, tl); n += tl;
    }
  #undef PUTC
  lineq_push (prio ? &c->prio : &c->normal, xstrndup (buf, n));
}

void irc_send_raw (irc_conn *c, const char *line)
{
  size_t n = 0;
  while (line[n] && line[n] != '\r' && line[n] != '\n') n++;
  if (!n) return;
  if (n > MAX_PAYLOAD) n = irc_utf8_trim (line, n, MAX_PAYLOAD);
  lineq_push (&c->normal, xstrndup (line, n));
}

static void refill (irc_conn *c, unsigned now)
{
  unsigned elapsed = now - c->last_refill;
  if (c->tokens >= c->burst) { c->tokens = c->burst; c->last_refill = now; return; }
  if (elapsed >= c->interval)
    {
      unsigned add = elapsed / c->interval;
      c->tokens += (int) (add > 1000 ? 1000 : add);
      c->last_refill += add * c->interval;
      if (c->tokens >= c->burst) { c->tokens = c->burst; c->last_refill = now; }
    }
}

int irc_conn_wants_output (irc_conn *c, unsigned now)
{
  if (lineq_peek (&c->prio)) return 1;
  if (!lineq_peek (&c->normal)) return 0;
  refill (c, now);
  return c->tokens > 0;
}

size_t irc_conn_output (irc_conn *c, unsigned now, char *buf, size_t size)
{
  size_t used = 0;
  char *l;
  while ((l = lineq_peek (&c->prio)) != NULL)
    {
      size_t n = strlen (l);
      if (used + n + 2 > size) return used;
      memcpy (buf + used, l, n); memcpy (buf + used + n, "\r\n", 2); used += n + 2;
      lineq_pop (&c->prio);
    }
  refill (c, now);
  while ((l = lineq_peek (&c->normal)) != NULL && c->tokens > 0)
    {
      size_t n = strlen (l);
      if (used + n + 2 > size) break;
      memcpy (buf + used, l, n); memcpy (buf + used + n, "\r\n", 2); used += n + 2;
      lineq_pop (&c->normal);
      if (c->tokens >= c->burst) c->last_refill = now;			/* the clock for the next token starts when the bucket stops being full */
      c->tokens--;
    }
  return used;
}

/* ---- commands ---- */

void irc_join (irc_conn *c, const char *channel, const char *key) { qline (c, 0, "JOIN", channel, key && *key ? key : NULL, NULL); }
void irc_part (irc_conn *c, const char *channel, const char *reason) { qline (c, 0, "PART", channel, NULL, reason && *reason ? reason : NULL); }
void irc_set_nick (irc_conn *c, const char *nick) { qline (c, 0, "NICK", nick, NULL, NULL); }
void irc_set_topic (irc_conn *c, const char *channel, const char *topic) { qline (c, 0, "TOPIC", channel, NULL, topic); }
void irc_quit (irc_conn *c, const char *reason) { c->quitting = 1; qline (c, 0, "QUIT", NULL, NULL, reason ? reason : ""); }

/* One chat line may be about 512 bytes in all and the server adds ":nick!user@host PRIVMSG target :" in front when it passes it on: keep 120 bytes for the prefix. */
static void say_lines (irc_conn *c, const char *cmd, const char *target, const char *text, const char *pre, const char *post)
{
  long b = 512 - 2 - 120 - (long) (strlen (cmd) + strlen (target) + strlen (pre) + strlen (post));
  size_t budget = b < 64 ? 64 : b > 400 ? 400 : (size_t) b;
  size_t prel = strlen (pre), postl = strlen (post);
  const char *p = text;
  while (*p)
    {
      size_t len = 0;
      while (p[len] && p[len] != '\n' && p[len] != '\r') len++;
      if (!len) { p++; continue; }					/* empty lines are skipped */
      while (len)
	{
	  size_t piece = len, skip = 0;
	  char *tmp;
	  if (len > budget)
	    {
	      size_t k;
	      piece = irc_utf8_trim (p, len, budget);
	      for (k = piece; k > piece / 2; k--)				/* cut at the last space of the second half of the piece, if there is one, and drop that space */
		if (p[k - 1] == ' ') { piece = k - 1; skip = 1; break; }
	      if (!piece) { piece = budget; skip = 0; }			/* cannot happen with a budget of 64 or more, but never loop */
	    }
	  tmp = xrealloc (NULL, prel + piece + postl + 1);
	  memcpy (tmp, pre, prel); memcpy (tmp + prel, p, piece); memcpy (tmp + prel + piece, post, postl + 1);
	  qline (c, 0, cmd, target, NULL, tmp);
	  free (tmp);
	  p += piece + skip; len -= piece + skip;
	}
      while (*p == '\r' || *p == '\n') p++;
    }
}

void irc_say (irc_conn *c, const char *target, const char *text) { say_lines (c, "PRIVMSG", target, text, "", ""); }
void irc_notice (irc_conn *c, const char *target, const char *text) { say_lines (c, "NOTICE", target, text, "", ""); }
void irc_action (irc_conn *c, const char *target, const char *text) { say_lines (c, "PRIVMSG", target, text, "\001ACTION ", "\001"); }

/* ---- what is known ---- */

const char *irc_conn_nick (irc_conn *c) { return c->nick; }
const char *irc_conn_server (irc_conn *c) { return c->server; }
const char *irc_conn_network (irc_conn *c) { return c->network; }
int irc_conn_registered (irc_conn *c) { return c->registered; }
int irc_conn_casemap (irc_conn *c) { return c->casemap; }
const char *irc_conn_isupport (irc_conn *c, const char *key) { return kv_get (c->isupport, c->nisupport, key); }
int irc_conn_cap_enabled (irc_conn *c, const char *name) { return strvec_has (&c->enabled, name); }
int irc_conn_casecmp (irc_conn *c, const char *a, const char *b) { return irc_casecmp (a, b, c->casemap); }

int irc_is_channel_name (irc_conn *c, const char *name)
{
  const char *types = c->chantypes ? c->chantypes : "#&";
  const char *status = c->statusmsg ? c->statusmsg : "";
  if (*name && strchr (types, *name)) return 1;			/* a channel type comes first: "+" can be both a channel type and a status prefix */
  while (*name && strchr (status, *name))			/* "@#chan": a message to the operators of #chan */
    {
      name++;
      if (*name && strchr (types, *name)) return 1;
    }
  return 0;
}

/* ---- events ---- */

void emit (irc_conn *c, irc_event *e) { if (c->fn) c->fn (c->ud, e); }

void ev_init (irc_conn *c, irc_event *e, int type, const irc_msg *m)
{
  memset (e, 0, sizeof *e);
  e->type = type;
  e->msg = m;
  e->from = m->nick; e->user = m->user; e->host = m->host;
  e->time = irc_tag_get (m, "time");
  e->msgid = irc_tag_get (m, "msgid");
  e->account = irc_tag_get (m, "account");
  if (e->time && !*e->time) e->time = NULL;
  if (e->msgid && !*e->msgid) e->msgid = NULL;
  if (e->account && (!*e->account || !strcmp (e->account, "*"))) e->account = NULL;
  e->self = m->nick && c->nick && !irc_casecmp (m->nick, c->nick, c->casemap);
}

const char *param (const irc_msg *m, int i) { return i < m->nparams ? m->params[i] : ""; }

/* the parameters from index first on, joined with spaces, in a buffer of the connection */
static const char *join_params (irc_conn *c, const irc_msg *m, int first)
{
  size_t need = 1, n = 0;
  int i;
  for (i = first; i < m->nparams; i++) need += strlen (m->params[i]) + 1;
  if (need > c->scratch_cap) { c->scratch_cap = need + 64; c->scratch = xrealloc (c->scratch, c->scratch_cap); }
  for (i = first; i < m->nparams; i++)
    {
      size_t l = strlen (m->params[i]);
      if (n) c->scratch[n++] = ' ';
      memcpy (c->scratch + n, m->params[i], l); n += l;
    }
  c->scratch[n] = 0;
  return c->scratch;
}

/* ---- capabilities and SASL ---- */

static const char *cap_value (irc_conn *c, const char *name) { return kv_get (c->avail, c->navail, name); }

static void send_cap_end (irc_conn *c)
{
  if (c->cap_done) return;
  c->cap_done = 1;
  qline (c, 1, "CAP", "END", NULL, NULL);
}

static void base64 (const unsigned char *in, size_t n, char *out)
{
  static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i, o = 0;
  for (i = 0; i + 2 < n; i += 3)
    {
      out[o++] = t[in[i] >> 2]; out[o++] = t[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
      out[o++] = t[((in[i + 1] & 15) << 2) | (in[i + 2] >> 6)]; out[o++] = t[in[i + 2] & 63];
    }
  if (n - i == 1) { out[o++] = t[in[i] >> 2]; out[o++] = t[(in[i] & 3) << 4]; out[o++] = '='; out[o++] = '='; }
  else if (n - i == 2) { out[o++] = t[in[i] >> 2]; out[o++] = t[((in[i] & 3) << 4) | (in[i + 1] >> 4)]; out[o++] = t[(in[i + 1] & 15) << 2]; out[o++] = '='; }
  out[o] = 0;
}

static void sasl_send_chunks (irc_conn *c)
{
  /* the payload goes in pieces of 400 characters; a last piece of exactly 400 is followed by "+" */
  size_t left = c->sasl_len;
  const char *p = c->sasl_payload;
  for (;;)
    {
      size_t k = left > SASL_CHUNK ? SASL_CHUNK : left;
      char *tmp = xstrndup (p, k);
      qline (c, 1, "AUTHENTICATE", tmp, NULL, NULL);
      free (tmp);
      p += k; left -= k;
      if (k < SASL_CHUNK) break;
      if (!left) { qline (c, 1, "AUTHENTICATE", "+", NULL, NULL); break; }
    }
}

static void sasl_start (irc_conn *c)
{
  size_t ul = strlen (c->cfg_sasl_user), pl = strlen (c->cfg_sasl_pass), n = ul + pl + 2;
  unsigned char *raw = xrealloc (NULL, n);
  raw[0] = 0; memcpy (raw + 1, c->cfg_sasl_user, ul); raw[1 + ul] = 0; memcpy (raw + 2 + ul, c->cfg_sasl_pass, pl);
  free (c->sasl_payload);
  c->sasl_payload = xrealloc (NULL, 4 * ((n + 2) / 3) + 1);
  base64 (raw, n, c->sasl_payload);
  c->sasl_len = strlen (c->sasl_payload);
  free (raw);
  c->sasl_active = 1;
  qline (c, 1, "AUTHENTICATE", "PLAIN", NULL, NULL);
}

static int sasl_wanted (irc_conn *c) { return c->cfg_sasl_user && c->cfg_sasl_pass && strvec_has (&c->enabled, "sasl"); }

/* all REQs are answered: log in with SASL, or finish */
static void cap_settled (irc_conn *c)
{
  if (c->cap_pending > 0 || c->cap_done || c->sasl_active) return;
  if (sasl_wanted (c))
    {
      const char *v = cap_value (c, "sasl");
      if (!v || !*v || strstr (v, "PLAIN")) { sasl_start (c); return; }
    }
  send_cap_end (c);
}

static void sasl_finished (irc_conn *c, int ok, const irc_msg *m, int code)
{
  irc_event e;
  ev_init (c, &e, ok ? IRC_EV_SASL_OK : IRC_EV_SASL_FAIL, m);
  e.numeric = code; e.text = param (m, m->nparams ? m->nparams - 1 : 0);
  c->sasl_active = 0;
  emit (c, &e);
  if (!ok && c->sasl_required && !c->registered) { c->quitting = 1; qline (c, 1, "QUIT", NULL, NULL, "SASL login failed"); return; }
  send_cap_end (c);
}

/* The words of a CAP list.  A leading "-" means that the capability is switched off; "~" and "=" are modifiers that mean nothing to us; "name=value" is split.  The list is changed. */
static void each_cap (char *list, void (*fn) (irc_conn *, const char *name, const char *value, int off), irc_conn *c)
{
  char *p = list;
  while (*p)
    {
      char *end, *eq;
      int off = 0;
      while (*p == ' ') p++;
      if (!*p) break;
      end = strchr (p, ' ');
      if (end) *end = 0;
      while (*p == '-' || *p == '~' || *p == '=') { if (*p == '-') off = 1; p++; }
      eq = strchr (p, '=');
      if (eq) *eq++ = 0;
      if (*p) fn (c, p, eq ? eq : "", off);
      if (!end) break;
      p = end + 1;
    }
}

static void cap_ls_item (irc_conn *c, const char *name, const char *value, int off)
{
  if (off) kv_remove (c->avail, &c->navail, name); else kv_set (&c->avail, &c->navail, &c->capavail, name, value);
}
static void cap_ack_item (irc_conn *c, const char *name, const char *value, int off)
{
  (void) value;
  if (off) strvec_remove (&c->enabled, name);
  else if (c->enabled.n < MAX_ENABLED && !strvec_has (&c->enabled, name)) strvec_add (&c->enabled, name);
}
static void cap_name_item (irc_conn *c, const char *name, const char *value, int off)	/* collects the names of a CAP NEW list */
{
  (void) value; (void) off;
  strvec_add (&c->new_names, name);
}

static void words (strvec *v, const char *s)
{
  while (*s)
    {
      size_t l;
      while (*s == ' ') s++;
      l = strcspn (s, " ");
      if (l) { char *w = xstrndup (s, l); strvec_add (v, w); free (w); }
      s += l;
    }
}

/* Asks for the wanted capabilities that the server has and that are not on yet; with only != NULL just those that are in it (CAP NEW). */
static void cap_request (irc_conn *c, const strvec *only)
{
  strvec want = { 0, 0, 0 };
  char req[480];
  size_t n = 0, i;
  words (&want, c->cfg_caps ? c->cfg_caps : DEFAULT_CAPS);
  req[0] = 0;
  for (i = 0; i < want.n; i++)
    {
      const char *name = want.v[i];
      size_t tl = strlen (name);
      if (cap_value (c, name) == NULL || strvec_has (&c->enabled, name)) continue;
      if (only && !strvec_has (only, name)) continue;
      if (!strcmp (name, "sasl") && !(c->cfg_sasl_user && c->cfg_sasl_pass)) continue;	/* no use without a login */
      if (n + tl + 2 >= sizeof req) break;
      if (n) req[n++] = ' ';
      memcpy (req + n, name, tl); n += tl; req[n] = 0;
    }
  strvec_clear (&want); free (want.v);
  if (n) { qline (c, 1, "CAP", "REQ", NULL, req); c->cap_pending++; }
}

static void handle_cap (irc_conn *c, irc_msg *m)
{
  const char *sub = param (m, 1);
  int more = (m->nparams >= 4 && !strcmp (m->params[2], "*"));	/* CAP * LS * :...  the list goes on in the next message */
  const char *list = m->nparams >= 3 ? m->params[m->nparams - 1] : "";
  char *copy = xstrdup (list);
  irc_event e;
  ev_init (c, &e, IRC_EV_CAP, m);
  e.arg = sub; e.text = list;
  if (!strcmp (sub, "LS"))
    {
      each_cap (copy, cap_ls_item, c);
      emit (c, &e);
      if (!more && !c->cap_done) { cap_request (c, NULL); cap_settled (c); }
    }
  else if (!strcmp (sub, "ACK"))
    {
      each_cap (copy, cap_ack_item, c);
      if (c->cap_pending > 0) c->cap_pending--;
      emit (c, &e);
      cap_settled (c);
    }
  else if (!strcmp (sub, "NAK"))
    {
      if (c->cap_pending > 0) c->cap_pending--;
      emit (c, &e);
      cap_settled (c);
    }
  else if (!strcmp (sub, "NEW"))
    {
      strvec_clear (&c->new_names);
      each_cap (copy, cap_name_item, c);
      free (copy); copy = xstrdup (list);
      each_cap (copy, cap_ls_item, c);
      emit (c, &e);
      cap_request (c, &c->new_names);
    }
  else if (!strcmp (sub, "DEL"))
    {
      strvec names = { 0, 0, 0 };
      size_t i;
      words (&names, list);
      for (i = 0; i < names.n; i++) { strvec_remove (&c->enabled, names.v[i]); kv_remove (c->avail, &c->navail, names.v[i]); }
      strvec_clear (&names); free (names.v);
      emit (c, &e);
    }
  free (copy);
}

/* ---- the 005 line ---- */

static void handle_isupport (irc_conn *c, const irc_msg *m)
{
  int i;
  for (i = 1; i < m->nparams - 1; i++)				/* the last parameter is the text "are supported by this server" */
    {
      char *tok = xstrdup (m->params[i]), *eq = strchr (tok, '=');
      if (eq) *eq++ = 0;
      if (tok[0] == '-') { kv_remove (c->isupport, &c->nisupport, tok + 1); }
      else
	{
	  kv_set (&c->isupport, &c->nisupport, &c->capisupport, tok, eq ? eq : "");
	  if (!strcmp (tok, "CASEMAPPING") && eq)
	    {
	      if (!c->nusers)						/* the order of the people we know depends on it: it is fixed once we know somebody */
		c->casemap = !strcmp (eq, "ascii") ? IRC_CASE_ASCII : !strcmp (eq, "strict-rfc1459") ? IRC_CASE_STRICT : IRC_CASE_RFC1459;
	    }
	  else if (!strcmp (tok, "CHANTYPES")) set_str (&c->chantypes, eq ? eq : "");
	  else if (!strcmp (tok, "STATUSMSG")) set_str (&c->statusmsg, eq ? eq : "");
	  else if (!strcmp (tok, "NETWORK")) set_str (&c->network, eq ? eq : "");
	  else if (!strcmp (tok, "PREFIX")) irc__chan_set_prefix (c, eq ? eq : "");
	  else if (!strcmp (tok, "CHANMODES")) irc__chan_set_chanmodes (c, eq ? eq : "");
	}
      free (tok);
    }
}

/* ---- the messages ---- */

static void ctcp_reply (irc_conn *c, const char *to, const char *cmd, const char *text)
{
  size_t n = strlen (cmd) + strlen (text) + 4;
  char *t = xrealloc (NULL, n);
  snprintf (t, n, "\001%s%s%s\001", cmd, *text ? " " : "", text);
  qline (c, 0, "NOTICE", to, NULL, t);
  free (t);
}

static void handle_privmsg (irc_conn *c, irc_msg *m, int notice)
{
  irc_event e;
  char *text = (char *) param (m, 1);
  ev_init (c, &e, notice ? IRC_EV_NOTICE : IRC_EV_PRIVMSG, m);
  e.target = param (m, 0);
  if (text[0] == '\001')
    {
      char *cmd = text + 1, *rest, *end = strchr (cmd, '\001');
      if (end) *end = 0;
      rest = strchr (cmd, ' ');
      if (rest) *rest++ = 0; else rest = cmd + strlen (cmd);
      if (!strcmp (cmd, "ACTION") && !notice)
	{
	  e.type = IRC_EV_ACTION; e.text = rest;
	  emit (c, &e);
	  return;
	}
      e.type = IRC_EV_CTCP; e.arg = cmd; e.text = rest; e.reply = notice;
      emit (c, &e);
      if (!notice && m->nick && !irc_casecmp (e.target, c->nick, c->casemap) && c->normal.n - c->normal.head < MAX_AUTO_QUEUE)	/* only a request to us gets an answer, never one to a channel, and not in a flood */
	{
	  if (!strcmp (cmd, "VERSION") && c->cfg_version) ctcp_reply (c, m->nick, "VERSION", c->cfg_version);
	  else if (!strcmp (cmd, "PING") && strlen (rest) <= 100) ctcp_reply (c, m->nick, "PING", rest);
	}
      return;
    }
  e.text = text;
  emit (c, &e);
}

static void handle_numeric (irc_conn *c, irc_msg *m, int num)
{
  irc_event e;
  if (num == 1)
    {
      if (m->nparams > 0) set_str (&c->nick, m->params[0]);
      if (m->prefix) set_str (&c->server, m->prefix);
      c->registered = 1;
      c->cap_done = 1;					/* registered: whatever was still being negotiated is over */
      ev_init (c, &e, IRC_EV_CONNECTED, m);
      e.target = c->nick; e.text = param (m, 1);
      emit (c, &e);
      return;
    }
  if (num == 5) { handle_isupport (c, m); return; }
  if (num == 421 && !strcmp (param (m, 1), "CAP")) { c->cap_done = 1; return; }	/* the server does not know CAP: registration goes on without it */
  if (num == 433 && !c->registered)			/* the nick is in use: try another one */
    {
      size_t l = strlen (c->nick);
      const char *nl = kv_get (c->isupport, c->nisupport, "NICKLEN");
      size_t max = nl && atoi (nl) > 0 ? (size_t) atoi (nl) : 30;
      char *n2 = xrealloc (NULL, l + 2);
      memcpy (n2, c->nick, l);
      if (l < max) { n2[l] = '_'; n2[l + 1] = 0; }
      else { n2[l - 1] = n2[l - 1] == '_' ? '0' : '_'; n2[l] = 0; }
      free (c->nick); c->nick = n2;
      qline (c, 1, "NICK", c->nick, NULL, NULL);
      return;
    }
  if (num == 903) { sasl_finished (c, 1, m, num); return; }
  if (num >= 902 && num <= 908 && num != 903 && num != 908) { sasl_finished (c, 0, m, num); return; }
  if (num == 908) return;				/* RPL_SASLMECHS: the list of mechanisms after a failure; the failure itself follows or has come */
  ev_init (c, &e, IRC_EV_NUMERIC, m);
  e.numeric = num; e.target = param (m, 0); e.text = join_params (c, m, 1);
  emit (c, &e);
}

static void handle (irc_conn *c, irc_msg *m)
{
  const char *cmd = m->command;
  irc_event e;
  if (irc__chan_handle (c, m)) return;				/* JOIN, PART, KICK, QUIT, NICK, TOPIC, MODE, NAMES, ... : irc_chan.c */
  if (cmd[0] >= '0' && cmd[0] <= '9' && cmd[1] && cmd[2] && !cmd[3]) { handle_numeric (c, m, atoi (cmd)); return; }
  if (!strcmp (cmd, "PING")) { qline (c, 1, "PONG", NULL, NULL, m->nparams ? m->params[m->nparams - 1] : ""); return; }
  if (!strcmp (cmd, "PRIVMSG")) { handle_privmsg (c, m, 0); return; }
  if (!strcmp (cmd, "NOTICE")) { handle_privmsg (c, m, 1); return; }
  if (!strcmp (cmd, "CAP")) { handle_cap (c, m); return; }
  if (!strcmp (cmd, "AUTHENTICATE"))
    {
      if (c->sasl_active && !strcmp (param (m, 0), "+")) sasl_send_chunks (c);
      return;
    }
  if (!strcmp (cmd, "INVITE")) { ev_init (c, &e, IRC_EV_INVITE, m); e.arg = param (m, 0); e.target = param (m, 1); emit (c, &e); }
  else if (!strcmp (cmd, "ERROR")) { ev_init (c, &e, IRC_EV_ERROR, m); e.text = param (m, 0); emit (c, &e); }
}

static void on_line (void *ud, char *line)
{
  irc_conn *c = ud;
  irc_msg m;
  c->last_input = c->now; c->ping_sent = 0; c->timed_out = 0;
  if (irc_parse (line, &m) == 0) handle (c, &m);
}

void irc_conn_input (irc_conn *c, const char *data, size_t n, unsigned now)
{
  c->now = now;
  irc_linebuf_feed (&c->lb, data, n, on_line, c);
}

/* ---- life ---- */

static void reset (irc_conn *c)
{
  irc__chan_reset (c);						/* first: the people are sorted by the case mapping that is still set */
  irc_linebuf_init (&c->lb);
  set_str (&c->nick, c->cfg_nick);
  free (c->server); c->server = NULL; free (c->network); c->network = NULL;
  free (c->chantypes); c->chantypes = NULL; free (c->statusmsg); c->statusmsg = NULL;
  c->registered = c->started = c->quitting = 0;
  c->casemap = IRC_CASE_RFC1459;
  kv_clear (c->isupport, &c->nisupport);
  lineq_clear (&c->prio); lineq_clear (&c->normal);
  c->tokens = c->burst; c->last_refill = 0;
  c->ping_sent = c->timed_out = 0;
  c->cap_done = c->cap_pending = c->ls_open = c->sasl_active = 0;
  kv_clear (c->avail, &c->navail); strvec_clear (&c->enabled); strvec_clear (&c->new_names);
  free (c->sasl_payload); c->sasl_payload = NULL; c->sasl_len = 0;
}

irc_conn *irc_conn_new (const irc_config *cfg, irc_event_fn fn, void *ud)
{
  irc_conn *c = xrealloc (NULL, sizeof *c);
  memset (c, 0, sizeof *c);
  c->cfg_nick = xstrdup (cfg->nick && *cfg->nick ? cfg->nick : "natter");
  c->cfg_user = xstrdup (cfg->user && *cfg->user ? cfg->user : c->cfg_nick);
  c->cfg_realname = xstrdup (cfg->realname && *cfg->realname ? cfg->realname : c->cfg_nick);
  c->cfg_pass = xstrdup (cfg->pass); c->cfg_sasl_user = xstrdup (cfg->sasl_user); c->cfg_sasl_pass = xstrdup (cfg->sasl_pass);
  c->cfg_version = xstrdup (cfg->ctcp_version); c->cfg_caps = xstrdup (cfg->caps);
  c->sasl_required = cfg->sasl_required;
  c->burst = cfg->burst > 0 ? cfg->burst : 5;
  c->interval = cfg->interval_cs ? cfg->interval_cs : 100;
  c->ping_idle = cfg->ping_idle_cs ? cfg->ping_idle_cs : 9000;
  c->ping_timeout = cfg->ping_timeout_cs ? cfg->ping_timeout_cs : 6000;
  c->fn = fn; c->ud = ud;
  reset (c);
  return c;
}

void irc_conn_free (irc_conn *c)
{
  if (!c) return;
  free (c->cfg_nick); free (c->cfg_user); free (c->cfg_realname); free (c->cfg_pass); free (c->cfg_sasl_user); free (c->cfg_sasl_pass); free (c->cfg_version); free (c->cfg_caps);
  free (c->nick); free (c->server); free (c->network); free (c->chantypes); free (c->statusmsg);
  kv_clear (c->isupport, &c->nisupport); free (c->isupport);
  kv_clear (c->avail, &c->navail); free (c->avail);
  strvec_clear (&c->enabled); free (c->enabled.v); strvec_clear (&c->new_names); free (c->new_names.v);
  lineq_clear (&c->prio); free (c->prio.lines); lineq_clear (&c->normal); free (c->normal.lines);
  irc__chan_free (c);
  free (c->sasl_payload); free (c->scratch);
  free (c);
}

static void queue_user_line (irc_conn *c)
{
  char line[MAX_PAYLOAD + 8], user[64];
  size_t i, n, rl = strlen (c->cfg_realname);
  for (i = 0; i < sizeof user - 1 && c->cfg_user[i] && c->cfg_user[i] != ' ' && c->cfg_user[i] != '\r' && c->cfg_user[i] != '\n'; i++) user[i] = c->cfg_user[i];
  user[i] = 0;
  if (!*user) strcpy (user, "natter");
  n = (size_t) snprintf (line, sizeof line, "USER %s 0 * :", user);
  if (n + rl > MAX_PAYLOAD) rl = irc_utf8_trim (c->cfg_realname, rl, MAX_PAYLOAD - n);
  for (i = 0; i < rl; i++) line[n + i] = (c->cfg_realname[i] == '\r' || c->cfg_realname[i] == '\n') ? ' ' : c->cfg_realname[i];
  line[n + rl] = 0;
  lineq_push (&c->prio, xstrdup (line));
}

void irc_conn_start (irc_conn *c, unsigned now)
{
  reset (c);
  c->now = now; c->started = 1; c->last_input = now; c->last_refill = now;
  if (c->cfg_caps && !*c->cfg_caps) c->cap_done = 1;		/* "": no negotiation */
  else qline (c, 1, "CAP", "LS", "302", NULL);
  if (c->cfg_pass) qline (c, 1, "PASS", NULL, NULL, c->cfg_pass);
  qline (c, 1, "NICK", c->nick, NULL, NULL);
  queue_user_line (c);
}

void irc_conn_tick (irc_conn *c, unsigned now)
{
  unsigned idle;
  c->now = now;
  if (!c->started || c->quitting) return;
  idle = now - c->last_input;
  if (!c->ping_sent && idle >= c->ping_idle)
    {
      qline (c, 1, "PING", NULL, NULL, "natter");
      c->ping_sent = 1; c->ping_sent_at = now;
    }
  else if (c->ping_sent && !c->timed_out && now - c->ping_sent_at >= c->ping_timeout)
    {
      irc_event e;
      memset (&e, 0, sizeof e);
      e.type = IRC_EV_TIMEOUT;
      c->timed_out = 1;
      emit (c, &e);
    }
}
