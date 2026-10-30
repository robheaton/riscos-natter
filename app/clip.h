/* clip.h - the clipboard of Natter: our own copy of the text, and the Wimp's clipboard messages (ClaimEntity, DataRequest, DataSave, DataSaveAck, DataLoad, DataLoadAck),
   so that other programs can paste what we copied and we can paste what they have. */
#ifndef NATTER_CLIP_H
#define NATTER_CLIP_H

#include "DeskLib:Wimp.h"

typedef void (*clip_paste_fn) (const char *text, void *ud);

void clip_init (void);						/* claims the messages; call once the application has started */
void clip_set (const char *text);				/* copies the text and claims the global clipboard */
int clip_owned (void);						/* the clipboard is ours */
/* Pastes into window w, icon i: at once if the clipboard is ours (fn is called before this returns); otherwise a request goes to the other programs and fn is called when the text arrives. */
void clip_paste (window_handle w, icon_handle i, clip_paste_fn fn, void *ud);

#endif
