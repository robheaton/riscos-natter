/* appearance.h - the Appearance window of Natter: the size of the text and the colours (a theme, and the colours that the user sets over it).  A dialogue box built in code, like the Choices window.
   The window edits a copy of the choices: Apply shows the result in the real windows, Save keeps it, Cancel puts back what it was. */
#ifndef NATTER_APPEARANCE_H
#define NATTER_APPEARANCE_H

#include "settings.h"

typedef struct
{
  void (*apply) (const settings *s);		/* show these choices in the windows (a preview: nothing is kept) */
  void (*saved) (const settings *s);		/* Save: the application keeps them, writes the choices and shows them */
} appearance_callbacks;

void appearance_init (const appearance_callbacks *cb);	/* creates the window (not shown) */
void appearance_show (const settings *s);		/* shows it, editing a copy of s (an open window comes to the front and keeps what was typed) */

#endif
