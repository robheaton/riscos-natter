/* settings.c - see settings.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "settings.h"

static void copy (char *dst, size_t size, const char *src)
{
  size_t n = strlen (src);
  if (n >= size) n = size - 1;
  memcpy (dst, src, n); dst[n] = 0;
}

void settings_defaults (settings *s)
{
  memset (s, 0, sizeof *s);
  copy (s->nick, sizeof s->nick, "natter_user");
  copy (s->user, sizeof s->user, "natter");
  copy (s->realname, sizeof s->realname, "Natter on RISC OS 64");
  s->log = 1;
  s->beep = 1;
  s->font_size = FONT_DEFAULT;
}

static void net_defaults (netsettings *n, const char *name)
{
  memset (n, 0, sizeof *n);
  copy (n->name, sizeof n->name, name);
  n->tls = 1;
  n->log = -1;
}

int net_port (const netsettings *n) { return n->port > 0 ? n->port : n->tls ? 6697 : 6667; }

int settings_find (const settings *s, const char *name)
{
  int i;
  for (i = 0; i < s->nnets; i++)
    {
      const char *a = s->nets[i].name, *b = name;
      while (*a && *b && tolower ((unsigned char) *a) == tolower ((unsigned char) *b)) { a++; b++; }
      if (!*a && !*b) return i;
    }
  return -1;
}

netsettings *settings_add (settings *s, const char *name)
{
  int i = settings_find (s, name);
  if (i >= 0) return &s->nets[i];
  if (s->nnets >= MAX_NETWORKS) return NULL;
  net_defaults (&s->nets[s->nnets], name);
  return &s->nets[s->nnets++];
}

void settings_remove (settings *s, int index)
{
  if (index < 0 || index >= s->nnets) return;
  memmove (&s->nets[index], &s->nets[index + 1], (size_t) (s->nnets - index - 1) * sizeof s->nets[0]);
  s->nnets--;
}

static const char *const colour_keys[NCOLOURS] = { "colour_background", "colour_text", "colour_faded", "colour_mention", "colour_lists" };

int colour_parse (const char *hex, unsigned *rgb)
{
  unsigned v = 0;
  int i;
  if (!hex || strlen (hex) != 6) return 0;
  for (i = 0; i < 6; i++)
    {
      int c = (unsigned char) hex[i];
      if (c >= '0' && c <= '9') v = v * 16 + (unsigned) (c - '0');
      else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned) (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v = v * 16 + (unsigned) (c - 'A' + 10);
      else return 0;
    }
  if (rgb) *rgb = v;
  return 1;
}

int font_size_clamp (int pt) { return pt < FONT_MIN ? FONT_MIN : pt > FONT_MAX ? FONT_MAX : pt; }

static int yes (const char *v) { return !strcasecmp (v, "yes") || !strcasecmp (v, "on") || !strcasecmp (v, "true") || !strcmp (v, "1"); }

static char *trim (char *p)
{
  char *e;
  while (*p == ' ' || *p == '\t') p++;
  e = p + strlen (p);
  while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return p;
}

static void set_net_key (netsettings *n, const char *k, const char *v)
{
  if (!strcasecmp (k, "host")) copy (n->host, sizeof n->host, v);
  else if (!strcasecmp (k, "port")) n->port = atoi (v) > 0 && atoi (v) < 65536 ? atoi (v) : 0;
  else if (!strcasecmp (k, "tls")) n->tls = yes (v);
  else if (!strcasecmp (k, "nick")) copy (n->nick, sizeof n->nick, v);
  else if (!strcasecmp (k, "user")) copy (n->user, sizeof n->user, v);
  else if (!strcasecmp (k, "realname")) copy (n->realname, sizeof n->realname, v);
  else if (!strcasecmp (k, "sasl_user")) copy (n->sasl_user, sizeof n->sasl_user, v);
  else if (!strcasecmp (k, "sasl_pass")) copy (n->sasl_pass, sizeof n->sasl_pass, v);
  else if (!strcasecmp (k, "password")) copy (n->password, sizeof n->password, v);
  else if (!strcasecmp (k, "autojoin")) copy (n->autojoin, sizeof n->autojoin, v);
  else if (!strcasecmp (k, "connect")) n->autoconnect = yes (v);
  else if (!strcasecmp (k, "log")) n->log = !strcasecmp (v, "default") ? -1 : yes (v);
}

int settings_load (settings *s, const char *path)
{
  FILE *f = fopen (path, "r");
  char line[512];
  netsettings *cur = NULL;
  int skipping = 0;
  settings_defaults (s);
  if (!f) return -1;
  while (fgets (line, sizeof line, f))
    {
      char *p = trim (line), *eq;
      if (!*p || *p == '#' || *p == ';') continue;
      if (*p == '[')
	{
	  char *close = strchr (p, ']');
	  if (close) { *close = 0; p = trim (p + 1); cur = *p ? settings_add (s, p) : NULL; skipping = (cur == NULL); }
	  continue;
	}
      eq = strchr (p, '=');
      if (!eq) continue;
      *eq = 0;
      {
	char *k = trim (p), *v = trim (eq + 1);
	if (skipping) continue;
	if (cur) set_net_key (cur, k, v);
	else if (!strcasecmp (k, "nick")) copy (s->nick, sizeof s->nick, v);
	else if (!strcasecmp (k, "user")) copy (s->user, sizeof s->user, v);
	else if (!strcasecmp (k, "realname")) copy (s->realname, sizeof s->realname, v);
	else if (!strcasecmp (k, "log")) s->log = yes (v);
	else if (!strcasecmp (k, "beep")) s->beep = yes (v);
	else if (!strcasecmp (k, "logdir")) copy (s->logdir, sizeof s->logdir, v);
	else if (!strcasecmp (k, "debug_log")) copy (s->debuglog, sizeof s->debuglog, v);
	else if (!strcasecmp (k, "font_size")) s->font_size = atoi (v) > 0 ? font_size_clamp (atoi (v)) : FONT_DEFAULT;
	else if (!strcasecmp (k, "theme")) s->theme = !strcasecmp (v, "dark") ? THEME_DARK : THEME_LIGHT;
	else
	  {
	    int c;
	    for (c = 0; c < NCOLOURS; c++)
	      if (!strcasecmp (k, colour_keys[c])) { if (colour_parse (v, NULL)) copy (s->colours[c], sizeof s->colours[c], v); else s->colours[c][0] = 0; }
	  }
      }
    }
  fclose (f);
  return 0;
}

static void put (FILE *f, const char *key, const char *value)	/* a value without a line break */
{
  const char *p;
  fprintf (f, "%s = ", key);
  for (p = value; *p; p++) if (*p != '\r' && *p != '\n') fputc (*p, f);
  fputc ('\n', f);
}

int settings_save (const settings *s, const char *path)
{
  FILE *f = fopen (path, "w");
  int i;
  if (!f) return -1;
  fprintf (f, "# Natter choices.  The passwords are here as you typed them: keep this file private.\n");
  put (f, "nick", s->nick); put (f, "user", s->user); put (f, "realname", s->realname);
  fprintf (f, "log = %s\n", s->log ? "yes" : "no");
  fprintf (f, "beep = %s\n", s->beep ? "yes" : "no");
  if (s->logdir[0]) put (f, "logdir", s->logdir);
  if (s->debuglog[0]) put (f, "debug_log", s->debuglog);
  fprintf (f, "font_size = %d\ntheme = %s\n", font_size_clamp (s->font_size), s->theme == THEME_DARK ? "dark" : "light");
  for (i = 0; i < NCOLOURS; i++) if (colour_parse (s->colours[i], NULL)) put (f, colour_keys[i], s->colours[i]);
  for (i = 0; i < s->nnets; i++)
    {
      const netsettings *n = &s->nets[i];
      fprintf (f, "\n[%s]\n", n->name);
      put (f, "host", n->host);
      if (n->port) fprintf (f, "port = %d\n", n->port);
      fprintf (f, "tls = %s\n", n->tls ? "yes" : "no");
      if (n->nick[0]) put (f, "nick", n->nick);
      if (n->user[0]) put (f, "user", n->user);
      if (n->realname[0]) put (f, "realname", n->realname);
      if (n->sasl_user[0]) put (f, "sasl_user", n->sasl_user);
      if (n->sasl_pass[0]) put (f, "sasl_pass", n->sasl_pass);
      if (n->password[0]) put (f, "password", n->password);
      if (n->autojoin[0]) put (f, "autojoin", n->autojoin);
      fprintf (f, "connect = %s\n", n->autoconnect ? "yes" : "no");
      if (n->log >= 0) fprintf (f, "log = %s\n", n->log ? "yes" : "no");
    }
  return fclose (f) == 0 ? 0 : -1;
}
