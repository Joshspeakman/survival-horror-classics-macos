#!/bin/bash
# development: run_re.sh <id> <seconds> [extra env...]: one test-mode run (background window, muted) into $(scratchdir re-run)/<id>-<n>
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ID=$1; SECS=$2; shift 2
case $ID in re1) EXE=ResidentEvil.exe G=ResidentEvilNative/game-files/"Resident Evil";; re2) EXE=LeonU.exe G=ResidentEvil2Native/game-files/"Resident Evil 2";;
  re2c) EXE=ClaireU.exe G=ResidentEvil2Native/game-files/"Resident Evil 2";; re3) EXE=ResidentEvil3.exe G=ResidentEvil3Native/game-files/"Resident Evil 3";;
  re3m) EXE=RE3_MERCE.exe G=ResidentEvil3Native/game-files/"Resident Evil 3";; dc) EXE=DINO.exe G=DinoCrisisNative/game-files/"Dino Crisis";;
  dc2) EXE=Dino2.exe G=DinoCrisis2Native/game-files/"Dino Crisis 2";; esac
BIN=$(ls "$HERE/work/$ID/app/" | grep -v -E '\.(metal|bin)$' | head -1)
n=1; R="$(scratchdir re-run)/$ID-$n"; while [ -e "$R" ]; do n=$((n+1)); R="$(scratchdir re-run)/$ID-$n"; done; mkdir -p "$R/data"
env DS_TEST=1 DS_NO_LAUNCHER=1 W32_AUDIO_MUTE=1 DS_SCRIPT="${DS_SCRIPT:-180:shot:$R/a.png;600:shot:$R/b.png;1200:shot:$R/c.png}" "$@" \
  timeout "$SECS" "$HERE/work/$ID/app/$BIN" --exe "$HERE/work/$ID/$EXE" --game "/Volumes/Expansion Pak/Games/$G" --data "$R/data" > "$R/log.txt" 2>&1
echo "exit $? -> $R"; ls "$R"; grep -v '^$' "$R/log.txt" | tail -${TAIL:-30}
