/* test_links.c - tests of finding web addresses in text. */
#include <stdio.h>
#include <string.h>
#include "links.h"

static int checks, fails;
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static char got[8][200];
static size_t find (const char *text)				/* the addresses of text, each copied to got[] */
{
  linkspan sp[8];
  size_t n = links_find (text, sp, 8), i;
  for (i = 0; i < n; i++) { size_t l = sp[i].to - sp[i].from; memcpy (got[i], text + sp[i].from, l); got[i][l] = 0; }
  return n;
}

int main (void)
{
  linkspan sp[4];
  char buf[64];
  CHECK_INT (find ("nothing here"), 0);
  CHECK_INT (find (""), 0);
  CHECK_INT (find ("see https://example.org/x now"), 1); CHECK_STR (got[0], "https://example.org/x");
  CHECK_INT (find ("http://a.b, then ftp://x.y/z."), 2); CHECK_STR (got[0], "http://a.b"); CHECK_STR (got[1], "ftp://x.y/z");	/* the comma and the full stop are not a part of it */
  CHECK_INT (find ("(https://example.org/a)"), 1); CHECK_STR (got[0], "https://example.org/a");
  CHECK_INT (find ("<https://example.org/a>"), 1); CHECK_STR (got[0], "https://example.org/a");
  CHECK_INT (find ("\"http://x.y/\""), 1); CHECK_STR (got[0], "http://x.y/");
  CHECK_INT (find ("https://en.wikipedia.org/wiki/Foo_(bar) and"), 1); CHECK_STR (got[0], "https://en.wikipedia.org/wiki/Foo_(bar)");	/* a ) that closes a ( inside stays */
  CHECK_INT (find ("(see https://en.wikipedia.org/wiki/Foo_(bar))"), 1); CHECK_STR (got[0], "https://en.wikipedia.org/wiki/Foo_(bar)");
  CHECK_INT (find ("HTTPS://EXAMPLE.ORG"), 1);							/* case does not matter */
  CHECK_INT (find ("www.riscos.info is it"), 1); CHECK_STR (got[0], "www.riscos.info");
  CHECK_INT (find ("www. and http:// and https:// alone"), 0);					/* not an address */
  CHECK_INT (find ("www.x"), 0);
  CHECK_INT (find ("xhttp://a.b"), 0);								/* it has to start the word */
  CHECK_INT (find ("a mailto:bob@x.y and file:///x"), 0);
  CHECK_INT (find ("https://a.b?q=1&r=2#frag!"), 1); CHECK_STR (got[0], "https://a.b?q=1&r=2#frag");
  CHECK_INT (links_find ("a http://one.b b http://two.c c http://three.d d http://four.e", sp, 2), 2);	/* up to max */

  links_find ("go www.riscos.info now", sp, 1);
  CHECK_INT (links_address ("go www.riscos.info now", &sp[0], buf, sizeof buf), 1); CHECK_STR (buf, "http://www.riscos.info");
  links_find ("go https://a.b/c now", sp, 1);
  CHECK_INT (links_address ("go https://a.b/c now", &sp[0], buf, sizeof buf), 1); CHECK_STR (buf, "https://a.b/c");
  CHECK_INT (links_address ("go https://a.b/c now", &sp[0], buf, 8), 0);				/* does not fit */
  printf ("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
