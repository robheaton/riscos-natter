/* settings.h - the choices of Natter: a plain text file with some global lines and one [section] for each network.  No Wimp calls: the application gives the file name.

     # a comment
     nick = alice
     log = on
     [Libera.Chat]
     host = irc.libera.chat
     port = 6697
     tls = yes
     sasl_user = alice
     sasl_pass = secret
     autojoin = #riscos64,#riscosdev
     connect = no

   The password is in the file as it is typed (as in most IRC clients): keep the file private. */
#ifndef NATTER_SETTINGS_H
#define NATTER_SETTINGS_H

#define MAX_NETWORKS 16

typedef struct
{
  char name[48];			/* the name of the section; shown in the buffer list */
  char host[128];
  int port;				/* 0: 6697 with TLS, 6667 without */
  int tls;
  char nick[48], user[48], realname[96];	/* empty: the global ones */
  char sasl_user[64], sasl_pass[128];	/* a login with SASL PLAIN when both are set */
  char password[128];			/* the password of the server (PASS) */
  char autojoin[256];			/* channels, separated by commas */
  int autoconnect;			/* connect when the application starts */
  int log;				/* -1: as the global setting, 0: no, 1: yes */
} netsettings;

typedef struct
{
  char nick[48], user[48], realname[96];	/* for networks that have none of their own */
  int log;				/* keep logs of the conversations (default yes) */
  char logdir[200];			/* where the logs go; empty: the default */
  netsettings nets[MAX_NETWORKS];
  int nnets;
} settings;

void settings_defaults (settings *s);
/* Reads the file into s (which has its defaults first).  Returns 0, or -1 if the file cannot be opened (s keeps the defaults). */
int settings_load (settings *s, const char *path);
int settings_save (const settings *s, const char *path);	/* 0, or -1 if the file cannot be written */
int settings_find (const settings *s, const char *name);	/* the index of a network by name (case does not matter), or -1 */
netsettings *settings_add (settings *s, const char *name);	/* a new network, with defaults (the existing one if the name is known); NULL if there is no room */
void settings_remove (settings *s, int index);
int net_port (const netsettings *n);				/* the port to use */

#endif
