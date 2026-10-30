/* logfile.h - the logs of Natter: plain text, one file for each channel (and one for each query, and one for the status of a network), a folder for each network.

     <logdir>/Libera_Chat/riscos64         the channel #riscos64
     <logdir>/Libera_Chat/Queries/bob      the conversation with bob
     <logdir>/Libera_Chat/!Status          what the server and Natter said that belongs to no conversation

   Each line starts with the date and time:  2026-10-29 12:04:11 <carol> hello,  * dave waves,  --> bob has joined,  <-- erin has quit,  -- something happened.  UTF-8, one line each.
   The names of channels and people are made into names that a file can have on RISC OS (a "#" is a wildcard and a "." a separator there) and anywhere else.  Lines wait in memory until
   logger_flush (call it every few seconds and when the program ends): the files are opened to add the lines and closed again, so that no file is kept open (a file system may allow few). */
#ifndef NATTER_LOGFILE_H
#define NATTER_LOGFILE_H

#include <stddef.h>

typedef struct logger logger;

/* dir: the folder of the logs; sep: the separator of names in a path ("/" or "." on RISC OS) */
logger *logger_new (const char *dir, const char *sep);
void logger_free (logger *l);					/* flushes */
/* One line (without a line break) for the conversation bufname of the network.  bufkind is BK_STATUS, BK_CHANNEL or BK_QUERY of buffer.h. */
void logger_add (logger *l, const char *network, int bufkind, const char *bufname, const char *line);
void logger_flush (logger *l);
int logger_errors (const logger *l);				/* the number of times that a file could not be written */
/* the path of the file for a conversation, in buf (for the user to see where it is) */
void logger_path (const logger *l, const char *network, int bufkind, const char *bufname, char *buf, size_t size);

/* the name of a channel, person or network as a file name: letters, digits and a few signs, at most 60 bytes, never empty */
void log_safe_name (const char *name, char *out, size_t size);
/* the text of a log line: the time as local time (tz_seconds east of UTC), then by kind (LK_* of buffer.h) */
size_t log_format (char *out, size_t size, long when, long tz_seconds, int linekind, const char *nick, const char *text);

#endif
