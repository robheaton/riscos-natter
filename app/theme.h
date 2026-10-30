/* theme.h - the colours of the windows: a light and a dark set, with the colours that the user has chosen on top.  No Wimp calls.
   A colour is 0xBBGGRR00, the form that ColourTrans takes. */
#ifndef NATTER_THEME_H
#define NATTER_THEME_H

#include "buffer.h"
#include "settings.h"

typedef struct
{
  unsigned back;		/* the background of the conversation */
  unsigned text;		/* what people say */
  unsigned faded;		/* times, joins, parts, and the other lines that are not talk */
  unsigned mention;		/* the band behind a line that mentions our nick */
  unsigned lists;		/* the background of the buffer list and the nick list */
  unsigned selected;		/* the buffer that is shown, and selected text */
  unsigned error;		/* errors, and the badge of unread mentions */
  unsigned marker;		/* the line above the first new message */
  unsigned badge;		/* the badge of unread lines */
  unsigned query;		/* the name of a private conversation in the buffer list */
  unsigned nick[NICK_COLOURS];	/* the colours of the nicks */
} palette;

void palette_make (const settings *s, palette *p);	/* the theme of s, then its own colours over it */
unsigned colour_bgr (unsigned rgb);			/* 0xRRGGBB to 0xBBGGRR00 */
void colour_hex (unsigned bgr, char out[8]);		/* 0xBBGGRR00 to "RRGGBB" */

#endif
