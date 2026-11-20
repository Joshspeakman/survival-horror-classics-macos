#!/usr/bin/env python3
"""convert_movies.py <game folder>: MPEG-1 copies of the movies macOS cannot decode, beside them as "<file>.mpg", which
the runtime plays in their place (amstream.inc, dshow.c): Resident Evil 2's Indeo 5 AVIs (Zmovie/*.bin) and Resident
Evil's Cinepak AVIs (USA/Movie/*.avi). The same as convert_movies.sh, for the downloaded app's setup (recomp/setup:
game.json "prepare"), where ffmpeg is the app's own (Resources/bin, tools/build_ffmpeg.sh) on PATH. The originals are
left as they are; already converted files are kept. Standard library only."""
import os, shutil, subprocess, sys

game = sys.argv[1]
ff = shutil.which("ffmpeg") or next((p for p in ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg") if os.path.exists(p)), None)
if not ff:
    sys.exit("ffmpeg is needed to convert this game's movies")
probe = os.path.join(os.path.dirname(ff), "ffprobe")
RATES = {"24000/1001", "24/1", "25/1", "30000/1001", "30/1", "50/1", "60000/1001", "60/1"}   # MPEG-1's own frame rates

movies = []
for base, dirs, names in os.walk(game):
    folder = os.path.basename(base).lower()
    for n in names:
        if (folder == "zmovie" and n.lower().endswith(".bin")) or (folder == "movie" and n.lower().endswith(".avi")):
            movies.append(os.path.join(base, n))
n = 0
for f in sorted(movies):
    out = f + ".mpg"
    if os.path.exists(out) and os.path.getsize(out) > 0:
        continue
    r = subprocess.run([probe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate", "-of", "csv=p=0", f],
                       capture_output=True, text=True)
    rate = r.stdout.strip() if r.returncode == 0 and r.stdout.strip() in RATES else "30"
    # MPEG-1 video at a high quality, MP2 sound; the frame rate as it is unless MPEG-1 has no such rate (then 30)
    r = subprocess.run([ff, "-nostdin", "-v", "error", "-y", "-i", f, "-c:v", "mpeg1video", "-q:v", "2", "-r", rate,
                        "-c:a", "mp2", "-b:a", "224k", "-ar", "44100", "-f", "mpeg", out + ".part"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"ffmpeg could not convert {os.path.basename(f)}: " + r.stderr.strip()[-200:])
    os.replace(out + ".part", out)
    n += 1
print(f"converted {n} movie(s)")
