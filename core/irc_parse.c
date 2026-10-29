/* irc_parse.c - message parsing, the line splitter, name folding.  No allocation, no operating system calls. */
#include <string.h>
#include "irc.h"

/* ---- the tags of IRCv3: @key=value;key2;+vendor/key3=a\sb ---- */

static void unescape_tag (char *v)
{
  char *r = v, *w = v;
  while (*r)
    {
      if (*r == '\\')
	{
	  r++;
	  if (!*r) break;				/* a lone backslash at the end is dropped */
	  switch (*r)
	    {
	    case ':': *w++ = ';'; break;
	    case 's': *w++ = ' '; break;
	    case 'r': *w++ = '\r'; break;
	    case 'n': *w++ = '\n'; break;
	    default: *w++ = *r; break;			/* \\ and any other escaped character stand for the character */
	    }
	  r++;
	}
      else *w++ = *r++;
    }
  *w = 0;
}

int irc_parse (char *line, irc_msg *m)
{
  char *p = line;
  memset (m, 0, sizeof *m);
  if (*p == '@')
    {
      char *end = strchr (p, ' ');
      char *t;
      if (!end) return -1;
      *end = 0;
      t = p + 1;
      while (*t && m->ntags < IRC_MAX_TAGS)
	{
	  char *semi = strchr (t, ';'), *eq;
	  if (semi) *semi = 0;
	  eq = strchr (t, '=');
	  if (eq) { *eq = 0; unescape_tag (eq + 1); }
	  if (*t)
	    {
	      m->tags[m->ntags].key = t;
	      m->tags[m->ntags].value = eq ? eq + 1 : "";
	      m->ntags++;
	    }
	  if (!semi) break;
	  t = semi + 1;
	}
      p = end + 1;
    }
  while (*p == ' ') p++;
  if (*p == ':')
    {
      char *end = strchr (p, ' '), *bang, *at;
      if (!end) return -1;
      *end = 0;
      m->prefix = p + 1;
      if (!*m->prefix) return -1;
      m->nick = m->prefix;
      bang = strchr (m->prefix, '!');
      at = strchr (bang ? bang + 1 : m->prefix, '@');
      if (bang) { *bang = 0; m->user = bang + 1; }
      if (at) { *at = 0; m->host = at + 1; }
      p = end + 1;
      while (*p == ' ') p++;
    }
  if (!*p) return -1;
  m->command = p;
  while (*p && *p != ' ')
    {
      if (*p >= 'a' && *p <= 'z') *p -= 32;
      p++;
    }
  if (*p) { *p++ = 0; }
  for (;;)
    {
      while (*p == ' ') p++;
      if (!*p) break;
      if (m->nparams == IRC_MAX_PARAMS - 1)		/* the last place takes all that is left, spaces and all */
	{
	  m->params[m->nparams++] = (*p == ':') ? p + 1 : p;
	  break;
	}
      if (*p == ':') { m->params[m->nparams++] = p + 1; break; }
      m->params[m->nparams++] = p;
      while (*p && *p != ' ') p++;
      if (*p) *p++ = 0;
    }
  return 0;
}

const char *irc_tag_get (const irc_msg *m, const char *key)
{
  int i;
  for (i = 0; i < m->ntags; i++)
    if (!strcmp (m->tags[i].key, key)) return m->tags[i].value;
  return NULL;
}

/* ---- lines ---- */

void irc_linebuf_init (irc_linebuf *lb) { lb->len = 0; lb->skipping = 0; }

void irc_linebuf_feed (irc_linebuf *lb, const char *data, size_t n, irc_line_fn fn, void *ud)
{
  size_t i;
  for (i = 0; i < n; i++)
    {
      char ch = data[i];
      if (ch == '\n')
	{
	  if (!lb->skipping)
	    {
	      if (lb->len && lb->buf[lb->len - 1] == '\r') lb->len--;
	      lb->buf[lb->len] = 0;
	      if (lb->len) fn (ud, lb->buf);
	    }
	  lb->len = 0; lb->skipping = 0;
	}
      else if (!lb->skipping)
	{
	  if (lb->len >= IRC_MAX_LINE - 1) { lb->skipping = 1; lb->len = 0; }	/* too long: drop it up to the next newline */
	  else if (ch == 0) lb->buf[lb->len++] = ' ';					/* a NUL would end the string early */
	  else lb->buf[lb->len++] = ch;
	}
    }
}

/* ---- names ---- */

int irc_fold (int c, int casemap)
{
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (casemap == IRC_CASE_ASCII) return c;
  switch (c)
    {
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case '~': return casemap == IRC_CASE_RFC1459 ? '^' : c;
    }
  return c;
}

int irc_casecmp (const char *a, const char *b, int casemap)
{
  for (;; a++, b++)
    {
      int x = irc_fold ((unsigned char) *a, casemap), y = irc_fold ((unsigned char) *b, casemap);
      if (x != y) return x < y ? -1 : 1;
      if (!x) return 0;
    }
}

size_t irc_utf8_trim (const char *s, size_t len, size_t max)
{
  size_t n = len < max ? len : max;
  if (n < len)
    while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80) n--;	/* s[n] would be the first byte left out: it must not be a continuation byte */
  return n;
}
