#!/bin/bash
# capture_art.sh <app MacOS dir> <app Resources dir> <out dir> <version id> <game folder> [-- extra game args]
# The launch window's art for one game, from the game itself: it runs in test mode (a background window, muted, no
# movies) for a few seconds while its title comes up, and the most detailed of a few screenshots (the title, not a
# fade) becomes <out>/<id>-title.png. Run by install.sh; nothing leaves this Mac.
set -uo pipefail
MACOS="$1"; RES="$2"; OUT="$3"; ID="$4"; GAME="$5"; shift 5; [ "${1:-}" = -- ] && shift; ARGS="$*"
case "$ID" in re1) BIN=ResidentEvil; EXE=re1-ResidentEvil.exe;; re2) BIN=ResidentEvil2Leon; EXE=re2-LeonU.exe;;
              re3) BIN=ResidentEvil3; EXE=re3-ResidentEvil3.exe;; dc) BIN=DinoCrisis; EXE=dc-DINO.exe;;
              dc2) BIN=DinoCrisis2; EXE=dc2-Dino2.exe;; *) exit 1;; esac
[ -x "$MACOS/$BIN" ] && [ -d "$GAME" ] || exit 1
T="$(mktemp -d)"; mkdir -p "$T/data" "$OUT"
S=""; for f in ${ART_FRAMES:-500 700 900 1100 1300}; do S="$S;$f:shot:$T/s$f.png"; done
env DS_TEST=1 DS_NO_LAUNCHER=1 W32_AUDIO_MUTE=1 RE_NO_MOVIES=1 GTA_NO_MOVIES=1 DS_ARGS="$ARGS" DS_SCRIPT="${S#;}" \
  timeout 60 "$MACOS/$BIN" --exe "$RES/$EXE" --game "$GAME" --data "$T/data" >/dev/null 2>&1
best="$(ls -S "$T"/s*.png 2>/dev/null | head -1)"
if [ -n "$best" ] && [ "$(stat -f %z "$best")" -gt 30000 ]; then cp "$best" "$OUT/$ID-title.png"; echo "art: $ID from $(basename "$best" .png)"; fi
rm -rf "$T"
