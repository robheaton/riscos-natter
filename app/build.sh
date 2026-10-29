#!/bin/bash
# app/build.sh [TC]: builds Natter for RISC OS 64 into out/!Natter (copy the folder to the machine and double-click it).
# TC = a RISC OS 64 tool chain folder (default: the development one); DL = the built DeskLib64 (default ~/Development/desklib64/dist).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TC=${1:-${TC:-/var/tmp/a64/target7-inst}}
DL=${DL:-/home/robh/Development/desklib64/dist}
CC=$TC/bin/aarch64-riscos-gcc
A=$HERE/out/'!Natter'
mkdir -p "$A"
$CC -munixlib -O2 -fsigned-char -std=gnu99 -Wall -Wextra -Wno-unused-parameter -I "$DL/include" -I "$HERE/../core" -I "$HERE" \
  "$HERE/natter.c" "$HERE/buffer.c" "$HERE/demo.c" "$HERE/../core/irc_parse.c" "$HERE/../core/irc_conn.c" "$HERE/../core/irc_chan.c" \
  "$DL/lib/libDeskLib64.a" -o "$A/!RunImage,ff8"
printf '| Natter, an IRC client for RISC OS 64\nWimpSlot -min 16M -max 16M\nRun <Obey$Dir>.!RunImage\n' > "$A/!Run,feb"
printf '| Natter\n' > "$A/!Boot,feb"
echo "built $A"
