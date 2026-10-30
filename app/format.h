/* format.h - the formatting codes of IRC text (bold, italic, underline, reverse, colours): the text is split into the plain text and the runs of one style.  No Wimp calls.
   Every other control character is dropped: the Font Manager would take some of them for commands. */
#ifndef NATTER_FORMAT_H
#define NATTER_FORMAT_H

#include <stddef.h>

enum { FMT_BOLD = 1, FMT_ITALIC = 2, FMT_UNDERLINE = 4, FMT_REVERSE = 8, FMT_STRIKE = 16 };

#define FMT_DEFAULT_COLOUR (-1)
#define FMT_RGB_FLAG 0x1000000			/* a colour that is 0x1000000 | 0xRRGGBB is a hex colour (code 4); otherwise it is an index 0 to 15 (code 3), or FMT_DEFAULT_COLOUR */

typedef struct
{
  size_t off;				/* the byte of the plain text where the style starts; it lasts until the next run */
  int flags;				/* FMT_* */
  int fg, bg;				/* colours: see above */
} fmtrun;

/* Splits raw into *plain (to be freed) and *runs (to be freed; NULL, with *nruns 0, when the text has no formatting).  The text before the first run has the default style. */
void fmt_parse (const char *raw, char **plain, fmtrun **runs, size_t *nruns);

/* The style at a byte of the plain text: fills *out (the default style when there is no run there). */
void fmt_style_at (const fmtrun *runs, size_t nruns, size_t off, fmtrun *out);

/* A colour of a run as 0xRRGGBB, or -1 for the default one. */
long fmt_rgb (int colour);

#endif
