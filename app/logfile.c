/* logfile.c - see logfile.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "buffer.h"
#include "logfile.h"

#define MAX_TARGETS 64			/* conversations with lines waiting */
#define MAX_PENDING 65536		/* so many bytes waiting for one of them: write now */
#define MAX_DIRS 128

typedef struct { char *path; char *data; size_t len, cap; } target;

struct logger
{
  char *dir, *sep;
  target t[MAX_TARGETS]; int nt;
  char *dirs[MAX_DIRS]; int ndirs;		/* folders that exist (or were made) already */
  int errors;
};

static char *xstrdup (const char *s) { size_t n = strlen (s) + 1; char *r = malloc (n); if (!r) abort (); memcpy (r, s, n); return r; }

/* ---- names ---- */

void log_safe_name (const char *name, char *out, size_t size)
{
  size_t n = 0;
  const char *p = name;
  #define PUT(c) do { if (n + 1 < size) out[n++] = (char) (c); } while (0)
  #define PUTS(s) do { const char *q_ = (s); while (*q_) PUT (*q_++); } while (0)
  if (!size) return;
  if (*p == '&') { PUTS ("amp-"); p++; }					/* channel types: "#riscos64" is "riscos64", and the others cannot be mixed up with it */
  else if (*p == '+') { PUTS ("plus-"); p++; }
  else if (*p == '!') { PUTS ("bang-"); p++; }
  else if (*p == '#') p++;
  for (; *p && n < 60; p++)
    {
      unsigned char c = (unsigned char) *p;
      if (c == '#') PUT ('_');							/* "##gccsdk" is "_gccsdk" */
      else if (c < 33 || c > 126 || strchr (".:\"|$&@^%\\*/`", c)) PUT ('_');	/* control characters, spaces, anything that is not ASCII, and what a path or a wildcard uses */
      else PUT (c);
    }
  if (!n) PUT ('_');
  out[n] = 0;
  #undef PUT
  #undef PUTS
}

static void join (char *buf, size_t size, const char *sep, const char *a, const char *b, const char *c)
{
  snprintf (buf, size, "%s%s%s%s%s", a, b ? sep : "", b ? b : "", c ? sep : "", c ? c : "");
}

void logger_path (const logger *l, const char *network, int bufkind, const char *bufname, char *buf, size_t size)
{
  char net[80], leaf[80], tmp[300];
  log_safe_name (network, net, sizeof net);
  if (bufkind == BK_STATUS) { join (buf, size, l->sep, l->dir, net, "!Status"); return; }
  log_safe_name (bufname, leaf, sizeof leaf);
  if (bufkind == BK_QUERY)
    {
      join (tmp, sizeof tmp, l->sep, l->dir, net, "Queries");
      join (buf, size, l->sep, tmp, leaf, NULL);
    }
  else join (buf, size, l->sep, l->dir, net, leaf);
}

/* ---- the text of a line ---- */

size_t log_format (char *out, size_t size, long when, long tz, int kind, const char *nick, const char *text)
{
  time_t tt = (time_t) (when + tz);
  struct tm *g = gmtime (&tt);
  char ts[40], clean[1100];
  size_t i, n = 0;
  if (!size) return 0;
  if (g) strftime (ts, sizeof ts, "%Y-%m-%d %H:%M:%S", g); else snprintf (ts, sizeof ts, "0000-00-00 00:00:00");
  for (i = 0; text[i] && n + 1 < sizeof clean; i++) clean[n++] = (text[i] == '\r' || text[i] == '\n') ? ' ' : text[i];
  clean[n] = 0;
  switch (kind)
    {
    case LK_TEXT: return (size_t) snprintf (out, size, "%s <%s> %s", ts, nick ? nick : "?", clean);
    case LK_NOTICE: return (size_t) snprintf (out, size, "%s -%s- %s", ts, nick ? nick : "?", clean);
    case LK_ACTION: return (size_t) snprintf (out, size, "%s * %s", ts, clean);
    case LK_JOIN: return (size_t) snprintf (out, size, "%s --> %s", ts, clean);
    case LK_PART: case LK_QUIT: return (size_t) snprintf (out, size, "%s <-- %s", ts, clean);
    default: return (size_t) snprintf (out, size, "%s -- %s", ts, clean);
    }
}

/* ---- writing ---- */

logger *logger_new (const char *dir, const char *sep)
{
  logger *l = calloc (1, sizeof *l);
  if (!l) abort ();
  l->dir = xstrdup (dir); l->sep = xstrdup (sep);
  return l;
}

static void ensure_dir (logger *l, const char *path)	/* every folder on the way to path, once */
{
  size_t i, seplen = strlen (l->sep), len = strlen (path);
  char buf[400];
  for (i = 1; i <= len && i < sizeof buf; i++)
    {
      if (i == len || !strncmp (path + i, l->sep, seplen))
	{
	  int k, known = 0;
	  memcpy (buf, path, i); buf[i] = 0;
	  for (k = 0; k < l->ndirs; k++) if (!strcmp (l->dirs[k], buf)) { known = 1; break; }
	  if (known) continue;
	  mkdir (buf, 0777);					/* it exists, or it is not a folder that can be made (a device or a variable): the file will say */
	  if (l->ndirs < MAX_DIRS) l->dirs[l->ndirs++] = xstrdup (buf);
	}
    }
}

static void write_target (logger *l, target *t)
{
  FILE *f;
  char folder[400];
  const char *last = NULL, *p;
  size_t seplen = strlen (l->sep);
  if (!t->len) return;
  for (p = t->path; *p; p++) if (!strncmp (p, l->sep, seplen)) last = p;
  if (last) { size_t n = (size_t) (last - t->path); if (n >= sizeof folder) n = sizeof folder - 1; memcpy (folder, t->path, n); folder[n] = 0; ensure_dir (l, folder); }
  f = fopen (t->path, "a");
  if (!f || fwrite (t->data, 1, t->len, f) != t->len) l->errors++;
  if (f) fclose (f);
  t->len = 0;
}

void logger_flush (logger *l)
{
  int i;
  for (i = 0; i < l->nt; i++) write_target (l, &l->t[i]);
}

void logger_add (logger *l, const char *network, int bufkind, const char *bufname, const char *line)
{
  char path[500];
  size_t n = strlen (line);
  int i;
  target *t = NULL;
  logger_path (l, network, bufkind, bufname, path, sizeof path);
  for (i = 0; i < l->nt; i++) if (!strcmp (l->t[i].path, path)) { t = &l->t[i]; break; }
  if (!t)
    {
      if (l->nt == MAX_TARGETS) { logger_flush (l); for (i = 0; i < l->nt; i++) { free (l->t[i].path); free (l->t[i].data); } l->nt = 0; }
      t = &l->t[l->nt++];
      t->path = xstrdup (path); t->data = NULL; t->len = t->cap = 0;
    }
  if (t->len + n + 2 > t->cap)
    {
      size_t cap = t->cap ? t->cap : 512;
      while (cap < t->len + n + 2) cap *= 2;
      t->data = realloc (t->data, cap);
      if (!t->data) abort ();
      t->cap = cap;
    }
  memcpy (t->data + t->len, line, n); t->len += n;
  t->data[t->len++] = '\n';
  if (t->len > MAX_PENDING) write_target (l, t);
}

int logger_errors (const logger *l) { return l->errors; }

void logger_free (logger *l)
{
  int i;
  if (!l) return;
  logger_flush (l);
  for (i = 0; i < l->nt; i++) { free (l->t[i].path); free (l->t[i].data); }
  for (i = 0; i < l->ndirs; i++) free (l->dirs[i]);
  free (l->dir); free (l->sep); free (l);
}
