#!/bin/bash
# deploy.sh APPDIR TARGET: copies the files of an application folder (for example out/!IRCSpike) to TARGET on the RISC OS 64 machine (for example NVMe::NVMe.$.!IRCSpike) through the NAS and agent64.
# Every file goes into a fresh NAS folder with a new name (a file that a machine which was reset still has open stays locked on the NAS, so a name is never used twice).
set -e
APP=${1:?application folder}; TARGET=${2:?target on the machine}
R=/home/robh/gccsdk-next/riscos64/agent64/rr.sh
NAS=Development/GCC/riscos64/irc64/dep
smbclient -N //nas1.local/RISCOS -c "mkdir Development/GCC/riscos64/irc64; mkdir $NAS" >/dev/null 2>&1 || true
n=$(date +%H%M%S); k=0
$R -t 60 "CDir $TARGET" | tail -1
for f in "$APP"/*; do
  b=$(basename "$f"); name=$(echo "$b" | sed 's/,[0-9a-f]*$//'); type=$(echo "$b" | sed -n 's/^.*,\([0-9a-f]*\)$/\1/p')
  tmp="d${n}_$k"; k=$((k + 1))
  cp "$f" "/var/tmp/$tmp"
  smbclient -N //nas1.local/RISCOS -c "mkdir $NAS/$tmp; cd $NAS/$tmp; put /var/tmp/$tmp $tmp,$type" >/dev/null
  rm -f "/var/tmp/$tmp"
  $R -t 60 "Delete $TARGET.$name" >/dev/null 2>&1 || true
  $R -t 300 "Copy LanMan98::RISCOS.\$.Development.GCC.riscos64.irc64.dep.$tmp.$tmp $TARGET.$name ~C ~V F" | tail -1
  smbclient -N //nas1.local/RISCOS -c "cd $NAS/$tmp; rm $tmp,$type; cd ..; rmdir $tmp" >/dev/null 2>&1 || true
done
$R -t 60 "Info $TARGET.!RunImage" | head -2
