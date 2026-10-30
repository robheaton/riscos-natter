/* test_logfile.c - tests of the log files: names, lines, and what is written where. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "buffer.h"
#include "logfile.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static const char *safe (const char *s) { static char buf[8][100]; static int k; char *b = buf[k++ & 7]; log_safe_name (s, b, sizeof buf[0]); return b; }

static char *slurp (const char *path)
{
  static char data[1 << 20];
  FILE *f = fopen (path, "rb");
  size_t n;
  if (!f) return NULL;
  n = fread (data, 1, sizeof data - 1, f); data[n] = 0; fclose (f);
  return data;
}

static int count_files (const char *dir)
{
  DIR *d = opendir (dir);
  struct dirent *e;
  int n = 0;
  if (!d) return -1;
  while ((e = readdir (d)) != NULL) if (e->d_name[0] != '.') n++;
  closedir (d);
  return n;
}

int main (void)
{
  char base[200], path[500], line[400];
  logger *l;
  char *content;

  /* names */
  CHECK_STR (safe ("#riscos64"), "riscos64"); CHECK_STR (safe ("##gccsdk"), "_gccsdk"); CHECK_STR (safe ("&local"), "amp-local"); CHECK_STR (safe ("+modeless"), "plus-modeless"); CHECK_STR (safe ("!safe"), "bang-safe");
  CHECK_STR (safe ("Libera.Chat"), "Libera_Chat"); CHECK_STR (safe ("a/b:c$d&e@f^g%h\\i*j\"k|l#m"), "a_b_c_d_e_f_g_h_i_j_k_l_m");
  CHECK_STR (safe ("with space"), "with_space"); CHECK_STR (safe ("caf\xc3\xa9"), "caf__"); CHECK_STR (safe ("a[b]{c}-d_e"), "a[b]{c}-d_e"); CHECK_STR (safe ("#"), "_"); CHECK_STR (safe (""), "_"); CHECK_STR (safe ("..."), "___");
  CHECK_INT (strlen (safe ("#aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")), 60);
  CHECK (strcmp (safe ("#Chan"), safe ("#chan")) != 0);					/* case is kept in the name (the file system decides about case) */
  CHECK (strcmp (safe ("#chan"), safe ("&chan")) != 0);					/* different channels, different files */
  { char tiny[4]; log_safe_name ("#riscos64", tiny, sizeof tiny); CHECK_STR (tiny, "ris"); }

  /* lines */
  log_format (line, sizeof line, 1761739451L, 0, LK_TEXT, "carol", "hello there"); CHECK_STR (line, "2025-10-29 12:04:11 <carol> hello there");
  log_format (line, sizeof line, 1761739451L, 3600, LK_TEXT, "carol", "later"); CHECK_STR (line, "2025-10-29 13:04:11 <carol> later");
  log_format (line, sizeof line, 1761739451L, -3600, LK_TEXT, "carol", "earlier"); CHECK_STR (line, "2025-10-29 11:04:11 <carol> earlier");
  log_format (line, sizeof line, 1761739451L, 0, LK_ACTION, "dave", "dave waves"); CHECK_STR (line, "2025-10-29 12:04:11 * dave waves");
  log_format (line, sizeof line, 1761739451L, 0, LK_NOTICE, "serv", "a notice"); CHECK_STR (line, "2025-10-29 12:04:11 -serv- a notice");
  log_format (line, sizeof line, 1761739451L, 0, LK_JOIN, NULL, "bob (u@h) has joined"); CHECK_STR (line, "2025-10-29 12:04:11 --> bob (u@h) has joined");
  log_format (line, sizeof line, 1761739451L, 0, LK_QUIT, NULL, "erin has quit: gone"); CHECK_STR (line, "2025-10-29 12:04:11 <-- erin has quit: gone");
  log_format (line, sizeof line, 1761739451L, 0, LK_TOPIC, NULL, "the topic"); CHECK_STR (line, "2025-10-29 12:04:11 -- the topic");
  log_format (line, sizeof line, 1761739451L, 0, LK_TEXT, "x", "two\r\nlines"); CHECK_STR (line, "2025-10-29 12:04:11 <x> two  lines");		/* a line break cannot start a second line of the file */
  log_format (line, sizeof line, 1761739451L, 0, LK_TEXT, NULL, "no nick"); CHECK_STR (line, "2025-10-29 12:04:11 <?> no nick");
  { char tiny[20]; size_t n = log_format (tiny, sizeof tiny, 1761739451L, 0, LK_TEXT, "carol", "cut short"); CHECK (n > 19); CHECK_INT (strlen (tiny), 19); }

  /* the files */
  snprintf (base, sizeof base, "/tmp/natter-logtest-%d", (int) getpid ());
  l = logger_new (base, "/");
  logger_add (l, "Libera.Chat", BK_CHANNEL, "#riscos64", "line one");
  logger_add (l, "Libera.Chat", BK_CHANNEL, "#riscos64", "line two");
  logger_add (l, "Libera.Chat", BK_QUERY, "bob", "a query line");
  logger_add (l, "Libera.Chat", BK_STATUS, "Status", "a status line");
  logger_add (l, "OFTC", BK_CHANNEL, "#debian-arm", "another network");
  logger_add (l, "Libera.Chat", BK_CHANNEL, "#bob", "a channel with the name of a person");
  snprintf (path, sizeof path, "%s/Libera_Chat/riscos64", base);
  CHECK (slurp (path) == NULL);									/* nothing is written until the flush */
  logger_flush (l);
  content = slurp (path); CHECK (content != NULL); if (content) CHECK_STR (content, "line one\nline two\n");
  snprintf (path, sizeof path, "%s/Libera_Chat/Queries/bob", base); content = slurp (path); CHECK (content != NULL); if (content) CHECK_STR (content, "a query line\n");
  snprintf (path, sizeof path, "%s/Libera_Chat/bob", base); content = slurp (path); CHECK (content != NULL); if (content) CHECK_STR (content, "a channel with the name of a person\n");
  snprintf (path, sizeof path, "%s/Libera_Chat/!Status", base); content = slurp (path); CHECK (content != NULL); if (content) CHECK_STR (content, "a status line\n");
  snprintf (path, sizeof path, "%s/OFTC/debian-arm", base); content = slurp (path); CHECK (content != NULL); if (content) CHECK_STR (content, "another network\n");
  CHECK_INT (logger_errors (l), 0);
  logger_add (l, "Libera.Chat", BK_CHANNEL, "#riscos64", "line three");				/* later lines are added */
  logger_flush (l); logger_flush (l);								/* a second flush adds nothing again */
  snprintf (path, sizeof path, "%s/Libera_Chat/riscos64", base); content = slurp (path); if (content) CHECK_STR (content, "line one\nline two\nline three\n");
  logger_path (l, "Libera.Chat", BK_QUERY, "bob", path, sizeof path); CHECK (strstr (path, "Libera_Chat/Queries/bob") != NULL);

  /* a big pile of lines is written without waiting for the flush; many conversations are fine */
  { int i; char big[1000];
    memset (big, 'z', 999); big[999] = 0;
    for (i = 0; i < 100; i++) logger_add (l, "Big", BK_CHANNEL, "#big", big);
    snprintf (path, sizeof path, "%s/Big/big", base);
    content = slurp (path); CHECK (content != NULL);					/* 100,000 bytes: it went out at 65536 */
    for (i = 0; i < 200; i++) { char nm[20]; snprintf (nm, sizeof nm, "#c%d", i); logger_add (l, "Many", BK_CHANNEL, nm, "x"); }
    logger_flush (l);
    snprintf (path, sizeof path, "%s/Many", base); CHECK_INT (count_files (path), 200); }
  CHECK_INT (logger_errors (l), 0);
  logger_free (l);

  /* a folder that cannot be made is counted, and nothing crashes */
  l = logger_new ("/nonexistent-natter/folder", "/");
  logger_add (l, "Net", BK_CHANNEL, "#x", "lost"); logger_flush (l);
  CHECK (logger_errors (l) > 0);
  logger_free (l);

  /* RISC OS style paths: the separator is a dot (the files are made here under dotted names, which is only to see the paths) */
  l = logger_new ("Logs", ".");
  logger_path (l, "Libera.Chat", BK_CHANNEL, "#riscos64", path, sizeof path); CHECK_STR (path, "Logs.Libera_Chat.riscos64");
  logger_path (l, "Libera.Chat", BK_STATUS, "x", path, sizeof path); CHECK_STR (path, "Logs.Libera_Chat.!Status");
  logger_path (l, "Libera.Chat", BK_QUERY, "bob", path, sizeof path); CHECK_STR (path, "Logs.Libera_Chat.Queries.bob");
  { char cmd[300]; snprintf (cmd, sizeof cmd, "rm -rf %s '%s'", base, "Logs.Libera_Chat.riscos64"); if (system (cmd)) {} }
  logger_free (l);
  { char cmd[300]; snprintf (cmd, sizeof cmd, "rm -rf %s Logs* 2>/dev/null", base); if (system (cmd)) {} }

  printf ("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
