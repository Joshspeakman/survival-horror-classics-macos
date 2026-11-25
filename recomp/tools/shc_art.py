#!/usr/bin/env python3
"""shc_art.py <game folder> <art folder> <re1|re2|re3|dc>: the launch window's art for one game, from the player's own
GOG folder, for the downloaded app's setup (recomp/setup: game.json "art"; install.sh does the same for an app built
from source): <id>.png, the game's GOG icon (goggame-*.ico, converted with macOS's sips), and <id>-banner.jpg, GOG's
store banner kept in the folder (app/webcache.zip: its largest picture). Either may be missing; the window then draws
the title. With --store (or alone with --store-only): GOG's art for the game (the product named by the folder's
goggame-<id>.info): <id>-store-portrait.jpg (the box, the window's framed portrait) and <id>-shot-<n>.jpg (the store
page's screenshots, the backdrop's slides); skipped offline. Standard library and sips only."""
import glob, json, os, shutil, subprocess, sys, urllib.request, zipfile

args = [a for a in sys.argv[1:] if not a.startswith("--")]
game, art, gid = args[0:3]
os.makedirs(art, exist_ok=True)

def store():
    info = sorted(glob.glob(os.path.join(game, "goggame-*.info")))
    if not info: return
    pid = os.path.basename(info[0])[len("goggame-"):-len(".info")]
    get = lambda u: urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0"}), timeout=20).read()
    try: d = json.loads(get("https://api.gog.com/v2/games/" + pid))
    except Exception: return
    want = {gid + "-store-portrait.jpg": ((d.get("_links") or {}).get("boxArtImage") or {}).get("href")}
    for k, sh in enumerate((d.get("_embedded") or {}).get("screenshots", [])[:6]):
        href = (((sh.get("_links") or {}).get("self")) or {}).get("href", "")
        if href: want["%s-shot-%d.jpg" % (gid, k + 1)] = href.replace("{formatter}", "1600")
    for name, url in want.items():
        out = os.path.join(art, name)
        if not url or os.path.exists(out): continue
        try: data = get(url)
        except Exception: continue
        if len(data) > 15000 and data[:3] == b"\xff\xd8\xff":
            with open(out + ".part", "wb") as f: f.write(data)
            os.replace(out + ".part", out)

if "--store-only" in sys.argv:
    store(); sys.exit(0)
if "--store" in sys.argv: store()
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
