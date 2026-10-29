#!/bin/bash
# spike/build.sh [TC]: builds the scrollback spike into out/!IRCSpike (copy the folder to RISC OS 64 and double-click it).
# TC = a RISC OS 64 tool chain folder (default: the development one); DL = the built DeskLib64 (default ~/Development/desklib64/dist).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TC=${1:-${TC:-/var/tmp/a64/target7-inst}}
DL=${DL:-/home/robh/Development/desklib64/dist}
CC=$TC/bin/aarch64-riscos-gcc
A=$HERE/out/'!IRCSpike'
mkdir -p "$A"
$CC -munixlib -O2 -fsigned-char -Wall -Wextra -I "$DL/include" "$HERE/scroll/scroll.c" "$DL/lib/libDeskLib64.a" -o "$A/!RunImage,ff8"
printf '| IRC scrollback spike\nRun <Obey$Dir>.!RunImage\n' > "$A/!Run,feb"
printf '| IRC scrollback spike\n' > "$A/!Boot,feb"
echo "built $A"
