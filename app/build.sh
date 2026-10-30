#!/bin/bash
# app/build.sh [TC]: builds Natter for RISC OS 64 into out/!Natter (copy the folder to the machine and double-click it).
# TC = a RISC OS 64 tool chain folder (default: the development one); DL = the built DeskLib64 (default ~/Development/desklib64/dist);
# SSL = a prefix with the AArch64 OpenSSL (include/ and lib/libssl.a, libcrypto.a; default: ~/Development/openssl-aarch64).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TC=${1:-${TC:-/var/tmp/a64/target7-inst}}
DL=${DL:-/home/robh/Development/desklib64/dist}
SSL=${SSL:-/home/robh/Development/openssl-aarch64}
CC=$TC/bin/aarch64-riscos-gcc
A=$HERE/out/'!Natter'
mkdir -p "$A/Resources"
$CC -munixlib -O2 -fsigned-char -std=gnu99 -Wall -Wextra -Wno-unused-parameter -I "$DL/include" -I "$SSL/include" -I "$HERE/../core" -I "$HERE" \
  "$HERE/natter.c" "$HERE/buffer.c" "$HERE/demo.c" "$HERE/net.c" "$HERE/settings.c" "$HERE/logfile.c" "$HERE/../core/irc_parse.c" "$HERE/../core/irc_conn.c" "$HERE/../core/irc_chan.c" \
  "$DL/lib/libDeskLib64.a" "$SSL/lib/libssl.a" "$SSL/lib/libcrypto.a" -o "$A/!RunImage,ff8"
cp "$HERE/Resources/ca-bundle" "$A/Resources/ca-bundle"
cp "$HERE/!Sprites,ff9" "$A/!Sprites,ff9"
printf '| Natter, an IRC client for RISC OS 64\nSet Natter$Dir <Obey$Dir>\nIconSprites <Obey$Dir>.!Sprites\nWimpSlot -min 16M -max 16M\nRun <Obey$Dir>.!RunImage\n' > "$A/!Run,feb"
printf '| Natter\nIconSprites <Obey$Dir>.!Sprites\n' > "$A/!Boot,feb"
echo "built $A"
