#!/usr/bin/env python3
"""gta_art.py <gta3|vc|sa> <art folder> [--no-downloads]

The launch window's art for one of the games (install.sh), written to the given folder (never to this project), from
the game's Steam store page. The Windows store pages are used (GTA III 12100, Vice City 12110, San Andreas 12120): the
Mac releases' own pages (12230, 12240, 12250) carry the same art but not all of it.
  logo.png              the game's logo with transparency
  hero.jpg              Steam's library hero, across the top of the window
  shot1-3.jpg           three of the store page's screenshots (shot1 is the game's card in the launch window)
Chosen screenshots are asked for by name; if one has gone, the store's screenshot list (appdetails) fills the gap.
--no-downloads (or no network): nothing is fetched and the window writes the titles instead. Standard library only.
"""
import json, os, sys, urllib.request

GAMES = {   # app id, the screenshots chosen for the launch window (shot1 = the card)
    'gta3': (12100, ['0000003292', '0000003296', '0000003295']),     # Liberty City
    'vc':   (12110, ['0000003315', '0000003313', '0000003316']),     # Vice City: the skyline, Tommy, Ocean Drive
    'sa':   (12120, ['0000003304', '0000003308', '0000003306']),     # Los Santos at sunset, Grove Street, the country
}
CDN = 'https://cdn.cloudflare.steamstatic.com/steam/apps/%d/%s'
SHOT = 'https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%d/%s.600x338.jpg'
JPG, PNG = b'\xff\xd8', b'\x89PNG'

def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'gta-classics-installer'})
    with urllib.request.urlopen(req, timeout=20) as r: return r.read()

def download(url, path, magic):
    try: d = fetch(url)
    except Exception as e:
        print('  %s: %s' % (os.path.basename(path), e), file=sys.stderr); return False
    if not d.startswith(magic) or len(d) < 5000: return False
    with open(path + '.part', 'wb') as f: f.write(d)
    os.replace(path + '.part', path); return True

def store_shots(app):   # the store page's screenshot list, as names usable in SHOT
    try: d = json.loads(fetch('https://store.steampowered.com/api/appdetails?appids=%d&filters=screenshots' % app))
    except Exception as e:
        print('  appdetails: %s' % e, file=sys.stderr); return []
    shots = (d.get(str(app)) or {}).get('data', {}).get('screenshots', [])
    return [os.path.basename(s.get('path_thumbnail', '')).split('.')[0] for s in shots if s.get('path_thumbnail')]

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) < 2 or args[0] not in GAMES: sys.exit(__doc__)
    app, chosen = GAMES[args[0]]; out = args[1]; os.makedirs(out, exist_ok=True)
    if '--no-downloads' in sys.argv[1:]: return 0
    have = lambda n: os.path.exists(os.path.join(out, n))
    got = [n for n, u, m in (('logo.png', CDN % (app, 'logo.png'), PNG), ('hero.jpg', CDN % (app, 'library_hero.jpg'), JPG))
           if have(n) or download(u, os.path.join(out, n), m)]
    spare = None
    for k in range(3):
        n = 'shot%d.jpg' % (k + 1); p = os.path.join(out, n)
        if not have(n) and not download(SHOT % (app, chosen[k]), p, JPG):
            if spare is None: spare = [s for s in store_shots(app) if s not in chosen]   # a chosen one has gone
            while spare and not download(SHOT % (app, spare.pop(0)), p, JPG): pass
        if have(n): got.append(n)
    return 0 if got else 1

if __name__ == '__main__': sys.exit(main())
