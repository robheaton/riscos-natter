/* appearance.c - see appearance.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeskLib:Event.h"
#include "DeskLib:Handler.h"
#include "appearance.h"
#include "dialog.h"
#include "theme.h"

#define W 900
#define ROW 56
#define LBL_X 24
#define LBL_W 250
#define FLD_X 284
#define FLD_R (W - 30)
#define BUTTON_ROW 9				/* the buttons are the last row: the window ends just below them */

static const appearance_callbacks *cb;
static window_handle win;
static settings ed, start;			/* the copy that is being edited, and what it was when the window was opened */

static char t_size[4], t_col[NCOLOURS][8];
static char l_size[] = "Text size", l_points[] = "points (8 to 16)", l_theme[] = "Theme", l_light[] = "Light", l_dark[] = "Dark";
static char l_cancel[] = "Cancel", l_apply[] = "Apply", l_save[] = "Save";
static char l_hint1[] = "A colour is six hex digits: red, green, blue (1E1E1E is dark grey).";
static char l_hint2[] = "Choosing a theme puts back its colours.";
static char v_num[] = "A0-9", v_hex[] = "A0-9A-Fa-f", v_radio[] = "Sradiooff,radioon";
static const char *const col_names[NCOLOURS] = { "Background", "Text", "Faded text", "Mentions", "Lists" };
static const char *const col_notes[NCOLOURS] = { "behind the conversation", "what people say", "times, joins and parts", "lines that name you", "buffer list and nick list" };
static char *col_note_text[NCOLOURS];

static icon_handle i_size, i_light, i_dark, i_col[NCOLOURS], i_cancel, i_apply, i_save;
static icon_handle order[1 + NCOLOURS];			/* the writable fields in the order of the Tab key */
static int norder;

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
}

static BOOL click_handler (event_pollblock *event, void *reference)
{
  icon_handle i = event->data.mouse.icon;
  (void) reference;
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
  else if (i == i_save) { read_fields (); if (cb->saved) cb->saved (&ed); Window_Hide (win); }
  else if (i == i_cancel) { if (cb->apply) cb->apply (&start); Window_Hide (win); }
  return TRUE;
}

static BOOL key_handler (event_pollblock *event, void *reference)
{
  int code = event->data.key.code, k;
  icon_handle here = event->data.key.caret.icon;
  (void) reference;
  if (code == 13) { read_fields (); if (cb->saved) cb->saved (&ed); Window_Hide (win); return TRUE; }
  if (code == 27) { if (cb->apply) cb->apply (&start); Window_Hide (win); return TRUE; }
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
  if (cb->apply) cb->apply (&start);
  Window_Hide (win);
  return TRUE;
}

void appearance_init (const appearance_callbacks *callbacks)
{
  int r, i;
  cb = callbacks;
  win = dlg_window (260, 160, W, 24 + BUTTON_ROW * ROW + 52 + 20, "Appearance");
  r = 0;
  label (r, l_size); i_size = field (r, FLD_X, FLD_X + 110, t_size, sizeof t_size, v_num);
  mk (FLD_X + 130, r, FLD_R, F_LABEL, l_points, sizeof l_points, NULL); r++;
  label (r, l_theme);
  i_light = mk (FLD_X, r, FLD_X + 200, F_RADIO, l_light, sizeof l_light, v_radio);
  i_dark = mk (FLD_X + 220, r, FLD_X + 420, F_RADIO, l_dark, sizeof l_dark, v_radio); r++;
  for (i = 0; i < NCOLOURS; i++)
    {
      static char names[NCOLOURS][16];
      copy (names[i], sizeof names[i], col_names[i]);
      label (r, names[i]);
      i_col[i] = field (r, FLD_X, FLD_X + 200, t_col[i], sizeof t_col[i], v_hex);
      col_note_text[i] = malloc (strlen (col_notes[i]) + 1);
      if (col_note_text[i]) { strcpy (col_note_text[i], col_notes[i]); mk (FLD_X + 220, r, FLD_R, F_LABEL, col_note_text[i], strlen (col_notes[i]) + 1, NULL); }
      r++;
    }
  mk (LBL_X, r, FLD_R, F_LABEL, l_hint1, sizeof l_hint1, NULL); r++;
  mk (LBL_X, r, FLD_R, F_LABEL, l_hint2, sizeof l_hint2, NULL); r++;
  i_cancel = dlg_icon (win, LBL_X, rowtop (r) - 52, LBL_X + 200, rowtop (r) - 4, F_BUTTON, l_cancel, sizeof l_cancel, NULL);
  i_apply = dlg_icon (win, FLD_R - 440, rowtop (r) - 52, FLD_R - 230, rowtop (r) - 4, F_BUTTON, l_apply, sizeof l_apply, NULL);
  i_save = dlg_icon (win, FLD_R - 210, rowtop (r) - 52, FLD_R, rowtop (r) - 4, F_BUTTON, l_save, sizeof l_save, NULL);
  Event_Claim (event_CLICK, win, event_ANY, click_handler, NULL);
  Event_Claim (event_KEY, win, event_ANY, key_handler, NULL);
  Event_Claim (event_CLOSE, win, event_ANY, close_handler, NULL);
  Event_Claim (event_OPEN, win, event_ANY, Handler_OpenWindow, NULL);
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
