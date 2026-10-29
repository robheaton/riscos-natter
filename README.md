# Natter

An IRC client for RISC OS 64 (AArch64), with a native Wimp front end: one window with the scrollback in the middle and panes around it (buffer list, topic, nick list, input line), not a terminal.

**Status: early, but it connects.** It has been run on a Raspberry Pi 5 with RISC OS 64: it connects to a public IRC network with TLS (the certificate and the server name are checked), logs in, joins channels, and shows the conversation with timestamps, coloured nicks, actions, highlights for mentions, unread badges, a nick list with prefixes, Tab completion of nicks and a history of what you typed.

## What is where

| Folder | What |
|---|---|
| `core/` | the protocol library in portable C (no sockets inside): IRCv3 messages and tags, capability negotiation, SASL PLAIN, channels and the people in them, CTCP, a flood-limited send queue. `make test` runs 409 checks and a fuzzer under AddressSanitizer. |
| `app/` | the application: `natter.c` (windows, events, commands), `buffer.c` (the lines of a conversation and their wrapping), `net.c` (sockets and TLS with OpenSSL), `settings.c` (the choices file), `demo.c` (a pretend server). `make test` there runs the tests of buffer and settings; `tests/net/run.sh` the tests of net.c against local servers with their own CA. `build.sh` builds `out/!Natter` for RISC OS 64. |
| `spike/` | the first test: a scrollback window with 10,000 lines, and `deploy.sh` that copies an application to the machine. |
| `tests/` | the test programs. |

## Using it

Start `!Natter`. It starts disconnected. `/help` lists the commands. `/connect NAME` connects to a network of your choices (`/networks` lists them), `/connect host [port] [notls]` to any server, `/savenet NAME` keeps it in `Choices:Natter.Choices` (plain text: look at it), `/demo` shows a made-up conversation. Double-click a nick to talk to that person.

## Not done yet

A dialog for the networks, logs (plain text, one file for each channel, with an off switch), menus and an iconbar icon, reconnecting, IRC colour codes, links, selecting and copying text.

64-bit only. Built with the [riscos-gcc16](https://github.com/robheaton/riscos-gcc16) RISC OS 64 tool chain, DeskLib64 and OpenSSL.
