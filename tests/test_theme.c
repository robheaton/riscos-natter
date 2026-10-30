/* test_theme.c - tests of the colours: the two themes, the colours of the user on top of them, and the settings that hold them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "theme.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp (a_, b_)) { fails++; printf ("FAIL %s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_HEX(a, b) do { unsigned long a_ = (unsigned long) (a), b_ = (unsigned long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %08lX, expected %08lX\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_INT(a, b) do { long a_ = (long) (a), b_ = (long) (b); checks++; if (a_ != b_) { fails++; printf ("FAIL %s:%d: %s is %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

static void write_file (const char *path, const char *text) { FILE *f = fopen (path, "wb"); fputs (text, f); fclose (f); }

int main (void)
{
  settings s, t;
  palette p;
  unsigned v = 0;
  char path[256];
  int i;
  snprintf (path, sizeof path, "/tmp/natter-theme-%d", (int) getpid ());

  /* colour_parse and colour_bgr */
  CHECK (colour_parse ("102030", &v)); CHECK_HEX (v, 0x102030);
  CHECK (colour_parse ("aBcDeF", &v)); CHECK_HEX (v, 0xABCDEF);
  CHECK (colour_parse ("000000", NULL));
  CHECK (!colour_parse ("", &v)); CHECK (!colour_parse ("12345", &v)); CHECK (!colour_parse ("1234567", &v)); CHECK (!colour_parse ("12345G", &v)); CHECK (!colour_parse ("#102030", &v)); CHECK (!colour_parse (NULL, &v));
  CHECK_HEX (colour_bgr (0x112233), 0x33221100); CHECK_HEX (colour_bgr (0xFF0000), 0x0000FF00); CHECK_HEX (colour_bgr (0xFFFFFF), 0xFFFFFF00);
  { char h[8]; colour_hex (0x33221100, h); CHECK_STR (h, "112233"); colour_hex (colour_bgr (0xABCDEF), h); CHECK_STR (h, "ABCDEF"); colour_hex (0, h); CHECK_STR (h, "000000"); colour_hex (0xFFFFFF00, h); CHECK_STR (h, "FFFFFF"); }
  CHECK_INT (font_size_clamp (3), FONT_MIN); CHECK_INT (font_size_clamp (11), 11); CHECK_INT (font_size_clamp (99), FONT_MAX);

  /* the themes: light is what Natter always looked like */
  settings_defaults (&s);
  CHECK_INT (s.font_size, FONT_DEFAULT); CHECK_INT (s.theme, THEME_LIGHT);
  palette_make (&s, &p);
  CHECK_HEX (p.back, 0xFFFFFF00); CHECK_HEX (p.text, 0); CHECK_HEX (p.mention, 0xC0FFFF00); CHECK_HEX (p.lists, 0xF0F0F000); CHECK_HEX (p.nick[0], 0xC0600000);
  s.theme = THEME_DARK; palette_make (&s, &p);
  CHECK_HEX (p.back, 0x1E1E1E00); CHECK_HEX (p.text, 0xE0E0E000);
  CHECK (p.back != p.text && p.back != p.faded && p.back != p.lists);
  for (i = 0; i < NICK_COLOURS; i++)					/* a nick can be read on the background: the dark theme has no dark nick colours */
    {
      unsigned c = p.nick[i];
      unsigned lum = ((c >> 24) & 0xFF) + ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF);
      CHECK (lum > 3 * 0x70);
    }

  /* the colours of the user go over the theme; a bad one is ignored */
  snprintf (s.colours[COLOUR_BACK], sizeof s.colours[0], "102030"); snprintf (s.colours[COLOUR_TEXT], sizeof s.colours[0], "zzzzzz"); snprintf (s.colours[COLOUR_LISTS], sizeof s.colours[0], "ffffff");
  palette_make (&s, &p);
  CHECK_HEX (p.back, 0x30201000); CHECK_HEX (p.text, 0xE0E0E000); CHECK_HEX (p.lists, 0xFFFFFF00);

  /* the file */
  write_file (path, "font_size = 14\ntheme = Dark\ncolour_background = 001122\ncolour_text = nonsense\ncolour_faded=AABBCC\ncolour_mention = 12345\n");
  CHECK_INT (settings_load (&t, path), 0);
  CHECK_INT (t.font_size, 14); CHECK_INT (t.theme, THEME_DARK);
  CHECK_STR (t.colours[COLOUR_BACK], "001122"); CHECK_STR (t.colours[COLOUR_TEXT], ""); CHECK_STR (t.colours[COLOUR_FADED], "AABBCC"); CHECK_STR (t.colours[COLOUR_MENTION], "");
  write_file (path, "font_size = 99\n"); settings_load (&t, path); CHECK_INT (t.font_size, FONT_MAX);
  write_file (path, "font_size = 2\n"); settings_load (&t, path); CHECK_INT (t.font_size, FONT_MIN);
  write_file (path, "font_size = big\n"); settings_load (&t, path); CHECK_INT (t.font_size, FONT_DEFAULT);
  write_file (path, "theme = purple\n"); settings_load (&t, path); CHECK_INT (t.theme, THEME_LIGHT);
  write_file (path, "nick = x\n"); settings_load (&t, path); CHECK_INT (t.font_size, FONT_DEFAULT); CHECK_INT (t.theme, THEME_LIGHT); CHECK_STR (t.colours[COLOUR_BACK], "");	/* an old file has the defaults */

  settings_defaults (&s); s.font_size = 15; s.theme = THEME_DARK; snprintf (s.colours[COLOUR_LISTS], sizeof s.colours[0], "445566");
  CHECK_INT (settings_save (&s, path), 0);
  CHECK_INT (settings_load (&t, path), 0);
  CHECK_INT (t.font_size, 15); CHECK_INT (t.theme, THEME_DARK); CHECK_STR (t.colours[COLOUR_LISTS], "445566"); CHECK_STR (t.colours[COLOUR_BACK], "");

  unlink (path);
  printf ("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
