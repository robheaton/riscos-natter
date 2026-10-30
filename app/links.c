/* links.c - see links.h. */
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "links.h"

static int starts_with (const char *p, size_t len, const char *prefix)
{
  size_t n = strlen (prefix);
  return len >= n && !strncasecmp (p, prefix, n);
}

static size_t count (const char *p, size_t len, char c) { size_t i, n = 0; for (i = 0; i < len; i++) if (p[i] == c) n++; return n; }

size_t links_find (const char *text, linkspan *out, size_t max)
{
  size_t n = 0, i = 0, len = strlen (text);
  while (i < len && n < max)
    {
      size_t s, e;
      while (i < len && text[i] == ' ') i++;
      s = i;
      while (i < len && text[i] != ' ') i++;
      e = i;
      while (s < e && strchr ("([{<\"'", text[s])) s++;			/* what is round the address is not a part of it */
      for (;;)
	{
	  char c;
	  if (e <= s) break;
	  c = text[e - 1];
	  if (!strchr (".,;:!?)]}>\"'", c)) break;
	  if (c == ')' && count (text + s, e - s, '(') >= count (text + s, e - s, ')')) break;	/* a ) that closes a ( inside the address stays */
	  if (c == ']' && count (text + s, e - s, '[') >= count (text + s, e - s, ']')) break;
	  e--;
	}
      if (e <= s) continue;
      if ((starts_with (text + s, e - s, "http://") && e - s > 7) || (starts_with (text + s, e - s, "https://") && e - s > 8) ||
	  (starts_with (text + s, e - s, "ftp://") && e - s > 6) || (starts_with (text + s, e - s, "ftps://") && e - s > 7) ||
	  (starts_with (text + s, e - s, "www.") && e - s >= 8 && memchr (text + s + 4, '.', e - s - 4)))
	{
	  out[n].from = s; out[n].to = e; n++;
	}
    }
  return n;
}

int links_address (const char *text, const linkspan *s, char *buf, size_t size)
{
  size_t len = s->to - s->from, pre = starts_with (text + s->from, len, "www.") ? 7 : 0;
  if (len + pre + 1 > size) return 0;
  if (pre) memcpy (buf, "http://", 7);
  memcpy (buf + pre, text + s->from, len);
  buf[pre + len] = 0;
  return 1;
}
