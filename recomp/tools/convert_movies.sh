#!/bin/bash
# convert_movies.sh <game folder>: MPEG-1 copies of the movies macOS cannot decode, beside them as "<file>.mpg", which
# the runtime plays in their place (amstream.inc, dshow.c): Resident Evil 2's Indeo 5 AVIs (Zmovie/*.bin) and
# Resident Evil's Cinepak AVIs (USA/Movie/*.avi). Needs ffmpeg (Homebrew: brew install ffmpeg) while it runs; the
# originals are left as they are. Already converted files are kept.
set -euo pipefail
GAME="${1:?usage: convert_movies.sh <game folder>}"
FF="$(command -v ffmpeg || true)"; [ -n "$FF" ] || for p in /opt/homebrew/bin/ffmpeg /usr/local/bin/ffmpeg; do [ -x "$p" ] && FF="$p"; done
[ -n "$FF" ] || { echo "ffmpeg is needed to convert this game's movies (brew install ffmpeg)"; exit 2; }
n=0
while IFS= read -r -d '' f; do
  out="$f.mpg"; [ -s "$out" ] && continue
  # MPEG-1 video at a high quality, MP2 sound; the frame rate as it is unless MPEG-1 has no such rate (then 30)
  rate="$("${FF%ffmpeg}ffprobe" -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$f" 2>/dev/null || echo 30/1)"
  case "$rate" in 24000/1001|24/1|25/1|30000/1001|30/1|50/1|60000/1001|60/1) r="$rate";; *) r=30;; esac
  "$FF" -nostdin -v error -y -i "$f" -c:v mpeg1video -q:v 2 -r "$r" -c:a mp2 -b:a 224k -ar 44100 -f mpeg "$out.part" && mv "$out.part" "$out" && n=$((n + 1))
done < <(find "$GAME" \( -ipath '*/Zmovie/*.bin' -o -ipath '*/Movie/*.avi' \) -print0)
echo "converted $n movie(s)"
