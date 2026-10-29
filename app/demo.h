/* demo.h - a pretend IRC server, so that the application can be tried (and its windows developed) without a network.  It answers what the core sends (the capability negotiation, JOIN,
   PART, PRIVMSG, NICK, TOPIC, MODE) and now and then makes up some traffic: messages, actions, joins, quits, a topic change, a message for us, a private message. */
#ifndef NATTER_DEMO_H
#define NATTER_DEMO_H

/* the server sends these lines (separated by \n) to the client */
typedef void (*demo_feed_fn) (void *ud, const char *lines);

void demo_start (demo_feed_fn feed, void *ud, const char *nick);
void demo_from_client (const char *line);		/* one line that the client sent, without CR LF */
void demo_tick (unsigned now_cs);			/* call about ten times a second */

#endif
