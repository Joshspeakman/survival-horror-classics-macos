#!/bin/bash
# re_qa.sh <out dir> [version ids...] : the QA pass, from the installed app ("Survival Horror Classics Nightly.app" in
# ~/Applications, or SHC_APP=<app>). For each version: title -> a new game (scripted keys) -> play, then room jumps with
# the Test Aids on (SHC_AIDS=1 SHC_ROOM=<room>), each run in test mode (a background window, muted). Writes a contact
# sheet per version (<out>/<id>.png), and <out>/report.txt: faults, crash reports, frame rate, frames per run.
set -uo pipefail
OUT="$1"; shift; IDS="${*:-re1 re2 re2c re3 re3m dc}"
APP="${SHC_APP:-$HOME/Applications/Survival Horror Classics Nightly.app}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GAMES="${SHC_GAMES_ROOT:-/Volumes/Expansion Pak/Games}"
M="$OUT/.montage"; mkdir -p "$OUT"; clang -O2 -fobjc-arc -framework Cocoa -o "$M" "$HERE/montage.m" 2>/dev/null || exit 1
: > "$OUT/report.txt"

info() {   # id -> BIN EXE DIR ARGS NEWGAME(keys) ROOMS SECS
  case $1 in
    re1)  BIN=ResidentEvil;            EXE=re1-ResidentEvil.exe;       DIR="ResidentEvilNative/game-files/Resident Evil"; ARGS=""
          KEYS="700:key:13;900:key:13;1100:key:13;1300:key:13;1500:key:13;1700:key:13"; PLAY="2000 2400 2800"; ROOMS="103 10a 20e 305"; SECS=110; AT=1500;;
    re2|re2c) BIN=$([ $1 = re2 ] && echo ResidentEvil2Leon || echo ResidentEvil2Claire); EXE=$([ $1 = re2 ] && echo re2-LeonU.exe || echo re2c-ClaireU.exe)
          DIR="ResidentEvil2Native/game-files/Resident Evil 2"; ARGS="-mode0"
          KEYS="1250:key:40;1280:key:13;1500:key:13;1750:key:13;2000:key:13;2250:key:13"; PLAY="2400 2800 3200"; ROOMS="101 10a 203 30b"; SECS=110; AT=2600;;
    re3|re3m) BIN=$([ $1 = re3 ] && echo ResidentEvil3 || echo ResidentEvil3Mercenaries); EXE=$([ $1 = re3 ] && echo re3-ResidentEvil3.exe || echo re3m-RE3_MERCE.exe)
          DIR="ResidentEvil3Native/game-files/Resident Evil 3"; ARGS=""
          KEYS="1250:key:37;1270:key:37;1300:key:13;1700:key:13;2100:key:13;2500:key:13"; PLAY="2900 3300 3700"; ROOMS="107 20a 10e"; SECS=150; AT=3000
          [ $1 = re3m ] && { KEYS="900:key:13;1100:key:13;1300:key:13;1500:key:13;1700:key:13;1900:key:13"; PLAY="2100 2500 2900"; ROOMS=""; };;
    dc)   BIN=DinoCrisis; EXE=dc-DINO.exe; DIR="DinoCrisisNative/game-files/Dino Crisis"; ARGS=""
          KEYS="900:key:13;1100:key:13;1400:key:13;1900:click:371,1318"; for f in $(seq 2100 120 7600); do KEYS="$KEYS;${f}:key:13"; done
          PLAY="2600 3600 5000 6500 7800"; ROOMS=""; SECS=280; AT=0;;
  esac
}
run() {    # id tag secs script [env...]: one run; shots named <tag>-<frame>.png
  local id=$1 tag=$2 secs=$3 script=$4; shift 4
  local R="$OUT/$id/$tag"; mkdir -p "$R/data"
  env DS_TEST=1 DS_NO_LAUNCHER=1 W32_AUDIO_MUTE=1 DS_ARGS="$ARGS" DS_SCRIPT="$script" "$@" timeout "$secs" "$APP/Contents/MacOS/$BIN" \
      --exe "$APP/Contents/Resources/$EXE" --game "$GAMES/$DIR" --data "$R/data" > "$R/log.txt" 2>&1
  local rc=$? faults crash fps frames
  faults=$(grep -a -c "fault\|unhandled exception" "$R/log.txt"); crash=$(ls "$R/data/CrashReports" 2>/dev/null | wc -l | tr -d ' ')
  fps=$(grep -a "host: frame" "$R/log.txt" | tail -1 | sed -E 's/.*\) ([0-9.]+) fps.*/\1/'); frames=$(grep -a "host: frame" "$R/log.txt" | tail -1 | awk '{print $3}')
  printf "%-5s %-10s exit %-3s faults %-3s crash reports %-2s last fps %-5s frames %s %s\n" "$id" "$tag" "$rc" "$faults" "$crash" "${fps:-?}" "${frames:-?}" \
         "$(grep -a 'room jump' "$R/log.txt" | head -1 | sed 's/^re: //')" | tee -a "$OUT/report.txt"
}
for id in $IDS; do
  info $id; [ -x "$APP/Contents/MacOS/$BIN" ] || { echo "$id: not installed" | tee -a "$OUT/report.txt"; continue; }
  mkdir -p "$OUT/$id"; shots=()
  S="$KEYS"; for f in $PLAY; do S="$S;${f}:shot:$OUT/$id/play-$f.png"; shots+=("$OUT/$id/play-$f.png"); done
  run $id play $SECS "$S"
  for room in $ROOMS; do
    S="$KEYS;$((AT + 300)):shot:$OUT/$id/room-$room.png"; shots+=("$OUT/$id/room-$room.png")
    run $id room-$room $((SECS - 20)) "$S" SHC_AIDS=1 SHC_ROOM=$room
  done
  existing=(); for s in "${shots[@]}"; do [ -f "$s" ] && existing+=("$s"); done
  [ ${#existing[@]} -gt 0 ] && "$M" "$OUT/$id.png" 4 320 "${existing[@]}"
done
rm -f "$M"
