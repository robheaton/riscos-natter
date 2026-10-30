/* appearance.c - see appearance.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeskLib:ColourTran.h"
#include "DeskLib:Event.h"
#include "DeskLib:EventMsg.h"
#include "DeskLib:GFX.h"
#include "DeskLib:Handler.h"
#include "DeskLib:SWI.h"
#include "appearance.h"
#include "dialog.h"
#include "theme.h"

extern void natter_log (const char *fmt, ...);

#define W 960
#define ROW 56
#define LBL_X 24
#define LBL_W 250
#define FLD_X 284
#define FLD_R (W - 30)
#define SW_W 90					/* the swatch: a button that shows the colour and opens the colour picker */
#define HEX_X (FLD_X + SW_W + 16)		/* the six digits of the colour, to type */
#define HEX_W 150
#define NOTE_X (HEX_X + HEX_W + 20)
#define BUTTON_ROW 9				/* the buttons are the last row: the window ends just below them */
#define FIRST_COLOUR_ROW 2

/* the Colour Picker module (the standard way to choose a colour on RISC OS) */
#define SWI_CP_OPEN 0x47702
#define SWI_CP_CLOSE 0x47703
#define MSG_CP_CHOICE 0x47700			/* the user clicked OK */
#define MSG_CP_CHANGED 0x47701			/* the colour changed while a slider was dragged */
#define MSG_CP_CLOSE 0x47702			/* the user clicked Cancel: the application closes the dialogue */
#define CP_TYPE_CLICKDRAG (2u << 2)		/* flags of the dialogue: tell us while the colour is dragged */

typedef struct				/* ColourPicker_Dialogue: all 32 bits, as the module has them */
{
  unsigned flags;
  unsigned title;
  int x0, y0, x1, y1;			/* the visible area of the window that it opens */
  int xscroll, yscroll;
  unsigned colour;			/* 0xBBGGRR00 */
  int size;				/* the size of the colour block: the colour and this word */
} cp_dialogue;

static const appearance_callbacks *cb;
static window_handle win;
static settings ed, start;			/* the copy that is being edited, and what it was when the window was opened */

static char t_size[4], t_col[NCOLOURS][8], t_sw[NCOLOURS][2];
static char l_size[] = "Text size", l_points[] = "points (8 to 16)", l_theme[] = "Theme", l_light[] = "Light", l_dark[] = "Dark";
static char l_cancel[] = "Cancel", l_apply[] = "Apply", l_save[] = "Save";
static char l_hint1[] = "Click a colour to choose it, or type six hex digits (red, green, blue).";
static char l_hint2[] = "Choosing a theme puts back its colours.";
static char v_num[] = "A0-9", v_hex[] = "A0-9A-Fa-f", v_radio[] = "Sradiooff,radioon";
static const char *const col_names[NCOLOURS] = { "Background", "Text", "Faded text", "Mentions", "Lists" };
static const char *const col_notes[NCOLOURS] = { "behind the conversation", "what people say", "times, joins and parts", "lines that name you", "buffer list and nick list" };

static icon_handle i_size, i_light, i_dark, i_col[NCOLOURS], i_sw[NCOLOURS], i_cancel, i_apply, i_save;
static icon_handle order[1 + NCOLOURS];			/* the writable fields in the order of the Tab key */
static int norder;
static unsigned swatch[NCOLOURS];			/* the colour that each swatch shows (0xBBGGRR00) */

static cp_dialogue cp_block;				/* the colour picker that is open: its handle (0: none), the colour that it is for, and its title */
static int cp_d, cp_slot = -1;
static char cp_title[NCOLOURS][24];

static int rowtop (int r) { return -(24 + r * ROW); }
static icon_handle mk (int x0, int r, int x1, unsigned flags, char *buf, size_t blen, char *val) { return dlg_icon (win, x0, rowtop (r) - 44, x1, rowtop (r) - 4, flags, buf, blen, val); }
static void label (int r, char *text) { mk (LBL_X, r, LBL_X + LBL_W, F_LABEL, text, strlen (text) + 1, NULL); }
static icon_handle field (int r, int x0, int x1, char *buf, size_t n, char *val)
{
  icon_handle h = mk (x0, r, x1, F_FIELD, buf, n, val);
  if (norder < 1 + NCOLOURS) order[norder++] = h;
  return h;
}

static void copy (char *dst, size_t size, const char *src) { snprintf (dst, size, "%s", src); }

/* ---- the colours of the swatches ---- */

/* the colour of each slot as the window has it: the digits that are typed if they are a colour, otherwise what ed has */
static void update_swatches (void)
{
  palette p;
  unsigned cols[NCOLOURS];
  int i;
  palette_make (&ed, &p);
  cols[COLOUR_BACK] = p.back; cols[COLOUR_TEXT] = p.text; cols[COLOUR_FADED] = p.faded; cols[COLOUR_MENTION] = p.mention; cols[COLOUR_LISTS] = p.lists;
  for (i = 0; i < NCOLOURS; i++)
    {
      unsigned rgb, was = swatch[i];
      swatch[i] = colour_parse (Icon_GetTextPtr (win, i_col[i]), &rgb) ? colour_bgr (rgb) : cols[i];
      if (swatch[i] != was) Icon_ForceRedraw (win, i_sw[i]);
    }
}

/* the window shows what ed has: the size, the theme and the colours that are in effect (the theme's, or the user's over them) */
static void load_fields (void)
{
  palette p;
  char hex[8], size[8];
  unsigned cols[NCOLOURS];
  int i;
  palette_make (&ed, &p);
  cols[COLOUR_BACK] = p.back; cols[COLOUR_TEXT] = p.text; cols[COLOUR_FADED] = p.faded; cols[COLOUR_MENTION] = p.mention; cols[COLOUR_LISTS] = p.lists;
  snprintf (size, sizeof size, "%d", ed.font_size);
  Icon_SetText (win, i_size, size);
  for (i = 0; i < NCOLOURS; i++) { colour_hex (cols[i], hex); Icon_SetText (win, i_col[i], hex); }
  if (ed.theme == THEME_DARK) { Icon_Select (win, i_dark); Icon_Deselect (win, i_light); } else { Icon_Select (win, i_light); Icon_Deselect (win, i_dark); }
  update_swatches ();
}

/* ed gets what is in the window: a colour that is the theme's own is not kept as the user's, and one that is not a colour is dropped */
static void read_fields (void)
{
  settings plain = ed;
  palette base;
  unsigned defaults[NCOLOURS];
  int i, pt = atoi (Icon_GetTextPtr (win, i_size));
  ed.font_size = pt > 0 ? font_size_clamp (pt) : FONT_DEFAULT;
  ed.theme = Icon_GetSelect (win, i_dark) ? THEME_DARK : THEME_LIGHT;
  memset (plain.colours, 0, sizeof plain.colours); plain.theme = ed.theme;
  palette_make (&plain, &base);
  defaults[COLOUR_BACK] = base.back; defaults[COLOUR_TEXT] = base.text; defaults[COLOUR_FADED] = base.faded; defaults[COLOUR_MENTION] = base.mention; defaults[COLOUR_LISTS] = base.lists;
  for (i = 0; i < NCOLOURS; i++)
    {
      unsigned rgb;
      const char *t = Icon_GetTextPtr (win, i_col[i]);
      if (colour_parse (t, &rgb) && colour_bgr (rgb) != defaults[i]) { char hex[8]; colour_hex (colour_bgr (rgb), hex); copy (ed.colours[i], sizeof ed.colours[i], hex); }
      else ed.colours[i][0] = 0;
    }
  update_swatches ();
}

/* ---- the colour picker ---- */

static void close_picker (void)
{
  if (cp_d) SWI (2, 0, SWI_CP_CLOSE | XOS_Bit, 0, cp_d);
  cp_d = 0; cp_slot = -1;
}

static void open_picker (int slot)
{
  window_state st;
  os_error *e;
  int d = 0, w = 0;
  close_picker ();
  Wimp_GetWindowState (win, &st);
  memset (&cp_block, 0, sizeof cp_block);
  cp_block.flags = CP_TYPE_CLICKDRAG;
  snprintf (cp_title[slot], sizeof cp_title[slot], "%s colour", col_names[slot]);
  cp_block.title = DL_PTR (cp_title[slot]);
  cp_block.x0 = st.openblock.screenrect.max.x + 16; cp_block.y1 = st.openblock.screenrect.max.y;
  cp_block.x1 = cp_block.x0 + 580; cp_block.y0 = cp_block.y1 - 460;
  cp_block.colour = swatch[slot];
  cp_block.size = 8;
  e = SWI (2, 2, SWI_CP_OPEN | XOS_Bit, 0, DL_PTR (&cp_block), &d, &w);
  natter_log ("colour picker open: %s d=%d w=%d", e ? e->errmess : "ok", d, w);
  if (e) { cp_block.size = 0; d = w = 0; e = SWI (2, 2, SWI_CP_OPEN | XOS_Bit, 0, DL_PTR (&cp_block), &d, &w); natter_log ("colour picker open (size 0): %s d=%d w=%d", e ? e->errmess : "ok", d, w); }
  if (e) return;
  cp_d = d; cp_slot = slot;
  if (w)
    {
      window_state ps;
      Wimp_GetWindowState (w, &ps);
      natter_log ("colour picker window %d at %d,%d to %d,%d", w, ps.openblock.screenrect.min.x, ps.openblock.screenrect.min.y, ps.openblock.screenrect.max.x, ps.openblock.screenrect.max.y);
    }
}

/* the colour picker says a colour: it goes into the field and the swatch, and the windows show it */
static void picked (unsigned colour)
{
  char hex[8];
  if (cp_slot < 0) return;
  colour_hex (colour, hex);
  Icon_SetText (win, i_col[cp_slot], hex);
  read_fields ();
  if (cb->apply) cb->apply (&ed);
}

static BOOL on_choice (event_pollblock *event, void *reference)
{
  const int *w = event->data.message.data.words;
  (void) reference;
  natter_log ("colour picker choice: d=%d flags=%x colour=%08x", w[0], (unsigned) w[1], (unsigned) w[2]);
  if (!cp_d || w[0] != cp_d) return FALSE;
  picked ((unsigned) w[2]);
  close_picker ();
  return TRUE;
}

static BOOL on_changed (event_pollblock *event, void *reference)
{
  const int *w = event->data.message.data.words;
  (void) reference;
  if (!cp_d || w[0] != cp_d) return FALSE;
  picked ((unsigned) w[2]);
  return TRUE;
}

static BOOL on_close_request (event_pollblock *event, void *reference)
{
  const int *w = event->data.message.data.words;
  (void) reference;
  natter_log ("colour picker close request: d=%d", w[0]);
  if (!cp_d || w[0] != cp_d) return FALSE;
  close_picker ();
  return TRUE;
}

/* ---- the window ---- */

static BOOL redraw_handler (event_pollblock *event, void *reference)
{
  window_redrawblock rb;
  BOOL more;
  (void) reference;
  rb.window = event->data.openblock.window;
  Wimp_RedrawWindow (&rb, &more);
  while (more)
    {
      int ox = rb.rect.min.x - rb.scroll.x, oy = rb.rect.max.y - rb.scroll.y, k;
      for (k = 0; k < NCOLOURS; k++)						/* the colour inside the border of each swatch button */
	{
	  ColourTrans_SetGCOL (swatch[k], 0, 0);
	  GFX_RectangleFill (ox + FLD_X + 6, oy + rowtop (FIRST_COLOUR_ROW + k) - 44 + 6, SW_W - 12, 28);
	}
      Wimp_GetRectangle (&rb, &more);
    }
  return TRUE;
}

static void hide (int keep)
{
  close_picker ();
  if (!keep && cb->apply) cb->apply (&start);
  Window_Hide (win);
}

static BOOL click_handler (event_pollblock *event, void *reference)
{
  icon_handle i = event->data.mouse.icon;
  int k;
  (void) reference;
  for (k = 0; k < NCOLOURS; k++) if (i == i_sw[k]) { open_picker (k); return TRUE; }
  if (i == i_light || i == i_dark)
    {
      int pt = atoi (Icon_GetTextPtr (win, i_size));
      ed.font_size = pt > 0 ? font_size_clamp (pt) : FONT_DEFAULT;
      ed.theme = i == i_dark ? THEME_DARK : THEME_LIGHT;
      memset (ed.colours, 0, sizeof ed.colours);			/* the theme's own colours */
      load_fields ();
      if (cb->apply) cb->apply (&ed);
    }
  else if (i == i_apply) { read_fields (); load_fields (); if (cb->apply) cb->apply (&ed); }
  else if (i == i_save) { read_fields (); if (cb->saved) cb->saved (&ed); hide (1); }
  else if (i == i_cancel) hide (0);
  return TRUE;
}

static BOOL key_handler (event_pollblock *event, void *reference)
{
  int code = event->data.key.code, k;
  icon_handle here = event->data.key.caret.icon;
  (void) reference;
  if (code == 13) { read_fields (); if (cb->saved) cb->saved (&ed); hide (1); return TRUE; }
  if (code == 27) { hide (0); return TRUE; }
  if (code == 9 || code == 0x18E || code == 0x18F)					/* Tab and the arrows go from field to field */
    {
      for (k = 0; k < norder; k++) if (order[k] == here) break;
      k = (code == 0x18F) ? (k <= 0 ? norder - 1 : k - 1) : (k + 1 >= norder ? 0 : k + 1);
      Icon_SetCaret (win, order[k]);
      return TRUE;
    }
  Wimp_ProcessKey (code);
  return TRUE;
}

static BOOL close_handler (event_pollblock *event, void *reference)
{
  (void) event; (void) reference;
  hide (0);
  return TRUE;
}

void appearance_init (const appearance_callbacks *callbacks)
{
  int r, i;
  cb = callbacks;
  win = dlg_window (260, 160, W, 24 + BUTTON_ROW * ROW + 52 + 20, "Appearance", 1);
  r = 0;
  label (r, l_size); i_size = field (r, FLD_X, FLD_X + 110, t_size, sizeof t_size, v_num);
  mk (FLD_X + 130, r, FLD_R, F_LABEL, l_points, sizeof l_points, NULL); r++;
  label (r, l_theme);
  i_light = mk (FLD_X, r, FLD_X + 200, F_RADIO, l_light, sizeof l_light, v_radio);
  i_dark = mk (FLD_X + 220, r, FLD_X + 420, F_RADIO, l_dark, sizeof l_dark, v_radio); r++;
  for (i = 0; i < NCOLOURS; i++)
    {
      static char names[NCOLOURS][16], notes[NCOLOURS][32];
      copy (names[i], sizeof names[i], col_names[i]);
      copy (notes[i], sizeof notes[i], col_notes[i]);
      label (r, names[i]);
      t_sw[i][0] = 0;
      i_sw[i] = mk (FLD_X, r, FLD_X + SW_W, F_BUTTON, t_sw[i], sizeof t_sw[i], NULL);
      i_col[i] = field (r, HEX_X, HEX_X + HEX_W, t_col[i], sizeof t_col[i], v_hex);
      mk (NOTE_X, r, FLD_R, F_LABEL, notes[i], strlen (notes[i]) + 1, NULL);
      r++;
    }
  mk (LBL_X, r, FLD_R, F_LABEL, l_hint1, sizeof l_hint1, NULL); r++;
  mk (LBL_X, r, FLD_R, F_LABEL, l_hint2, sizeof l_hint2, NULL); r++;
  i_cancel = dlg_icon (win, LBL_X, rowtop (r) - 52, LBL_X + 200, rowtop (r) - 4, F_BUTTON, l_cancel, sizeof l_cancel, NULL);
  i_apply = dlg_icon (win, FLD_R - 440, rowtop (r) - 52, FLD_R - 230, rowtop (r) - 4, F_BUTTON, l_apply, sizeof l_apply, NULL);
  i_save = dlg_icon (win, FLD_R - 210, rowtop (r) - 52, FLD_R, rowtop (r) - 4, F_BUTTON, l_save, sizeof l_save, NULL);
  Event_Claim (event_REDRAW, win, event_ANY, redraw_handler, NULL);
  Event_Claim (event_CLICK, win, event_ANY, click_handler, NULL);
  Event_Claim (event_KEY, win, event_ANY, key_handler, NULL);
  Event_Claim (event_CLOSE, win, event_ANY, close_handler, NULL);
  Event_Claim (event_OPEN, win, event_ANY, Handler_OpenWindow, NULL);
  EventMsg_Claim (MSG_CP_CHOICE, event_ANY, on_choice, NULL);
  EventMsg_Claim (MSG_CP_CHANGED, event_ANY, on_changed, NULL);
  EventMsg_Claim (MSG_CP_CLOSE, event_ANY, on_close_request, NULL);
}

void appearance_show (const settings *s)
{
  window_state st;
  Wimp_GetWindowState (win, &st);
  if (!st.flags.data.open)							/* a fresh start from the choices; an open window keeps what has been typed */
    {
      ed = *s; start = *s;
      ed.font_size = font_size_clamp (ed.font_size);
      load_fields ();
    }
  Window_Show (win, open_CENTERED);
  Icon_SetCaret (win, i_size);
}
