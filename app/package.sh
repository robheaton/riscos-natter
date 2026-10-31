#!/bin/bash
# app/package.sh VERSION: makes out/Natter-VERSION.zip from out/!Natter (run build.sh first).  The RISC OS file types go into the zip itself (tools/rozip.py), so that SparkFS
# and the other unzips on RISC OS give the files their types and plain names.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
V=${1:?give the version}
[ -d "$HERE/out/!Natter" ] || { echo "run build.sh first"; exit 1; }
rm -f "$HERE/out/Natter-$V.zip"
python3 "$HERE/tools/rozip.py" --from-dir "$HERE/out/!Natter" "$HERE/out/Natter-$V.zip" --top '!Natter'
ls -l "$HERE/out/Natter-$V.zip"
