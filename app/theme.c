/* theme.c - see theme.h. */
#include <string.h>
#include "theme.h"

unsigned colour_bgr (unsigned rgb) { return ((rgb & 0xFF) << 24) | (((rgb >> 8) & 0xFF) << 16) | (((rgb >> 16) & 0xFF) << 8); }

void colour_hex (unsigned bgr, char out[8])
{
  static const char digits[] = "0123456789ABCDEF";
  unsigned r = (bgr >> 8) & 0xFF, g = (bgr >> 16) & 0xFF, b = (bgr >> 24) & 0xFF;
  out[0] = digits[r >> 4]; out[1] = digits[r & 15]; out[2] = digits[g >> 4]; out[3] = digits[g & 15]; out[4] = digits[b >> 4]; out[5] = digits[b & 15]; out[6] = 0;
}

static void light (palette *p)
{
  static const unsigned nicks[NICK_COLOURS] = { 0xC0600000, 0x50A03000, 0x2020D000, 0x00A0D000, 0xA0306000, 0x80808000, 0xA0A00000, 0x20208000 };
  p->back = 0xFFFFFF00; p->text = 0x00000000; p->faded = 0x80808000; p->mention = 0xC0FFFF00; p->lists = 0xF0F0F000;
  p->selected = 0xFFE8D000; p->error = 0x0000C800; p->marker = 0x2020D000; p->badge = 0xD0D0D000; p->query = 0x40802000;
  memcpy (p->nick, nicks, sizeof p->nick);
}

static void dark (palette *p)
{
  static const unsigned nicks_rgb[NICK_COLOURS] = { 0x5AA9FF, 0x4CD07D, 0xFF6B6B, 0xF0C040, 0xB58CFF, 0xA0A0A0, 0x40D0D0, 0xE08A5A };
  int i;
  p->back = colour_bgr (0x1E1E1E); p->text = colour_bgr (0xE0E0E0); p->faded = colour_bgr (0x8C8C8C); p->mention = colour_bgr (0x4A4420); p->lists = colour_bgr (0x2A2A2A);
  p->selected = colour_bgr (0x36507A); p->error = colour_bgr (0xFF6B6B); p->marker = colour_bgr (0xFF5555); p->badge = colour_bgr (0x505050); p->query = colour_bgr (0x4CD07D);
  for (i = 0; i < NICK_COLOURS; i++) p->nick[i] = colour_bgr (nicks_rgb[i]);
}

void palette_make (const settings *s, palette *p)
{
  unsigned rgb;
  if (s->theme == THEME_DARK) dark (p); else light (p);
  if (colour_parse (s->colours[COLOUR_BACK], &rgb)) p->back = colour_bgr (rgb);
  if (colour_parse (s->colours[COLOUR_TEXT], &rgb)) p->text = colour_bgr (rgb);
  if (colour_parse (s->colours[COLOUR_FADED], &rgb)) p->faded = colour_bgr (rgb);
  if (colour_parse (s->colours[COLOUR_MENTION], &rgb)) p->mention = colour_bgr (rgb);
  if (colour_parse (s->colours[COLOUR_LISTS], &rgb)) p->lists = colour_bgr (rgb);
}
