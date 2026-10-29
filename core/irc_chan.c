/* irc_chan.c - channels and the people in them: JOIN, PART, KICK, QUIT, NICK, TOPIC, MODE, the NAMES list, and the notifications of account-notify, away-notify, chghost and setname.
   The state is updated before the event is emitted.  A person is one irc_user for all the channels that we share; a channel has a list of members (a person and the prefix bits)
   sorted by the folded nick, with a binary search.  While a NAMES list is arriving the members are only appended and sorted once at the end (a big channel has tens of thousands). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irc_priv.h"

static const char *pmodes (irc_conn *c) { return c->prefix_modes ? c->prefix_modes : "ov"; }
static const char *psyms (irc_conn *c) { return c->prefix_syms ? c->prefix_syms : "@+"; }

void irc__chan_set_prefix (irc_conn *c, const char *value)		/* "(qaohv)~&@%+" */
{
  const char *close;
  size_t nm, ns;
  if (*value != '(' || !(close = strchr (value, ')'))) { free (c->prefix_modes); free (c->prefix_syms); c->prefix_modes = c->prefix_syms = NULL; return; }
  nm = (size_t) (close - value - 1); ns = strlen (close + 1);
  if (ns < nm) nm = ns;
  if (nm > 31) nm = 31;							/* the prefix of a member is the bits of an unsigned */
  free (c->prefix_modes); free (c->prefix_syms);
  c->prefix_modes = xstrndup (value + 1, nm); c->prefix_syms = xstrndup (close + 1, nm);
}

void irc__chan_set_chanmodes (irc_conn *c, const char *value)		/* "beI,k,l,imnpst" */
{
  int i;
  for (i = 0; i < 4; i++) { free (c->chanmodes[i]); c->chanmodes[i] = NULL; }
  for (i = 0; i < 4 && *value; i++)
    {
      size_t l = strcspn (value, ",");
      c->chanmodes[i] = xstrndup (value, l);
      value += l;
      if (*value == ',') value++;
    }
}

/* 0, 1, 2, 3 for the types A (a list), B (an argument always), C (an argument when set) and D (none) */
static int mode_type (irc_conn *c, char letter)
{
  static const char *defaults[4] = { "beI", "k", "l", "imnpst" };
  int i;
  for (i = 0; i < 4; i++)
    {
      const char *set = c->chanmodes[i] ? c->chanmodes[i] : (c->chanmodes[0] ? "" : defaults[i]);
      if (strchr (set, letter)) return i;
    }
  return 3;
}

/* ---- people ---- */

static size_t user_pos (irc_conn *c, const char *nick, int *found)
{
  size_t lo = 0, hi = c->nusers;
  while (lo < hi)
    {
      size_t mid = (lo + hi) / 2;
      int d = irc_casecmp (c->users[mid]->nick, nick, c->casemap);
      if (d == 0) { *found = 1; return mid; }
      if (d < 0) lo = mid + 1; else hi = mid;
    }
  *found = 0;
  return lo;
}

static irc_user *user_find (irc_conn *c, const char *nick)
{
  int found;
  size_t i = user_pos (c, nick, &found);
  return found ? c->users[i] : NULL;
}

static void user_insert (irc_conn *c, irc_user *u)
{
  int found;
  size_t i = user_pos (c, u->nick, &found);
  if (c->nusers == c->capusers) { c->capusers = c->capusers ? c->capusers * 2 : 64; c->users = xrealloc (c->users, c->capusers * sizeof *c->users); }
  memmove (&c->users[i + 1], &c->users[i], (c->nusers - i) * sizeof *c->users);
  c->users[i] = u; c->nusers++;
}

static void user_unlink (irc_conn *c, irc_user *u)		/* out of the sorted array, by pointer */
{
  int found;
  size_t i = user_pos (c, u->nick, &found);
  if (found && c->users[i] == u) { memmove (&c->users[i], &c->users[i + 1], (c->nusers - i - 1) * sizeof *c->users); c->nusers--; }
}

static void user_free (irc_user *u) { free (u->nick); free (u->user); free (u->host); free (u->account); free (u->realname); free (u); }

static irc_user *user_get (irc_conn *c, const char *nick)	/* the person, new if not known */
{
  irc_user *u = user_find (c, nick);
  if (u) return u;
  u = xrealloc (NULL, sizeof *u);
  memset (u, 0, sizeof *u);
  u->nick = xstrdup (nick);
  user_insert (c, u);
  return u;
}

static void user_unref (irc_conn *c, irc_user *u)
{
  if (--u->refs <= 0) { user_unlink (c, u); user_free (u); }
}

static void user_info (irc_user *u, const char *user, const char *host)	/* what a message tells us; NULL leaves what we know */
{
  if (user && *user && (!u->user || strcmp (u->user, user))) set_str (&u->user, user);
  if (host && *host && (!u->host || strcmp (u->host, host))) set_str (&u->host, host);
}

/* ---- channels ---- */

static irc_channel *chan_find (irc_conn *c, const char *name)
{
  size_t i;
  for (i = 0; i < c->nchannels; i++) if (!irc_casecmp (c->channels[i]->name, name, c->casemap)) return c->channels[i];
  return NULL;
}

static irc_channel *chan_add (irc_conn *c, const char *name)
{
  irc_channel *ch = xrealloc (NULL, sizeof *ch);
  memset (ch, 0, sizeof *ch);
  ch->name = xstrdup (name); ch->topic = xstrdup ("");
  if (c->nchannels == c->capchannels) { c->capchannels = c->capchannels ? c->capchannels * 2 : 16; c->channels = xrealloc (c->channels, c->capchannels * sizeof *c->channels); }
  c->channels[c->nchannels++] = ch;
  return ch;
}

static void chan_clear_members (irc_conn *c, irc_channel *ch)
{
  size_t i;
  for (i = 0; i < ch->n; i++) user_unref (c, ch->members[i].user);
  ch->n = 0; ch->unsorted = 0;
}

static void chan_remove (irc_conn *c, irc_channel *ch)
{
  size_t i;
  chan_clear_members (c, ch);
  for (i = 0; i < c->nchannels; i++)
    if (c->channels[i] == ch) { memmove (&c->channels[i], &c->channels[i + 1], (c->nchannels - i - 1) * sizeof *c->channels); c->nchannels--; break; }
  kv_clear (ch->modes, &ch->nmodes); free (ch->modes);
  free (ch->members); free (ch->name); free (ch->topic); free (ch->topic_by);
  free (ch);
}

static int sort_casemap;
static int member_cmp (const void *a, const void *b)
{
  return irc_casecmp (((const irc_member *) a)->user->nick, ((const irc_member *) b)->user->nick, sort_casemap);
}

static void chan_sort (irc_conn *c, irc_channel *ch)	/* after a bulk append; also drops a person who is listed twice */
{
  size_t i, w;
  if (!ch->unsorted) return;
  sort_casemap = c->casemap;
  qsort (ch->members, ch->n, sizeof *ch->members, member_cmp);
  for (i = w = 0; i < ch->n; i++)
    {
      if (w && !irc_casecmp (ch->members[w - 1].user->nick, ch->members[i].user->nick, c->casemap)) { ch->members[w - 1].prefix |= ch->members[i].prefix; user_unref (c, ch->members[i].user); }
      else ch->members[w++] = ch->members[i];
    }
  ch->n = w; ch->unsorted = 0;
}

static size_t member_pos (irc_conn *c, irc_channel *ch, const char *nick, int *found)
{
  size_t lo = 0, hi;
  chan_sort (c, ch);
  hi = ch->n;
  while (lo < hi)
    {
      size_t mid = (lo + hi) / 2;
      int d = irc_casecmp (ch->members[mid].user->nick, nick, c->casemap);
      if (d == 0) { *found = 1; return mid; }
      if (d < 0) lo = mid + 1; else hi = mid;
    }
  *found = 0;
  return lo;
}

static void chan_grow (irc_channel *ch)
{
  if (ch->n == ch->cap) { ch->cap = ch->cap ? ch->cap * 2 : 32; ch->members = xrealloc (ch->members, ch->cap * sizeof *ch->members); }
}

static void member_attach (irc_conn *c, irc_channel *ch, irc_user *u, unsigned prefix)	/* in the right place; no change of the reference count */
{
  int found;
  size_t i = member_pos (c, ch, u->nick, &found);
  if (found) { ch->members[i].prefix |= prefix; return; }
  chan_grow (ch);
  memmove (&ch->members[i + 1], &ch->members[i], (ch->n - i) * sizeof *ch->members);
  ch->members[i].user = u; ch->members[i].prefix = prefix; ch->n++;
}

static void member_add (irc_conn *c, irc_channel *ch, irc_user *u, unsigned prefix, int bulk)
{
  if (bulk) { chan_grow (ch); ch->members[ch->n].user = u; ch->members[ch->n].prefix = prefix; ch->n++; ch->unsorted = 1; u->refs++; return; }
  {
    int found;
    size_t i = member_pos (c, ch, u->nick, &found);
    if (found) { ch->members[i].prefix |= prefix; return; }
  }
  u->refs++;
  member_attach (c, ch, u, prefix);
}

static int member_remove (irc_conn *c, irc_channel *ch, const char *nick)	/* 1 if the person was in the channel */
{
  int found;
  size_t i = member_pos (c, ch, nick, &found);
  irc_user *u;
  if (!found) return 0;
  u = ch->members[i].user;
  memmove (&ch->members[i], &ch->members[i + 1], (ch->n - i - 1) * sizeof *ch->members); ch->n--;
  user_unref (c, u);
  return 1;
}

static int is_self (irc_conn *c, const char *nick) { return nick && c->nick && !irc_casecmp (nick, c->nick, c->casemap); }

/* the channels that a person is in, in the order of joining; returns how many (the array has room for all channels) */
static size_t channels_of (irc_conn *c, irc_user *u, irc_channel **out)
{
  size_t i, n = 0;
  if (!u) return 0;
  for (i = 0; i < c->nchannels; i++)
    {
      int found;
      member_pos (c, c->channels[i], u->nick, &found);
      if (found) out[n++] = c->channels[i];
    }
  return n;
}

/* ---- modes ---- */

static const char *join_from (irc_conn *c, const irc_msg *m, int first)
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

/* m->params[first] is a mode string such as "+ov-b" and the arguments follow it */
static void apply_modes (irc_conn *c, irc_channel *ch, const irc_msg *m, int first)
{
  const char *p = first < m->nparams ? m->params[first] : "";
  int pi = first + 1, adding = 1;
  for (; *p; p++)
    {
      char letter[2];
      const char *pm;
      int type;
      if (*p == '+') { adding = 1; continue; }
      if (*p == '-') { adding = 0; continue; }
      letter[0] = *p; letter[1] = 0;
      pm = strchr (pmodes (c), *p);
      if (pm)								/* o, v ...: a member gets or loses a prefix */
	{
	  const char *arg = pi < m->nparams ? m->params[pi++] : NULL;
	  if (arg)
	    {
	      int found;
	      size_t i = member_pos (c, ch, arg, &found);
	      if (found)
		{
		  unsigned bit = 1u << (pm - pmodes (c));
		  if (adding) ch->members[i].prefix |= bit; else ch->members[i].prefix &= ~bit;
		}
	    }
	  continue;
	}
      type = mode_type (c, *p);
      if (type == 0) { if (pi < m->nparams) pi++; continue; }			/* a list (bans ...): not kept */
      {
	const char *arg = "";
	if (type == 1 || (type == 2 && adding)) { if (pi < m->nparams) arg = m->params[pi++]; }
	if (adding) kv_set (&ch->modes, &ch->nmodes, &ch->capmodes, letter, arg); else kv_remove (ch->modes, &ch->nmodes, letter);
      }
    }
}

size_t irc_channel_modestring (const irc_channel *ch, char *buf, size_t size)
{
  size_t idx[512], n = 0, i, j, len = 0;
  if (!size) return 0;
  for (i = 0; i < ch->nmodes && n < 512; i++)
    {
      for (j = n; j > 0 && ch->modes[idx[j - 1]].name[0] > ch->modes[i].name[0]; j--) idx[j] = idx[j - 1];
      idx[j] = i; n++;
    }
  #define PUT(chr) do { if (len + 1 < size) buf[len++] = (chr); } while (0)
  PUT ('+');
  for (i = 0; i < n; i++) PUT (ch->modes[idx[i]].name[0]);
  for (i = 0; i < n; i++)
    {
      const char *a = ch->modes[idx[i]].value;
      if (!*a) continue;
      PUT (' ');
      while (*a) { PUT (*a); a++; }
    }
  #undef PUT
  buf[len] = 0;
  return len;
}

/* ---- the messages ---- */

static void emit_each (irc_conn *c, irc_event *e, irc_channel **list, size_t n)	/* one event for each channel, or one without a target */
{
  size_t i;
  if (!n) { e->target = NULL; emit (c, e); return; }
  for (i = 0; i < n; i++) { e->target = list[i]->name; emit (c, e); }
}

static void do_join (irc_conn *c, irc_msg *m)
{
  irc_event e;
  const char *chname = param (m, 0);
  irc_channel *ch = chan_find (c, chname);
  const char *account = NULL, *realname = NULL;
  int self = is_self (c, m->nick);
  ev_init (c, &e, IRC_EV_JOIN, m);
  e.target = chname;
  if (m->nparams >= 3) { account = strcmp (m->params[1], "*") ? m->params[1] : NULL; realname = m->params[2]; e.account = account; e.text = realname; }	/* extended-join */
  if (m->nick && *chname)
    {
      if (self)
	{
	  irc_user *u;
	  if (ch) { chan_clear_members (c, ch); set_str (&ch->topic, ""); set_str (&ch->topic_by, NULL); ch->topic_time = 0; kv_clear (ch->modes, &ch->nmodes); }
	  else ch = chan_add (c, chname);
	  ch->synced = 0; ch->names_open = 0;
	  u = user_get (c, m->nick);
	  user_info (u, m->user, m->host);
	  member_add (c, ch, u, 0, 0);
	  qline (c, 0, "MODE", ch->name, NULL, NULL);			/* the modes of the channel (324) */
	}
      else if (ch)
	{
	  irc_user *u = user_get (c, m->nick);
	  user_info (u, m->user, m->host);
	  if (m->nparams >= 3) { set_str (&u->account, account); set_str (&u->realname, realname); }
	  member_add (c, ch, u, 0, 0);
	}
    }
  emit (c, &e);
}

static void do_part (irc_conn *c, irc_msg *m, int kick)
{
  irc_event e;
  const char *chname = param (m, 0);
  const char *who = kick ? param (m, 1) : m->nick;
  irc_channel *ch = chan_find (c, chname);
  ev_init (c, &e, kick ? IRC_EV_KICK : IRC_EV_PART, m);
  e.target = chname;
  if (kick) { e.arg = who; e.text = param (m, 2); e.self = is_self (c, who); }
  else e.text = param (m, 1);
  if (e.self || (!kick && is_self (c, m->nick)))
    {
      e.self = 1;
      emit (c, &e);							/* the channel can still be read in the callback */
      if (ch) chan_remove (c, ch);
      return;
    }
  if (ch && who) member_remove (c, ch, who);
  emit (c, &e);
}

static void do_quit (irc_conn *c, irc_msg *m)
{
  irc_event e;
  irc_channel **list = xrealloc (NULL, (c->nchannels + 1) * sizeof *list);
  irc_user *u = m->nick ? user_find (c, m->nick) : NULL;
  size_t n = channels_of (c, u, list), i;
  ev_init (c, &e, IRC_EV_QUIT, m);
  e.text = param (m, 0);
  for (i = 0; i < n; i++) member_remove (c, list[i], m->nick);
  emit_each (c, &e, list, n);
  free (list);
}

static void do_nick (irc_conn *c, irc_msg *m)
{
  irc_event e;
  const char *newnick = param (m, 0);
  irc_channel **list = xrealloc (NULL, (c->nchannels + 1) * sizeof *list);
  irc_user *u = m->nick ? user_find (c, m->nick) : NULL, *other;
  size_t n = channels_of (c, u, list), i;
  unsigned *prefixes = xrealloc (NULL, (n + 1) * sizeof *prefixes);
  int mine = is_self (c, m->nick);
  ev_init (c, &e, IRC_EV_NICK, m);
  e.arg = newnick; e.self = mine;
  if (u && *newnick)
    {
      for (i = 0; i < n; i++)						/* take the person out of the member lists, rename, put back at the new place */
	{
	  int found;
	  size_t k = member_pos (c, list[i], u->nick, &found);
	  prefixes[i] = list[i]->members[k].prefix;
	  memmove (&list[i]->members[k], &list[i]->members[k + 1], (list[i]->n - k - 1) * sizeof *list[i]->members); list[i]->n--;
	}
      user_unlink (c, u);
      if ((other = user_find (c, newnick)) != NULL && other != u)	/* somebody that we missed leaving: forget them */
	{
	  size_t j, ocount;
	  irc_channel **ol = xrealloc (NULL, (c->nchannels + 1) * sizeof *ol);
	  ocount = channels_of (c, other, ol);
	  for (j = 0; j < ocount; j++) member_remove (c, ol[j], other->nick);
	  free (ol);
	}
      set_str (&u->nick, newnick);
      user_insert (c, u);
      for (i = 0; i < n; i++) member_attach (c, list[i], u, prefixes[i]);
    }
  if (mine) set_str (&c->nick, newnick);
  emit_each (c, &e, list, n);
  free (prefixes); free (list);
}

static void do_topic (irc_conn *c, irc_msg *m)
{
  irc_event e;
  irc_channel *ch = chan_find (c, param (m, 0));
  ev_init (c, &e, IRC_EV_TOPIC, m);
  e.target = param (m, 0); e.text = param (m, 1);
  if (ch)
    {
      char who[512];
      snprintf (who, sizeof who, "%s%s%s%s%s", m->nick ? m->nick : "", m->user ? "!" : "", m->user ? m->user : "", m->host ? "@" : "", m->host ? m->host : "");
      set_str (&ch->topic, e.text); set_str (&ch->topic_by, who); ch->topic_time = 0;
    }
  emit (c, &e);
}

static void do_mode (irc_conn *c, irc_msg *m)
{
  irc_event e;
  const char *target = param (m, 0);
  irc_channel *ch = irc_is_channel_name (c, target) ? chan_find (c, target) : NULL;
  ev_init (c, &e, IRC_EV_MODE, m);
  e.target = target;
  if (ch) apply_modes (c, ch, m, 1);
  e.text = join_from (c, m, 1);
  emit (c, &e);
}

static void do_names (irc_conn *c, irc_msg *m)				/* 353: [me, symbol, channel, "@+nick nick2 ..."] */
{
  irc_channel *ch = chan_find (c, param (m, 2));
  char *p = (char *) param (m, 3);
  if (!ch) return;
  if (!ch->names_open) { chan_clear_members (c, ch); ch->names_open = 1; }
  while (*p)
    {
      char *end, *bang, *at;
      unsigned prefix = 0;
      irc_user *u;
      while (*p == ' ') p++;
      if (!*p) break;
      end = strchr (p, ' ');
      if (end) *end = 0;
      for (; *p; p++)
	{
	  const char *s = strchr (psyms (c), *p);
	  if (!s) break;
	  prefix |= 1u << (s - psyms (c));
	}
      bang = strchr (p, '!');
      if (bang) { *bang = 0; at = strchr (bang + 1, '@'); if (at) *at = 0; }
      else at = NULL;
      if (*p)
	{
	  u = user_get (c, p);
	  if (bang) user_info (u, bang + 1, at ? at + 1 : NULL);
	  member_add (c, ch, u, prefix, 1);
	}
      if (!end) break;
      p = end + 1;
    }
}

int irc__chan_handle (irc_conn *c, irc_msg *m)
{
  const char *cmd = m->command;
  irc_event e;
  irc_user *u;
  if (!strcmp (cmd, "JOIN")) { do_join (c, m); return 1; }
  if (!strcmp (cmd, "PART")) { do_part (c, m, 0); return 1; }
  if (!strcmp (cmd, "KICK")) { do_part (c, m, 1); return 1; }
  if (!strcmp (cmd, "QUIT")) { do_quit (c, m); return 1; }
  if (!strcmp (cmd, "NICK")) { do_nick (c, m); return 1; }
  if (!strcmp (cmd, "TOPIC")) { do_topic (c, m); return 1; }
  if (!strcmp (cmd, "MODE")) { do_mode (c, m); return 1; }
  if (!strcmp (cmd, "ACCOUNT"))
    {
      ev_init (c, &e, IRC_EV_ACCOUNT, m);
      e.arg = strcmp (param (m, 0), "*") && *param (m, 0) ? param (m, 0) : NULL;
      if ((u = m->nick ? user_find (c, m->nick) : NULL) != NULL) set_str (&u->account, e.arg);
      emit (c, &e);
      return 1;
    }
  if (!strcmp (cmd, "AWAY"))
    {
      ev_init (c, &e, IRC_EV_AWAY, m);
      e.text = m->nparams ? param (m, 0) : NULL;
      if ((u = m->nick ? user_find (c, m->nick) : NULL) != NULL) u->away = m->nparams > 0;
      emit (c, &e);
      return 1;
    }
  if (!strcmp (cmd, "CHGHOST"))
    {
      ev_init (c, &e, IRC_EV_CHGHOST, m);
      e.arg = param (m, 0); e.text = param (m, 1);
      if ((u = m->nick ? user_find (c, m->nick) : NULL) != NULL) { set_str (&u->user, e.arg); set_str (&u->host, e.text); }
      emit (c, &e);
      return 1;
    }
  if (!strcmp (cmd, "SETNAME"))
    {
      ev_init (c, &e, IRC_EV_SETNAME, m);
      e.text = param (m, 0);
      if ((u = m->nick ? user_find (c, m->nick) : NULL) != NULL) set_str (&u->realname, e.text);
      emit (c, &e);
      return 1;
    }
  if (cmd[0] >= '0' && cmd[0] <= '9' && cmd[1] && cmd[2] && !cmd[3])
    {
      int num = atoi (cmd);
      irc_channel *ch;
      switch (num)
	{
	case 353: ch = chan_find (c, param (m, 2)); if (!ch) return 0; do_names (c, m); return 1;
	case 366:
	  ch = chan_find (c, param (m, 1));
	  if (!ch) return 0;
	  ch->names_open = 0; ch->synced = 1;
	  if (c->nick && *c->nick) { irc_user *me = user_get (c, c->nick); member_add (c, ch, me, 0, 0); }	/* we are in it, listed or not */
	  chan_sort (c, ch);
	  ev_init (c, &e, IRC_EV_NAMES, m);
	  e.target = ch->name;
	  emit (c, &e);
	  return 1;
	case 332:
	  ch = chan_find (c, param (m, 1));
	  if (ch) set_str (&ch->topic, param (m, 2));
	  ev_init (c, &e, IRC_EV_TOPIC, m);
	  e.numeric = 332; e.target = param (m, 1); e.text = param (m, 2);
	  emit (c, &e);
	  return 1;
	case 331: ch = chan_find (c, param (m, 1)); if (ch) set_str (&ch->topic, ""); return 1;
	case 333:
	  ch = chan_find (c, param (m, 1));
	  if (ch) { set_str (&ch->topic_by, param (m, 2)); ch->topic_time = strtoul (param (m, 3), NULL, 10); }
	  return 1;
	case 324:						/* the modes of the channel: replace what we have; the caller shows the reply as well */
	  ch = chan_find (c, param (m, 1));
	  if (ch) { kv_clear (ch->modes, &ch->nmodes); apply_modes (c, ch, m, 2); }
	  return 0;
	case 352:						/* WHO reply: [me, channel, user, host, server, nick, flags, "hops real name"] */
	  if ((u = user_find (c, param (m, 5))) != NULL)
	    {
	      const char *real = strchr (param (m, 7), ' ');
	      user_info (u, param (m, 2), param (m, 3));
	      u->away = param (m, 6)[0] == 'G';
	      if (real) set_str (&u->realname, real + 1);
	    }
	  return 0;
	}
    }
  return 0;
}

/* ---- the state of the connection ---- */

void irc__chan_reset (irc_conn *c)
{
  size_t i;
  while (c->nchannels) chan_remove (c, c->channels[c->nchannels - 1]);
  for (i = 0; i < c->nusers; i++) user_free (c->users[i]);		/* none should be left */
  c->nusers = 0;
  free (c->prefix_modes); free (c->prefix_syms); c->prefix_modes = c->prefix_syms = NULL;
  for (i = 0; i < 4; i++) { free (c->chanmodes[i]); c->chanmodes[i] = NULL; }
}

void irc__chan_free (irc_conn *c)
{
  irc__chan_reset (c);
  free (c->users); free (c->channels);
}

/* ---- for the program that uses the library ---- */

size_t irc_conn_nchannels (irc_conn *c) { return c->nchannels; }
const irc_channel *irc_conn_channel_at (irc_conn *c, size_t i) { return i < c->nchannels ? c->channels[i] : NULL; }
const irc_channel *irc_conn_channel (irc_conn *c, const char *name) { return chan_find (c, name); }
const char *irc_channel_name (const irc_channel *ch) { return ch->name; }
const char *irc_channel_topic (const irc_channel *ch) { return ch->topic; }
const char *irc_channel_topic_by (const irc_channel *ch) { return ch->topic_by ? ch->topic_by : ""; }
unsigned long irc_channel_topic_time (const irc_channel *ch) { return ch->topic_time; }
int irc_channel_synced (const irc_channel *ch) { return ch->synced; }
size_t irc_channel_nmembers (const irc_channel *ch) { return ch->n; }

const irc_member *irc_channel_member (irc_conn *c, const irc_channel *ch, size_t i)
{
  chan_sort (c, (irc_channel *) ch);
  return i < ch->n ? &ch->members[i] : NULL;
}

const irc_member *irc_channel_find (irc_conn *c, const irc_channel *ch, const char *nick)
{
  int found;
  size_t i = member_pos (c, (irc_channel *) ch, nick, &found);
  return found ? &ch->members[i] : NULL;
}

const char *irc_conn_prefix_modes (irc_conn *c) { return pmodes (c); }
const char *irc_conn_prefix_symbols (irc_conn *c) { return psyms (c); }

int irc_member_rank (irc_conn *c, const irc_member *m)
{
  int i, n = (int) strlen (pmodes (c));
  for (i = 0; i < n; i++) if (m->prefix & (1u << i)) return i;
  return n;
}

char irc_member_symbol (irc_conn *c, const irc_member *m)
{
  int r = irc_member_rank (c, m);
  return r < (int) strlen (psyms (c)) ? psyms (c)[r] : 0;
}

const irc_user *irc_conn_user (irc_conn *c, const char *nick) { return user_find (c, nick); }

int irc_conn_selfcheck (irc_conn *c)
{
  size_t i, j;
  for (i = 1; i < c->nusers; i++) if (irc_casecmp (c->users[i - 1]->nick, c->users[i]->nick, c->casemap) >= 0) return 1;
  for (i = 0; i < c->nusers; i++) if (c->users[i]->refs <= 0) return 2;
  for (i = 0; i < c->nchannels; i++)
    {
      irc_channel *ch = c->channels[i];
      chan_sort (c, ch);
      for (j = 1; j < ch->n; j++) if (irc_casecmp (ch->members[j - 1].user->nick, ch->members[j].user->nick, c->casemap) >= 0) return 3;
      for (j = 0; j < ch->n; j++)
	{
	  int found;
	  size_t k = user_pos (c, ch->members[j].user->nick, &found);
	  if (!found || c->users[k] != ch->members[j].user) return 4;
	}
    }
  for (i = 0; i < c->nusers; i++)
    {
      int count = 0;
      for (j = 0; j < c->nchannels; j++)
	{
	  int found;
	  member_pos (c, c->channels[j], c->users[i]->nick, &found);
	  if (found) count++;
	}
      if (count != c->users[i]->refs) return 5;
    }
  return 0;
}
