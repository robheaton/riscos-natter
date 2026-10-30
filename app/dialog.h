/* dialog.h - what the dialogue boxes of Natter share: the flags of the icons (known to work, see the dialogue test of DeskLib64), and a window and an icon made in code. */
#ifndef NATTER_DIALOG_H
#define NATTER_DIALOG_H

#include <stddef.h>
#include "DeskLib:Core.h"
#include "DeskLib:Wimp.h"
#include "DeskLib:WimpSWIs.h"
#include "DeskLib:Window.h"
#include "DeskLib:Icon.h"

#define F_LABEL (0x07000111u)					/* text, vertically centred, indirected */
#define F_FIELD (0x0700F135u)					/* a writable field: text, border, filled, vcentre, indirected, button type 15 */
#define F_OPTION (0x0700B113u)					/* an option button: type 11, with the sprites of the validation string */
#define F_RADIO (0x0700B113u | (1u << 16))			/* a radio button: the same, in exclusive selection group 1 (the icons of a group switch each other off) */
#define F_BUTTON (0x07000119u | (1u << 2) | (1u << 5) | (3u << 12))	/* text, centred, indirected, border, filled, click */
#define F_POPUP (0x07000115u | (1u << 5) | (3u << 12))		/* the same, text on the left: it shows the network that is picked */

/* An icon with indirected text in window w (buf has blen bytes; val is the validation string or NULL). */
icon_handle dlg_icon (window_handle w, int x0, int y0, int x1, int y1, unsigned flags, char *buf, size_t blen, char *val);
/* A dialogue window with a title bar, a close icon, and a work area of w x h units (not shown). */
window_handle dlg_window (int x, int y, int w, int h, const char *title);

#endif
