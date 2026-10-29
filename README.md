# Natter

An IRC client for RISC OS 64 (AArch64), with a native Wimp front end: a window with a proportional-font scrollback, not a terminal.

**Status: early.** Nothing here is usable yet. What exists:

* `spike/`: a test application (`!IRCSpike`) that shows a scrollback window with 10,000 generated chat lines, with its own redraw loop, built with DeskLib64. It was run on RISC OS 64 to check that the idea works: text in proportional fonts (UTF-8), colours, filled line backgrounds, a scroll bar. `spike/build.sh` builds it, `spike/deploy.sh` copies it to a machine.

## Plan

1. `core/`: a protocol library in portable C with no sockets inside it (bytes in, events and commands out), tested on Linux against scripted server conversations: message parsing with IRCv3 tags, capability negotiation and SASL login, `005` server settings, channel and nick tracking, a rate-limited send queue.
2. The Wimp application: a buffer list (networks and channels, with unread and highlight marks), the scrollback, a nick list, a topic bar and an input line.
3. Sockets and TLS (OpenSSL) on RISC OS 64.

64-bit only. Built with the [riscos-gcc16](https://github.com/robheaton/riscos-gcc16) RISC OS 64 tool chain and DeskLib64.
