/* choices.c - see choices.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeskLib:Core.h"
#include "DeskLib:Event.h"
#include "DeskLib:Handler.h"
#include "DeskLib:Wimp.h"
#include "DeskLib:WimpSWIs.h"
#include "DeskLib:Window.h"
#include "DeskLib:Icon.h"
#include "DeskLib:Menu.h"
#include "DeskLib:Error.h"
#include <openssl/opensslv.h>
#include "choices.h"

/* the icons: flags that are known to work (see the dialogue test of DeskLib64) */
#define F_LABEL (0x07000111u)					/* text, vertically centred, indirected */
#define F_FIELD (0x0700F135u)					/* a writable field: text, border, filled, vcentre, indirected, button type 15 */
#define F_OPTION (0x0700B113u)					/* an option button: type 11, with the sprites of the validation string */
#define F_BUTTON (0x07000119u | (1u << 2) | (1u << 5) | (3u << 12))	/* text, centred, indirected, border, filled, click */
#define F_POPUP (0x07000115u | (1u << 5) | (3u << 12))		/* the same, text on the left: it shows the network that is picked */

#define W 940
#define ROW 56
#define LBL_X 24
#define LBL_W 250
#define FLD_X 284
#define FLD_R (W - 30)

static const choices_callbacks *cb;
static window_handle win;
static settings ed;						/* the copy that is being edited */
static int ed_net;
static int menu_open;
static menu_ptr netmenu;

/* the text of the icons */
static char t_nick[48], t_user[48], t_real[96], t_logdir[200], t_netname[48], t_host[128], t_port[8], t_netnick[48], t_sasl_user[64], t_sasl_pass[128], t_srv_pass[128], t_autojoin[256], t_pick[64];
static char l_you[] = "Your names", l_nick[] = "Nick", l_user[] = "User name", l_real[] = "Real name", l_logs[] = "Logs", l_logopt[] = "Keep logs of the conversations", l_logdir[] = "Log folder", l_open[] = "Open";
static char l_net[] = "Network", l_new[] = "New", l_del[] = "Delete", l_name[] = "Name", l_server[] = "Server", l_port[] = "Port", l_tls[] = "Secure connection (TLS)", l_netnick[] = "Nick here", l_sasl[] = "Login account";
static char l_pass[] = "Password", l_srvpass[] = "Server password", l_join[] = "Join at start", l_auto[] = "Connect when Natter starts", l_cancel[] = "Cancel", l_connect[] = "Connect", l_save[] = "Save";
static char l_hint[] = "Nick, names and logs are for all networks; a nick for one network is optional.";
static char v_pass[] = "D*", v_port[] = "A0-9", v_opt[] = "Soptoff,opton";

static icon_handle i_nick, i_user, i_real, i_log, i_logdir, i_open, i_pick, i_new, i_del, i_netname, i_host, i_port, i_tls, i_netnick, i_sasl_user, i_sasl_pass, i_srv_pass, i_autojoin, i_auto, i_cancel, i_connect, i_save;
static icon_handle order[12];					/* the writable fields in the order of the Tab key */
static int norder;

static icon_handle mk (int x0, int y0, int x1, int y1, unsigned flags, char *buf, size_t blen, char *val)
{
  icon_createblock ic;
  icon_handle h = -1;
  memset (&ic, 0, sizeof ic);
  ic.window = win;
  ic.icondata.workarearect.min.x = x0; ic.icondata.workarearect.min.y = y0; ic.icondata.workarearect.max.x = x1; ic.icondata.workarearect.max.y = y1;
  ic.icondata.flags.value = flags;
  ic.icondata.data.indirecttext.buffer = DL_PTR (buf);
  ic.icondata.data.indirecttext.validstring = val ? DL_PTR (val) : (dl_ptr) -1;
  ic.icondata.data.indirecttext.bufflen = (int) blen;
  Wimp_CreateIcon (&ic, &h);
  return h;
}

static int rowtop (int r) { return -(24 + r * ROW); }
static void label (int r, char *text) { mk (LBL_X, rowtop (r) - 44, LBL_X + LBL_W, rowtop (r) - 4, F_LABEL, text, strlen (text) + 1, NULL); }
static icon_handle field (int r, char *buf, size_t n, char *val, int right)
{
  icon_handle h = mk (FLD_X, rowtop (r) - 44, right, rowtop (r) - 4, F_FIELD, buf, n, val);
  if (norder < 12) order[norder++] = h;
  return h;
}

/* ---- between the icons and the copy ---- */

static void set_opt (icon_handle i, int on) { if (on) Icon_Select (win, i); else Icon_Deselect (win, i); }
static int get_opt (icon_handle i) { return Icon_GetSelect (win, i) ? 1 : 0; }

static void copy (char *dst, size_t size, const char *src) { snprintf (dst, size, "%s", src); }

static void load_global (void)
{
  Icon_SetText (win, i_nick, ed.nick); Icon_SetText (win, i_user, ed.user); Icon_SetText (win, i_real, ed.realname);
  set_opt (i_log, ed.log);
  Icon_SetText (win, i_logdir, ed.logdir);
}

static void store_global (void)
{
  copy (ed.nick, sizeof ed.nick, Icon_GetTextPtr (win, i_nick)); copy (ed.user, sizeof ed.user, Icon_GetTextPtr (win, i_user)); copy (ed.realname, sizeof ed.realname, Icon_GetTextPtr (win, i_real));
  ed.log = get_opt (i_log);
  copy (ed.logdir, sizeof ed.logdir, Icon_GetTextPtr (win, i_logdir));
}

static void load_net (void)
{
  const netsettings *n = ed_net >= 0 && ed_net < ed.nnets ? &ed.nets[ed_net] : NULL;
  char port[16];
  Icon_SetText (win, i_pick, n ? n->name : "");
  Icon_SetText (win, i_netname, n ? n->name : "");
  Icon_SetText (win, i_host, n ? n->host : "");
  snprintf (port, sizeof port, "%d", n && n->port ? n->port : 0);
  Icon_SetText (win, i_port, n && n->port ? port : "");
  set_opt (i_tls, n ? n->tls : 1);
  Icon_SetText (win, i_netnick, n ? n->nick : "");
  Icon_SetText (win, i_sasl_user, n ? n->sasl_user : ""); Icon_SetText (win, i_sasl_pass, n ? n->sasl_pass : "");
  Icon_SetText (win, i_srv_pass, n ? n->password : "");
  Icon_SetText (win, i_autojoin, n ? n->autojoin : "");
  set_opt (i_auto, n ? n->autoconnect : 0);
}

static void store_net (void)
{
  netsettings *n;
  char name[48];
  int k;
  if (ed_net < 0 || ed_net >= ed.nnets) return;
  n = &ed.nets[ed_net];
  copy (name, sizeof name, Icon_GetTextPtr (win, i_netname)[0] ? Icon_GetTextPtr (win, i_netname) : n->name);
  for (k = 0; k < ed.nnets; k++)								/* names are unique: "Libera" and "libera" are one name */
    if (k != ed_net && settings_find (&ed, name) == k) { size_t l = strlen (name); if (l + 2 < sizeof name) { name[l] = '2'; name[l + 1] = 0; } break; }
  copy (n->name, sizeof n->name, name);
  copy (n->host, sizeof n->host, Icon_GetTextPtr (win, i_host));
  { int port = atoi (Icon_GetTextPtr (win, i_port)); n->port = port > 0 && port < 65536 ? port : 0; }
  n->tls = get_opt (i_tls);
  copy (n->nick, sizeof n->nick, Icon_GetTextPtr (win, i_netnick));
  copy (n->sasl_user, sizeof n->sasl_user, Icon_GetTextPtr (win, i_sasl_user));
  copy (n->sasl_pass, sizeof n->sasl_pass, Icon_GetTextPtr (win, i_sasl_pass));
  copy (n->password, sizeof n->password, Icon_GetTextPtr (win, i_srv_pass));
  copy (n->autojoin, sizeof n->autojoin, Icon_GetTextPtr (win, i_autojoin));
  n->autoconnect = get_opt (i_auto);
}

/* ---- actions ---- */

static int valid (void)
{
  int i;
  for (i = 0; i < ed.nnets; i++)
    if (!ed.nets[i].host[0]) { Error_Report (0, "The network %s has no server: type the name of its server (for example irc.libera.chat).", ed.nets[i].name); ed_net = i; load_net (); return 0; }
  if (!ed.nick[0]) { Error_Report (0, "Type the nick that you want to use."); return 0; }
  return 1;
}

static void finish (int connect)
{
  store_net (); store_global ();
  if (!valid ()) return;
  if (cb->saved) cb->saved (&ed);
  if (connect && cb->connect) cb->connect (ed_net);
  Window_Hide (win);
}

static void do_new (void)
{
  char name[48];
  int k;
  netsettings *n;
  store_net ();
  copy (name, sizeof name, "New network");
  for (k = 2; settings_find (&ed, name) >= 0 && k < 100; k++) snprintf (name, sizeof name, "New network %d", k);
  n = settings_add (&ed, name);
  if (!n) { Error_Report (0, "There is no room for another network (the most is %d).", MAX_NETWORKS); return; }
  ed_net = settings_find (&ed, name);
  load_net ();
  Icon_SetCaret (win, i_netname);
}

static void do_delete (void)
{
  if (ed_net < 0 || ed_net >= ed.nnets) return;
  settings_remove (&ed, ed_net);
  if (!ed.nnets) settings_add (&ed, "New network");
  if (ed_net >= ed.nnets) ed_net = ed.nnets - 1;
  load_net ();
}

static void pick_menu (event_pollblock *event)
{
  char desc[1200];
  int i;
  size_t n = 0;
  store_net ();
  desc[0] = 0;
  for (i = 0; i < ed.nnets && n < 1000; i++) n += (size_t) snprintf (desc + n, sizeof desc - n, "%s%s%s", i ? "," : "", i == ed_net ? "!" : "", ed.nets[i].name);
  { menu_ptr fresh = Menu_New ("Network", desc); if (netmenu) Menu_FullDispose (netmenu); netmenu = fresh; }
  menu_open = 1;
  Menu_Show (netmenu, event->data.mouse.pos.x - 64, event->data.mouse.pos.y);
}

int choices_handle_menu (const int *sel)
{
  if (!menu_open) return 0;
  menu_open = 0;
  if (sel[0] >= 0 && sel[0] < ed.nnets) { ed_net = sel[0]; load_net (); }
  return 1;
}

/* ---- events ---- */

static BOOL click_handler (event_pollblock *event, void *reference)
{
  icon_handle i = event->data.mouse.icon;
  (void) reference;
  if (i == i_save) finish (0);
  else if (i == i_connect) finish (1);
  else if (i == i_cancel) Window_Hide (win);
  else if (i == i_new) do_new ();
  else if (i == i_del) do_delete ();
  else if (i == i_pick) pick_menu (event);
  else if (i == i_open) { if (cb->open_logs) cb->open_logs (); }
  return TRUE;
}

static BOOL key_handler (event_pollblock *event, void *reference)
{
  int code = event->data.key.code, k;
  icon_handle here = event->data.key.caret.icon;
  (void) reference;
  if (code == 13) { finish (0); return TRUE; }
  if (code == 27) { Window_Hide (win); return TRUE; }
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
  Window_Hide (win);
  return TRUE;
}

/* ---- the window ---- */

void choices_init (const choices_callbacks *callbacks)
{
  window_block wb;
  int r, height;
  static const int rows = 20;
  cb = callbacks;
  height = 24 + rows * ROW + 20;
  memset (&wb, 0, sizeof wb);
  wb.screenrect.min.x = 200; wb.screenrect.min.y = 100; wb.screenrect.max.x = 200 + W; wb.screenrect.max.y = 100 + height;
  wb.behind = -1;
  wb.flags.value = 0x87000012u;						/* back, close and title; moveable; the Wimp redraws it (it only has icons) */
  wb.colours.cols.titlefore = 7; wb.colours.cols.titleback = 2; wb.colours.cols.workfore = 7; wb.colours.cols.workback = 1;
  wb.colours.cols.scrollouter = 3; wb.colours.cols.scrollinner = 1; wb.colours.cols.titlefocus = 12;
  wb.workarearect.min.x = 0; wb.workarearect.max.x = W; wb.workarearect.max.y = 0; wb.workarearect.min.y = -height;
  wb.titleflags.value = 0x19; wb.workflags.value = 0;
  wb.spritearea = 1;
  wb.minsize.x = 100; wb.minsize.y = 100;
  strncpy (wb.title.text, "Choices", 11);
  Wimp_CreateWindow (&wb, &win);
  r = 0;
  label (r++, l_you);
  label (r, l_nick); i_nick = field (r++, t_nick, sizeof t_nick, NULL, FLD_R);
  label (r, l_user); i_user = field (r++, t_user, sizeof t_user, NULL, FLD_R);
  label (r, l_real); i_real = field (r++, t_real, sizeof t_real, NULL, FLD_R);
  label (r++, l_logs);
  i_log = mk (LBL_X, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_OPTION, l_logopt, sizeof l_logopt, v_opt); r++;
  label (r, l_logdir);
  i_logdir = field (r, t_logdir, sizeof t_logdir, NULL, FLD_R - 130);
  i_open = mk (FLD_R - 110, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_BUTTON, l_open, sizeof l_open, NULL); r++;
  label (r, l_net);
  i_pick = mk (FLD_X, rowtop (r) - 44, FLD_R - 250, rowtop (r) - 4, F_POPUP, t_pick, sizeof t_pick, NULL);
  i_new = mk (FLD_R - 230, rowtop (r) - 44, FLD_R - 120, rowtop (r) - 4, F_BUTTON, l_new, sizeof l_new, NULL);
  i_del = mk (FLD_R - 110, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_BUTTON, l_del, sizeof l_del, NULL); r++;
  label (r, l_name); i_netname = field (r++, t_netname, sizeof t_netname, NULL, FLD_R);
  label (r, l_server); i_host = field (r, t_host, sizeof t_host, NULL, FLD_R - 250);
  mk (FLD_R - 230, rowtop (r) - 44, FLD_R - 160, rowtop (r) - 4, F_LABEL, l_port, sizeof l_port, NULL);
  i_port = field (r++, t_port, sizeof t_port, v_port, FLD_R);
  i_tls = mk (FLD_X, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_OPTION, l_tls, sizeof l_tls, v_opt); r++;
  label (r, l_netnick); i_netnick = field (r++, t_netnick, sizeof t_netnick, NULL, FLD_R);
  label (r, l_sasl); i_sasl_user = field (r++, t_sasl_user, sizeof t_sasl_user, NULL, FLD_R);
  label (r, l_pass); i_sasl_pass = field (r++, t_sasl_pass, sizeof t_sasl_pass, v_pass, FLD_R);
  label (r, l_srvpass); i_srv_pass = field (r++, t_srv_pass, sizeof t_srv_pass, v_pass, FLD_R);
  label (r, l_join); i_autojoin = field (r++, t_autojoin, sizeof t_autojoin, NULL, FLD_R);
  i_auto = mk (FLD_X, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_OPTION, l_auto, sizeof l_auto, v_opt); r++;
  mk (LBL_X, rowtop (r) - 44, FLD_R, rowtop (r) - 4, F_LABEL, l_hint, sizeof l_hint, NULL); r++;
  i_cancel = mk (LBL_X, rowtop (r) - 52, LBL_X + 200, rowtop (r) - 4, F_BUTTON, l_cancel, sizeof l_cancel, NULL);
  i_connect = mk (FLD_R - 440, rowtop (r) - 52, FLD_R - 230, rowtop (r) - 4, F_BUTTON, l_connect, sizeof l_connect, NULL);
  i_save = mk (FLD_R - 210, rowtop (r) - 52, FLD_R, rowtop (r) - 4, F_BUTTON, l_save, sizeof l_save, NULL);
  Event_Claim (event_CLICK, win, event_ANY, click_handler, NULL);
  Event_Claim (event_KEY, win, event_ANY, key_handler, NULL);
  Event_Claim (event_CLOSE, win, event_ANY, close_handler, NULL);
  Event_Claim (event_OPEN, win, event_ANY, Handler_OpenWindow, NULL);
}

void choices_show (const settings *s, int select)
{
  window_state st;
  Wimp_GetWindowState (win, &st);
  if (!st.flags.data.open)							/* a fresh start from the settings; an open window keeps what has been typed */
    {
      ed = *s;
      if (!ed.nnets) settings_add (&ed, "New network");
      ed_net = select >= 0 && select < ed.nnets ? select : 0;
      load_global ();
      load_net ();
    }
  Window_Show (win, open_CENTERED);
  Icon_SetCaret (win, i_nick);
}

/* ---- the program information window: the standard box that comes out of the first item of the icon bar menu ---- */

window_handle info_window_create (void)
{
  static char title[] = "Program information";
  static char t_name[] = "Natter", l_purpose[] = "Purpose", t_purpose[] = "An IRC client for RISC OS 64", l_author[] = "Author", t_author[] = "Rob Heaton";
  static char l_version[] = "Version", t_version[] = "0.1 (" __DATE__ ")", t_ssl[] = "Uses OpenSSL " OPENSSL_VERSION_STR " (Apache Licence 2.0)", t_sprite[] = "natterbig";
  window_block wb;
  window_handle w = 0, keep = win;
  icon_createblock ic;
  const int width = 640, height = 300;
  memset (&wb, 0, sizeof wb);
  wb.screenrect.min.x = 300; wb.screenrect.min.y = 300; wb.screenrect.max.x = 300 + width; wb.screenrect.max.y = 300 + height;
  wb.behind = -1;
  wb.flags.value = 0x84000010u;						/* a title bar; the Wimp redraws it */
  wb.colours.cols.titlefore = 7; wb.colours.cols.titleback = 2; wb.colours.cols.workfore = 7; wb.colours.cols.workback = 1;
  wb.colours.cols.scrollouter = 3; wb.colours.cols.scrollinner = 1; wb.colours.cols.titlefocus = 12;
  wb.workarearect.min.x = 0; wb.workarearect.max.x = width; wb.workarearect.max.y = 0; wb.workarearect.min.y = -height;
  wb.titleflags.value = 0x119; wb.workflags.value = 0;
  wb.title.indirecttext.buffer = DL_PTR (title); wb.title.indirecttext.validstring = (dl_ptr) -1; wb.title.indirecttext.bufflen = sizeof title;
  wb.spritearea = 1;
  wb.minsize.x = 100; wb.minsize.y = 100;
  Wimp_CreateWindow (&wb, &w);
  win = w;									/* mk () makes its icons in win */
  memset (&ic, 0, sizeof ic);
  ic.window = w;
  ic.icondata.workarearect.min.x = 24; ic.icondata.workarearect.min.y = -170; ic.icondata.workarearect.max.x = 160; ic.icondata.workarearect.max.y = -34;
  ic.icondata.flags.value = (7u << 24) | 0x2u | 0x8u | 0x10u | 0x100u;		/* a sprite, centred, indirected */
  ic.icondata.data.indirectsprite.name = DL_PTR (t_sprite); ic.icondata.data.indirectsprite.spritearea = (dl_ptr) 1; ic.icondata.data.indirectsprite.nameisname = sizeof t_sprite;
  { icon_handle h; Wimp_CreateIcon (&ic, &h); }
  mk (190, -74, width - 20, -30, F_LABEL, t_name, sizeof t_name, NULL);
  mk (190, -126, 300, -86, F_LABEL, l_purpose, sizeof l_purpose, NULL); mk (310, -126, width - 20, -86, F_LABEL, t_purpose, sizeof t_purpose, NULL);
  mk (190, -172, 300, -132, F_LABEL, l_author, sizeof l_author, NULL); mk (310, -172, width - 20, -132, F_LABEL, t_author, sizeof t_author, NULL);
  mk (190, -218, 300, -178, F_LABEL, l_version, sizeof l_version, NULL); mk (310, -218, width - 20, -178, F_LABEL, t_version, sizeof t_version, NULL);
  mk (24, -276, width - 20, -236, F_LABEL, t_ssl, sizeof t_ssl, NULL);
  win = keep;
  return w;
}
