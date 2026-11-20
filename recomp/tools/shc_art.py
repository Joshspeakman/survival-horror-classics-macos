#!/usr/bin/env python3
"""shc_art.py <game folder> <art folder> <re1|re2|re3|dc>: the launch window's art for one game, from the player's own
GOG folder, for the downloaded app's setup (recomp/setup: game.json "art"; install.sh does the same for an app built
from source): <id>.png, the game's GOG icon (goggame-*.ico, converted with macOS's sips), and <id>-banner.jpg, GOG's
store banner kept in the folder (app/webcache.zip: its largest picture). Either may be missing; the window then draws
the title. Standard library and sips only."""
import glob, os, shutil, subprocess, sys, zipfile

game, art, gid = sys.argv[1:4]
os.makedirs(art, exist_ok=True)
ico = sorted(glob.glob(os.path.join(game, "goggame-*.ico")) + glob.glob(os.path.join(game, "app", "goggame-*.ico")))
if ico:
    subprocess.run(["/usr/bin/sips", "-s", "format", "png", ico[0], "--out", os.path.join(art, gid + ".png")], capture_output=True)
wc = os.path.join(game, "app", "webcache.zip")
if os.path.exists(wc):
    with zipfile.ZipFile(wc) as z:
        jpgs = [i for i in z.infolist() if i.filename.lower().endswith(".jpg") and "logo" not in i.filename.lower()]
        if jpgs:
            big = max(jpgs, key=lambda i: i.file_size)
            with z.open(big) as src, open(os.path.join(art, gid + "-banner.jpg"), "wb") as dst:
                shutil.copyfileobj(src, dst)
print(" ".join(sorted(os.listdir(art))))
