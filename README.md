# Natter

An IRC client for RISC OS 64 (AArch64), with a native Wimp front end: one window with the scrollback in the middle and panes around it (buffer list, topic, nick list, input line), not a terminal.

**Status: version 0.1.1, usable.** It has been run on a Raspberry Pi 5 with RISC OS 64. It connects to IRC networks with TLS (the certificate and the server name are checked), logs in with a password or SASL, joins channels and shows the conversation with timestamps, coloured nicks, IRC formatting (bold, italic, underline, colours), highlights for mentions with an optional beep, unread badges, a nick list with prefixes, clickable web addresses, selecting and copying text, Tab completion of nicks and a history of what you typed. Choices and Appearance (text size, light or dark theme, colours) are dialogue boxes; logs are plain text; the connection comes back by itself.

## What is where

| Folder | What |
|---|---|
| `core/` | the protocol library in portable C (no sockets inside): IRCv3 messages and tags, capability negotiation, SASL PLAIN, channels and the people in them, CTCP, a flood-limited send queue. `make test` runs 409 checks and a fuzzer under AddressSanitizer. |
| `app/` | the application: `natter.c` (windows, events, commands), `buffer.c` (the lines of a conversation and their wrapping), `net.c` (sockets and TLS with OpenSSL), `settings.c` (the choices file), `logfile.c` (the plain text logs), `theme.c` (colours), `select.c` (selecting and copying text), `clip.c` (the Wimp clipboard), `choices.c`, `appearance.c` and `dialog.c` (the dialogue boxes, built in code), `demo.c` (a pretend server). `make test` there runs the host tests of everything without Wimp calls; `tests/net/run.sh` the tests of net.c against local servers with their own CA. `build.sh` builds `out/!Natter` for RISC OS 64. |
| `spike/` | the first test: a scrollback window with 10,000 lines, and `deploy.sh` that copies an application to the machine. |
| `tests/` | the test programs. |

## Using it

Start `!Natter`: it puts an icon on the icon bar (Menu: Info, Networks, Choices..., Appearance..., Quit; Select opens the window again after you have closed it). It starts disconnected.

- **Choices** is a dialogue box: your nick and names, whether to keep logs and where, and the networks (server, port, TLS, a nick of its own, a SASL login, a server password, channels to join, connect when Natter starts). Connect from there, or with `/connect NAME`; `/connect host [port] [notls]` goes to any server, `/savenet NAME` keeps it, `/help` lists the commands.
- **Appearance** sets the size of the text (8 to 16 points), a light or dark theme and the colours (six hex digits each); Apply shows the result at once.
- **Logs** are plain text, one file for each channel, query and network status, in a folder for each network (`<Choices$Write>.Natter.Logs` unless you choose another); `/log off` stops them.
- **Menus**: Menu over the conversation or the buffer list (join, part, topic, copy, log, close), over a person in the nick list (talk to, who is, op and voice, kick, ban). A double click on a nick opens a conversation with that person.
- **Selecting text**: drag with Select over the conversation, double click for a word, Adjust to extend. Ctrl-C (or Copy selection in the menu) copies it, with the time and nick of the lines that are selected from their start; Ctrl-V pastes into the input line. The clipboard is the Wimp's, so other programs can paste what you copied.
- The connection comes back by itself after a failure (5 seconds, doubling to 5 minutes) and joins the channels again.

## Not done yet

several networks at once.

## Debug log

Natter writes no debug log unless you ask for one. To get one (for a bug report), add a line to the choices file (`<Choices$Write>.Natter.Choices`) giving a folder that exists:

    debug_log = RAM::RamDisc0.$.NatterDebug

Each run then writes a file `nl<time>` in that folder with what the program did (every window redraw and event), so it grows fast: only use it when you are looking for a fault. The Choices window keeps the line when it saves.

## Licence

Natter is under the **Zero-Clause BSD** licence (`LICENCE`): use it, copy it, change it and share it for any purpose, with no conditions. The compiled program also contains DeskLib, OpenSSL, a list of root certificates and the C library of the tool chain, which keep their own licences: `NOTICE.md` says which, and what a copy of the program has to say about them.

64-bit only. Built with the [riscos-gcc16](https://github.com/robheaton/riscos-gcc16) RISC OS 64 tool chain, DeskLib64 and OpenSSL 4.0.3 (`build.sh` needs `SSL=` a folder with the AArch64 OpenSSL headers and libraries).
