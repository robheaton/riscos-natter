#include <unistd.h>
/* test_settings.c - tests of the choices file: reading, writing, and what a hand-edited file can contain. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static const char *tmp (const char *name) { static char p[256]; snprintf (p, sizeof p, "/tmp/natter-test-%d-%s", (int) getpid (), name); return p; }
static void write_file (const char *path, const char *text) { FILE *f = fopen (path, "wb"); fputs (text, f); fclose (f); }

int main (void)
{
  settings s, t;
  const char *p = tmp ("choices");
  netsettings *n;
  int i;

  settings_defaults (&s);
  CHECK_STR (s.nick, "natter_user"); CHECK_INT (s.log, 1); CHECK_INT (s.nnets, 0); CHECK_INT (s.beep, 1);
  CHECK_INT (settings_load (&s, "/nonexistent/folder/file"), -1); CHECK_STR (s.nick, "natter_user");	/* a missing file leaves the defaults */

  write_file (p, "# my choices\nnick = alice\nuser=al\nrealname =  Alice A  \nlog = no\nlogdir = Logs\n\n"
		 "[Libera.Chat]\nhost = irc.libera.chat\ntls = yes\nsasl_user = alice\nsasl_pass = s3cret # not a comment\nautojoin = #riscos64,#riscosdev\nconnect = yes\n\n"
		 "[plain]\r\nhost = irc.example.org\r\nport = 6667\r\ntls = no\r\nnick = other\r\nlog = no\r\nunknown = value\r\nnonsense line\r\n"
		 "[libera.chat]\nport = 7000\n");
  CHECK_INT (settings_load (&s, p), 0);
  CHECK_STR (s.nick, "alice"); CHECK_STR (s.user, "al"); CHECK_STR (s.realname, "Alice A"); CHECK_INT (s.log, 0); CHECK_STR (s.logdir, "Logs");
  CHECK_INT (s.nnets, 2);										/* [libera.chat] is the same network as [Libera.Chat] */
  CHECK_INT (settings_find (&s, "LIBERA.CHAT"), 0); CHECK_INT (settings_find (&s, "plain"), 1); CHECK_INT (settings_find (&s, "nope"), -1);
  n = &s.nets[0];
  CHECK_STR (n->name, "Libera.Chat"); CHECK_STR (n->host, "irc.libera.chat"); CHECK_INT (n->tls, 1); CHECK_INT (n->port, 7000); CHECK_INT (net_port (n), 7000);
  CHECK_STR (n->sasl_user, "alice"); CHECK_STR (n->sasl_pass, "s3cret # not a comment"); CHECK_STR (n->autojoin, "#riscos64,#riscosdev"); CHECK_INT (n->autoconnect, 1); CHECK_INT (n->log, -1);
  n = &s.nets[1];
  CHECK_STR (n->host, "irc.example.org"); CHECK_INT (n->port, 6667); CHECK_INT (n->tls, 0); CHECK_STR (n->nick, "other"); CHECK_INT (n->log, 0); CHECK_INT (n->autoconnect, 0);
  n = settings_add (&s, "fresh");
  CHECK (n != NULL); CHECK_INT (n->tls, 1); CHECK_INT (net_port (n), 6697); n->tls = 0; CHECK_INT (net_port (n), 6667);

  /* write and read back */
  CHECK_INT (settings_save (&s, tmp ("saved")), 0);
  CHECK_INT (settings_load (&t, tmp ("saved")), 0);
  CHECK_INT (t.nnets, 3); CHECK_STR (t.nick, "alice"); CHECK_INT (t.log, 0);
  CHECK_STR (t.nets[0].sasl_pass, "s3cret # not a comment"); CHECK_STR (t.nets[1].nick, "other"); CHECK_INT (t.nets[1].port, 6667); CHECK_INT (t.nets[2].tls, 0); CHECK_INT (t.nets[0].autoconnect, 1);
  CHECK_INT (t.nets[0].log, -1); CHECK_INT (t.nets[1].log, 0);
  {									/* the beep (settings of its own: the checks below go on with s) */
    settings u, v;
    write_file (tmp ("beepfile"), "beep = no\n"); settings_load (&u, tmp ("beepfile")); CHECK_INT (u.beep, 0);
    CHECK_INT (settings_save (&u, tmp ("beep")), 0); settings_load (&v, tmp ("beep")); CHECK_INT (v.beep, 0);
    write_file (tmp ("beepfile"), "nick = x\n"); settings_load (&u, tmp ("beepfile")); CHECK_INT (u.beep, 1);			/* an old file: on */
  }
  CHECK_INT (settings_save (&s, "/nonexistent/folder/file"), -1);

  /* a value with a line break cannot start a new setting */
  strcpy (s.nets[0].autojoin, "#a\nsasl_pass = stolen");
  settings_save (&s, tmp ("inject")); settings_load (&t, tmp ("inject"));
  CHECK_STR (t.nets[0].sasl_pass, "s3cret # not a comment");

  /* removing, and the limit */
  settings_remove (&s, 0); CHECK_INT (s.nnets, 2); CHECK_STR (s.nets[0].name, "plain"); settings_remove (&s, 9); settings_remove (&s, -1); CHECK_INT (s.nnets, 2);
  settings_defaults (&s);
  for (i = 0; i < MAX_NETWORKS + 3; i++) { char nm[16]; snprintf (nm, sizeof nm, "n%d", i); n = settings_add (&s, nm); if (i < MAX_NETWORKS) CHECK (n != NULL); else CHECK (n == NULL); }
  CHECK_INT (s.nnets, MAX_NETWORKS);

  /* long values are cut, not overflowed; an empty section name is skipped with its lines */
  { char big[4000]; FILE *f = fopen (p, "w"); memset (big, 'a', sizeof big); big[3999] = 0;
    fprintf (f, "nick = %s\n[]\nhost = hidden\n[net]\nhost = %s\n", big, big); fclose (f); }
  settings_load (&t, p);
  CHECK_INT (strlen (t.nick), sizeof t.nick - 1); CHECK_INT (t.nnets, 1); CHECK_INT (strlen (t.nets[0].host), sizeof t.nets[0].host - 1);
  { FILE *f = fopen (p, "w"); fputs ("[net]\nport = 99999\nport2 = 5\ntls = maybe\n", f); fclose (f); }
  settings_load (&t, p);
  CHECK_INT (t.nets[0].port, 0); CHECK_INT (t.nets[0].tls, 0);						/* a port out of range is ignored; "maybe" is not yes */

  remove (p); remove (tmp ("saved")); remove (tmp ("inject"));
  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
