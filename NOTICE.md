# Natter: the licence, and what is inside the program

## Natter itself

Natter's own source (everything in `core/`, `app/`, `tests/` and `spike/`, except the files named below) is under the **Zero-Clause BSD** licence (`LICENCE`): anyone may use, copy, modify and distribute it for any purpose, with or without fee, and does not have to keep any notice or give any credit. SPDX identifier: `0BSD`.

## What the built program contains

The compiled `!Natter` also contains other people's work. Those parts keep their own licences, so a copy of the **program** (not of Natter's source) has to carry these notices; `build.sh` puts this file into the application folder as `Notices` for that reason.

| Part | Where it comes from | Licence | What it asks of someone who distributes the program |
|---|---|---|---|
| DeskLib (the Wimp library) | DeskLib, ported to 64-bit as DeskLib64 | the DeskLib licence (`LICENCE` of DeskLib64: "may be used in the production of any piece of software so long as the authors acknowledge in the final product distribution that DeskLib (or parts thereof) was used") | **acknowledge that DeskLib was used**: this paragraph does that |
| OpenSSL 4.0.3 (TLS) | openssl.org | Apache License 2.0 | include a copy of the Apache 2.0 licence and keep OpenSSL's notices |
| `Resources/ca-bundle` (the root certificates that a server's certificate is checked against) | Mozilla, through the curl project | Mozilla Public License 2.0 | the file is plain text and is its own source: keep it with its notices |
| the C library and runtime of the RISC OS 64 tool chain (UnixLib-derived, libgcc) | riscos-gcc16 | see that project's licence notes (libgcc has the GCC Runtime Library Exception) | as that project says |

**Acknowledgement.** This program uses DeskLib, "the FreeWare C library for RISC OS machines", by its many authors (see the `LICENCE` and `NOTICE.md` of DeskLib64), and OpenSSL (Apache License 2.0, https://www.openssl.org).

## What this does not mean

The Zero-Clause BSD licence is about Natter's code. It cannot change the licences of DeskLib, OpenSSL, the certificate list or the C library above. Anyone who rebuilds Natter from source with a different Wimp library, TLS library and certificate list is only bound by what those ones say.
