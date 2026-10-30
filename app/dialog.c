/* dialog.c - see dialog.h. */
#include <string.h>
#include "dialog.h"

icon_handle dlg_icon (window_handle w, int x0, int y0, int x1, int y1, unsigned flags, char *buf, size_t blen, char *val)
{
  icon_createblock ic;
  icon_handle h = -1;
  memset (&ic, 0, sizeof ic);
  ic.window = w;
  ic.icondata.workarearect.min.x = x0; ic.icondata.workarearect.min.y = y0; ic.icondata.workarearect.max.x = x1; ic.icondata.workarearect.max.y = y1;
  ic.icondata.flags.value = flags;
  ic.icondata.data.indirecttext.buffer = DL_PTR (buf);
  ic.icondata.data.indirecttext.validstring = val ? DL_PTR (val) : (dl_ptr) -1;
  ic.icondata.data.indirecttext.bufflen = (int) blen;
  Wimp_CreateIcon (&ic, &h);
  return h;
}

window_handle dlg_window (int x, int y, int w, int h, const char *title, int own_redraw)
{
  window_block wb;
  window_handle win = 0;
  memset (&wb, 0, sizeof wb);
  wb.screenrect.min.x = x; wb.screenrect.min.y = y; wb.screenrect.max.x = x + w; wb.screenrect.max.y = y + h;
  wb.behind = -1;
  wb.flags.value = own_redraw ? 0x87000002u : 0x87000012u;			/* back, close and title; moveable; 0x10: the Wimp redraws it (it only has icons) */
  wb.colours.cols.titlefore = 7; wb.colours.cols.titleback = 2; wb.colours.cols.workfore = 7; wb.colours.cols.workback = 1;
  wb.colours.cols.scrollouter = 3; wb.colours.cols.scrollinner = 1; wb.colours.cols.titlefocus = 12;
  wb.workarearect.min.x = 0; wb.workarearect.max.x = w; wb.workarearect.max.y = 0; wb.workarearect.min.y = -h;
  wb.titleflags.value = 0x19; wb.workflags.value = 0;
  wb.spritearea = 1;
  wb.minsize.x = 100; wb.minsize.y = 100;
  strncpy (wb.title.text, title, 11);
  Wimp_CreateWindow (&wb, &win);
  return win;
}
