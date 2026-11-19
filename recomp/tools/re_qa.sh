#!/bin/bash
# re_qa.sh <out dir> [version ids...] : the QA pass, from the installed app ("Survival Horror Classics Nightly.app" in
# ~/Applications, or SHC_APP=<app>). For each version: title -> a new game (scripted keys) -> play, then room jumps with
# the Test Aids on (SHC_AIDS=1 SHC_ROOM=<room>), each run in test mode (a background window, muted). Writes a contact
# sheet per version (<out>/<id>.png), and <out>/report.txt: faults, crash reports, the game's error boxes, movies the host
# played, frame rate, frames per run.
# QA_MODS_ROOT=<folder like ~/Games/SurvivalHorrorClassics/mods>: one more play run per version with every mod installed
# for its game on (GTA_MODS), its mod files listed in the report.
set -uo pipefail
OUT="$1"; shift; IDS="${*:-re1 re2 re2c re3 re3m dc}"
APP="${SHC_APP:-$HOME/Applications/Survival Horror Classics Nightly.app}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GAMES="${SHC_GAMES_ROOT:-/Volumes/Expansion Pak/Games}"
M="$OUT/.montage"; mkdir -p "$OUT"; clang -O2 -fobjc-arc -framework Cocoa -o "$M" "$HERE/montage.m" 2>/dev/null || exit 1
: > "$OUT/report.txt"

info() {   # id -> BIN EXE DIR ARGS NEWGAME(keys) ROOMS SECS SKIP (DS_MOVIE_SKIP: each movie this long, then skipped)
  SKIP=""
  case $1 in
    re1)  BIN=ResidentEvil;            EXE=re1-ResidentEvil.exe;       DIR="ResidentEvilNative/game-files/Resident Evil"; ARGS=""; SKIP="4,4,4,4,4,4,4,4"
          KEYS="700:key:13;900:key:13;1100:key:13;1300:key:13;1500:key:13;1700:key:13"; PLAY="2000 2400 2800"; ROOMS="103 10a 20e 305"; SECS=110; AT=2000;;
    re2|re2c) BIN=$([ $1 = re2 ] && echo ResidentEvil2Leon || echo ResidentEvil2Claire); EXE=$([ $1 = re2 ] && echo re2-LeonU.exe || echo re2c-ClaireU.exe)
          DIR="ResidentEvil2Native/game-files/Resident Evil 2"; ARGS="-mode0"
          KEYS="1250:key:40;1280:key:13;1500:key:13;1750:key:13;2000:key:13;2250:key:13"; PLAY="2400 2800 3200"; ROOMS="101 10a 203 30b"; SECS=110; AT=2600;;
    re3|re3m) BIN=$([ $1 = re3 ] && echo ResidentEvil3 || echo ResidentEvil3Mercenaries); EXE=$([ $1 = re3 ] && echo re3-ResidentEvil3.exe || echo re3m-RE3_MERCE.exe)
          DIR="ResidentEvil3Native/game-files/Resident Evil 3"; ARGS=""
          KEYS="1250:key:37;1270:key:37;1300:key:13;1700:key:13;2100:key:13;2500:key:13"; PLAY="2900 3300 3700"; ROOMS="107 112 113 10e 20a 305 30b"; SECS=150; AT=3000
          [ $1 = re3m ] && { KEYS="700:key:13;900:key:13;1100:key:13;1300:key:13;1500:key:13;1700:key:13;2400:key:13;2600:key:13;2800:key:13"; PLAY="2300 3200 3800 4400"; ROOMS=""; };;
    dc)   BIN=DinoCrisis; EXE=dc-DINO.exe; DIR="DinoCrisisNative/game-files/Dino Crisis"; ARGS=""
          KEYS="900:key:13;1100:key:13;1400:key:13;1900:click:371,1318"; for f in $(seq 2100 120 9400); do KEYS="$KEYS;${f}:key:13"; done
          PLAY="2600 3600 5000 6500 7800 8600 9400 10000"; ROOMS="105 203"; SECS=380; AT=9800;;    # room shots in play, after the opening (a room run reaches ~10450 frames)
  esac
}
run() {    # id tag secs script [env...]: one run; shots named <tag>-<frame>.png
  local id=$1 tag=$2 secs=$3 script=$4; shift 4
  local R="$OUT/$id/$tag"; mkdir -p "$R/data"
  env DS_TEST=1 DS_NO_LAUNCHER=1 W32_AUDIO_MUTE=1 DS_ARGS="$ARGS" DS_SCRIPT="$script" DS_MOVIE_SKIP="$SKIP" "$@" timeout "$secs" "$APP/Contents/MacOS/$BIN" \
      --exe "$APP/Contents/Resources/$EXE" --game "$GAMES/$DIR" --data "$R/data" > "$R/log.txt" 2>&1
  local rc=$? faults crash fps frames boxes movies
  faults=$(grep -a -c "fault\|unhandled exception" "$R/log.txt"); crash=$(ls "$R/data/CrashReports" 2>/dev/null | wc -l | tr -d ' ')
  boxes=$(grep -a -c "MessageBox" "$R/log.txt"); movies=$(grep -a -c "host: movie /" "$R/log.txt")   # error boxes (test mode closes them)
  fps=$(grep -a "host: frame" "$R/log.txt" | tail -1 | sed -E 's/.*\) ([0-9.]+) fps.*/\1/'); frames=$(grep -a "host: frame" "$R/log.txt" | tail -1 | awk '{print $3}')
  printf "%-5s %-10s exit %-3s faults %-3s crash reports %-2s boxes %-2s movies %-2s last fps %-5s frames %s %s\n" "$id" "$tag" "$rc" "$faults" "$crash" "$boxes" "$movies" "${fps:-?}" "${frames:-?}" \
         "$(grep -a 'room jump' "$R/log.txt" | head -1 | sed 's/^re: //')" | tee -a "$OUT/report.txt"
}
for id in $IDS; do
  info $id; [ -x "$APP/Contents/MacOS/$BIN" ] || { echo "$id: not installed" | tee -a "$OUT/report.txt"; continue; }
  mkdir -p "$OUT/$id"; shots=()
  S="$KEYS"; for f in $PLAY; do S="$S;${f}:shot:$OUT/$id/play-$f.png"; shots+=("$OUT/$id/play-$f.png"); done
  run $id play $SECS "$S"
  if [ -n "${QA_MODS_ROOT:-}" ]; then
    case $id in re2c) game=re2;; re3m) game=re3;; *) game=$id;; esac; mods=""
    for k in $(seq 0 63); do                     # the launcher's order (index.plist, first wins), on ones only; else every folder
      f=$(/usr/libexec/PlistBuddy -c "Print :$k:folder" "$QA_MODS_ROOT/$game/index.plist" 2>/dev/null) || break
      [ "$(/usr/libexec/PlistBuddy -c "Print :$k:enabled" "$QA_MODS_ROOT/$game/index.plist" 2>/dev/null)" = true ] && mods="$mods${mods:+$'\n'}$QA_MODS_ROOT/$game/$f"
    done
    [ -n "$mods" ] || for d in "$QA_MODS_ROOT/$game"/*/; do [ -d "$d" ] && mods="$mods${mods:+$'\n'}${d%/}"; done
    if [ -n "$mods" ]; then
      S="$KEYS"; for f in $PLAY; do S="$S;${f}:shot:$OUT/$id/mods-$f.png"; shots+=("$OUT/$id/mods-$f.png"); done
      run $id mods $SECS "$S" GTA_MODS="$mods" W32_FILELOG=1
      echo "      mod files read: $(grep -a 'CreateFileA\|re: mod file' "$OUT/$id/mods/log.txt" | grep -a -c "$QA_MODS_ROOT")" | tee -a "$OUT/report.txt"
    fi
  fi
  for room in $ROOMS; do
    S="$KEYS;$((AT + 300)):shot:$OUT/$id/room-$room.png"; shots+=("$OUT/$id/room-$room.png")
    run $id room-$room $((SECS - 20)) "$S" SHC_AIDS=1 SHC_ROOM=$room
  done
  existing=(); for s in "${shots[@]}"; do [ -f "$s" ] && existing+=("$s"); done
  [ ${#existing[@]} -gt 0 ] && "$M" "$OUT/$id.png" 4 320 "${existing[@]}"
done
rm -f "$M"
