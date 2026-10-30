/* clip.c - see clip.h.  The messages are the ones of the PRM (the data transfer protocol): the file that carries the text is a scrap file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeskLib:Event.h"
#include "DeskLib:EventMsg.h"
#include "DeskLib:File.h"
#include "DeskLib:WimpSWIs.h"
#include "clip.h"

#define FILETYPE_TEXT 0xFFF
#define CLIPBOARD_FLAG 4		/* in the flags of ClaimEntity and DataRequest */
#define NAME_AT 24			/* where the leaf or file name starts in the data of DataSave, DataSaveAck and DataLoad: after the window, icon, position, size and file type */
#define MAX_PASTE 65536
#define SCRAP "<Wimp$Scrap>"

static char *own;			/* the text that we copied, or NULL when somebody else has the clipboard */
static unsigned save_ref, load_ref;	/* the references of our DataSave and DataLoad, for the answers */
static clip_paste_fn want_fn;		/* a paste that has been asked for */
static void *want_ud;
static int pasting;
static unsigned want_ref;

static unsigned round4 (unsigned n) { return (n + 3u) & ~3u; }

static void send (event_type kind, message_block *m, task_handle to) { Wimp_SendMessage (kind, m, to, 0); }

/* a message with the common part: yourref is the reference of the message that it answers (0 for a new one) */
static void start_message (message_block *m, message_action action, unsigned yourref)
{
  memset (m, 0, sizeof *m);
  m->header.action = action;
  m->header.yourref = yourref;
}

void clip_set (const char *text)
{
  message_block m;
  free (own);
  own = malloc (strlen (text) + 1);
  if (!own) return;
  strcpy (own, text);
  start_message (&m, message_CLAIMENTITY, 0);
  m.header.size = 20 + 4;
  m.data.words[0] = CLIPBOARD_FLAG;
  send (event_USERMESSAGE, &m, 0);
}

int clip_owned (void) { return own != NULL; }

void clip_paste (window_handle w, icon_handle i, clip_paste_fn fn, void *ud)
{
  message_block m;
  if (own) { fn (own, ud); return; }
  start_message (&m, message_DATAREQUEST, 0);
  m.header.size = 20 + 4 * 7;
  m.data.words[0] = (int) w; m.data.words[1] = (int) i; m.data.words[2] = 0; m.data.words[3] = 0;
  m.data.words[4] = CLIPBOARD_FLAG;
  m.data.words[5] = FILETYPE_TEXT; m.data.words[6] = -1;
  pasting = 1; want_fn = fn; want_ud = ud;
  Wimp_SendMessage (event_USERMESSAGERECORDED, &m, 0, 0);
  want_ref = m.header.myref;
}

/* ---- being the owner ---- */

static BOOL on_claim (event_pollblock *event, void *reference)		/* somebody else has taken the clipboard */
{
  (void) reference;
  if (event->data.message.data.words[0] & CLIPBOARD_FLAG) { free (own); own = NULL; }
  return TRUE;
}

static BOOL on_request (event_pollblock *event, void *reference)
{
  message_block *q = &event->data.message, m;
  int k, text_ok = 0;
  (void) reference;
  if (!own || !(q->data.words[4] & CLIPBOARD_FLAG)) return TRUE;
  for (k = 5; k < 59 && q->data.words[k] != -1; k++) if (q->data.words[k] == FILETYPE_TEXT) text_ok = 1;
  if (!text_ok) return TRUE;
  start_message (&m, message_DATASAVE, q->header.myref);
  memcpy (m.data.words, q->data.words, 4 * sizeof (int));			/* the window, icon and position of the request */
  m.data.words[4] = (int) strlen (own); m.data.words[5] = FILETYPE_TEXT;
  strcpy (m.data.bytes + NAME_AT, "Selection");
  m.header.size = round4 (20 + NAME_AT + 10);
  send (event_USERMESSAGERECORDED, &m, q->header.sender);
  save_ref = m.header.myref;
  return TRUE;
}

static BOOL on_save_ack (event_pollblock *event, void *reference)		/* the other program says where to put the text */
{
  message_block *q = &event->data.message, m;
  const char *name = q->data.bytes + NAME_AT;
  FILE *f;
  (void) reference;
  if (!own || q->header.yourref != save_ref) return TRUE;
  f = fopen (name, "wb");
  if (!f) return TRUE;
  fwrite (own, 1, strlen (own), f);
  fclose (f);
  File_SetType (name, FILETYPE_TEXT);
  start_message (&m, message_DATALOAD, q->header.myref);
  memcpy (m.data.words, q->data.words, 4 * sizeof (int));
  m.data.words[4] = (int) strlen (own); m.data.words[5] = FILETYPE_TEXT;
  strncpy (m.data.bytes + NAME_AT, name, 200); m.data.bytes[NAME_AT + 200] = 0;
  m.header.size = round4 ((unsigned) (20 + NAME_AT + strlen (m.data.bytes + NAME_AT) + 1));
  send (event_USERMESSAGERECORDED, &m, q->header.sender);
  load_ref = m.header.myref;
  return TRUE;
}

static BOOL on_load_ack (event_pollblock *event, void *reference)
{
  (void) event; (void) reference;
  return TRUE;									/* the text has arrived: nothing more to do */
}

/* ---- pasting ---- */

static BOOL on_save (event_pollblock *event, void *reference)			/* the owner offers the text: say where to put it */
{
  message_block *q = &event->data.message, m;
  (void) reference;
  if (!pasting || q->header.yourref != want_ref) return TRUE;
  start_message (&m, message_DATASAVEACK, q->header.myref);
  memcpy (m.data.words, q->data.words, 4 * sizeof (int));
  m.data.words[4] = -1; m.data.words[5] = q->data.words[5];			/* the size is -1: the file is not safe, only a scrap */
  strcpy (m.data.bytes + NAME_AT, SCRAP);
  m.header.size = round4 ((unsigned) (20 + NAME_AT + sizeof SCRAP));
  send (event_USERMESSAGE, &m, q->header.sender);
  return TRUE;
}

static BOOL on_load (event_pollblock *event, void *reference)			/* the text is in the file */
{
  message_block *q = &event->data.message, m;
  const char *name = q->data.bytes + NAME_AT;
  FILE *f;
  char *text;
  size_t n;
  (void) reference;
  if (!pasting) return FALSE;
  f = fopen (name, "rb");
  if (!f) return TRUE;
  text = malloc (MAX_PASTE + 1);
  if (!text) { fclose (f); return TRUE; }
  n = fread (text, 1, MAX_PASTE, f);
  fclose (f);
  text[n] = 0;
  start_message (&m, message_DATALOADACK, q->header.myref);
  m.header.size = q->header.size;
  memcpy (m.data.bytes, q->data.bytes, sizeof m.data.bytes);
  send (event_USERMESSAGE, &m, q->header.sender);
  remove (name);
  pasting = 0;
  if (want_fn) want_fn (text, want_ud);
  free (text);
  return TRUE;
}

void clip_init (void)
{
  EventMsg_Claim (message_CLAIMENTITY, event_ANY, on_claim, NULL);
  EventMsg_Claim (message_DATAREQUEST, event_ANY, on_request, NULL);
  EventMsg_Claim (message_DATASAVEACK, event_ANY, on_save_ack, NULL);
  EventMsg_Claim (message_DATALOADACK, event_ANY, on_load_ack, NULL);
  EventMsg_Claim (message_DATASAVE, event_ANY, on_save, NULL);
  EventMsg_Claim (message_DATALOAD, event_ANY, on_load, NULL);
}
