/* choices.h - the Choices window of Natter: a dialogue box (built in code, no template file) for the settings of settings.h: the names of the user, logging, and the networks (a popup menu
   to pick one, New and Delete, and the fields of the one that is picked).  The window edits a copy: Save keeps it, Cancel drops it, Connect keeps it and connects to the network that is shown. */
#ifndef NATTER_CHOICES_H
#define NATTER_CHOICES_H

#include "settings.h"

typedef struct
{
  void (*saved) (const settings *s);			/* Save (or Connect) was clicked: s is the new set of choices; the application keeps and stores it */
  void (*connect) (int index);				/* Connect was clicked, after saved: connect to network number index of the choices */
  void (*open_logs) (void);				/* the button next to the log folder */
} choices_callbacks;

void choices_init (const choices_callbacks *cb);		/* creates the window (not shown) */
/* Shows the window, editing a copy of s, with network number select picked (-1: the first).  If it is open already it comes to the front and keeps what the user has typed. */
void choices_show (const settings *s, int select);
int choices_handle_menu (const int *selection);		/* a menu choice: 1 if it was the popup of this window (the application's menu handler asks first) */

#endif
