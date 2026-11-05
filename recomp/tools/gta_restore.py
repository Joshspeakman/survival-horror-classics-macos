#!/usr/bin/env python3
"""gta_restore.py <gta3|vc|sa> <disc image or mounted disc> <game folder> <mod folder>

Restores, from the player's own original PC disc, the content later releases changed or removed, as a mod folder
(the Mods window's "Restore from Disc"; files at their game-folder paths, read in place of the game's own). Everything
stays on this Mac: nothing from the disc is part of this project.

San Andreas (the original v1.0 DVD, 2005; the Steam release is v3.0):
  audio/streams/*           the radio stations as released (v3.0 cut licensed songs from eight of them)
  audio/config/traklkup.dat their track index
  models/txd/loadscs.txd    the loading screens with the EAX and NVIDIA logos
  data/script/main.scm,     the mission scripts as released (v1.01 and later rebuilt them without the hidden
  data/script/script.img    girlfriend minigame's code; it ships switched off on the disc as well)
  models/gta3.img           the game's own archive with the 2005 downtown Los Santos shop fronts and signs restored
                            (civic06_lan.txd, downtown3_las.txd): an APFS clone of the game's copy, two entries rewritten
  <mod folder>-hot-coffee/  the switch for the hidden girlfriend minigame ("Hot Coffee"): the v1.0 main.scm with the
                            one byte that keeps it off changed (main.scm starts GF_SEX with 0, the censored scene, unless
                            a story flag is set; this passes 1, its interior scenes, as the 2005 PC mod did). Off in the
                            Mods window until switched on.
GTA III and Vice City: the Steam copies hold the discs' data unchanged (checked file by file), so nothing is restored.

Needs: hdiutil (macOS), unshield (brew install unshield) for the installer's .cab archives.
"""
import os, shutil, struct, subprocess, sys, tempfile

SA_FILES = ['audio/config/traklkup.dat', 'models/txd/loadscs.txd', 'data/script/main.scm', 'data/script/script.img']
SA_IMG_ENTRIES = ['civic06_lan.txd', 'downtown3_las.txd']

def say(*a): print(*a, flush=True)

def find_ci(root, rel):
    """root/rel, matching each component in any case; None if absent"""
    cur = root
    for part in rel.split('/'):
        try: names = os.listdir(cur)
        except OSError: return None
        m = [n for n in names if n.lower() == part.lower()]
        if not m: return None
        cur = os.path.join(cur, m[0])
    return cur

def mount(image):
    out = subprocess.run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify', image], capture_output=True, text=True)
    if out.returncode: sys.exit('the disc image could not be mounted: ' + out.stderr.strip())
    for line in out.stdout.splitlines():
        if '/Volumes/' in line: return line[line.index('/Volumes/'):].strip()
    sys.exit('the disc image mounted without a volume')

def img_dir(f):
    f.seek(0); h = f.read(8)
    if h[:4] != b'VER2': raise ValueError('not a San Andreas .img')
    n = struct.unpack('<I', h[4:8])[0]; raw = f.read(32 * n); d = {}
    for k in range(n):
        off, ss, sz, name = struct.unpack_from('<IHH24s', raw, 32 * k)
        d[name.split(b'\0')[0].decode('latin1').lower()] = (k, off, ss or sz)
    return d, n

def copy_clone(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if subprocess.run(['cp', '-c', src, dst]).returncode: shutil.copyfile(src, dst)   # APFS clone where it can be

def restore_sa(disc, game, mod):
    streams = find_ci(disc, 'audio/streams')
    if not streams: sys.exit('no audio/STREAMS on this disc: is it the San Andreas PC DVD?')
    cab = find_ci(disc, 'data1.cab')
    if not cab: sys.exit('no data1.cab on this disc')
    if not shutil.which('unshield'): sys.exit('unshield is needed for the installer archives: brew install unshield')
    os.makedirs(mod, exist_ok=True)
    say('== the radio stations')
    sdir = os.path.join(mod, 'audio', 'streams'); os.makedirs(sdir, exist_ok=True)
    for name in sorted(os.listdir(streams)):
        src = os.path.join(streams, name)
        if os.path.isfile(src): say('   ', name); shutil.copyfile(src, os.path.join(sdir, name))
    with tempfile.TemporaryDirectory(prefix='gta_restore_') as tmp:
        say('== the installer archive (unshield)')
        r = subprocess.run(['unshield', '-d', tmp, 'x', cab], capture_output=True, text=True)
        if r.returncode: sys.exit('unshield failed: ' + (r.stderr or r.stdout)[-400:])
        def from_cab(rel):                                       # the archive's groups are the game's folders (and Root_Data)
            return find_ci(tmp, rel) or find_ci(os.path.join(tmp, 'Root_Data'), rel)
        for rel in SA_FILES:
            p = from_cab(rel)
            if not p: sys.exit('the disc has no ' + rel)
            dst = os.path.join(mod, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(p, dst); say('   ', rel)
        say('== models/gta3.img: the original downtown signage')
        disc_img = from_cab('models/gta3.img'); game_img = find_ci(game, 'models/gta3.img')
        if not disc_img or not game_img: sys.exit('gta3.img is missing on the disc or in the game folder')
        out = os.path.join(mod, 'models', 'gta3.img'); copy_clone(game_img, out)
        with open(disc_img, 'rb') as df, open(out, 'r+b') as of:
            dd, _ = img_dir(df); od, n = img_dir(of)
            for name in SA_IMG_ENTRIES:
                if name not in dd or name not in od: sys.exit('gta3.img has no ' + name)
                _, doff, dsec = dd[name]; k, ooff, osec = od[name]
                df.seek(doff * 2048); data = df.read(dsec * 2048)
                if dsec <= osec: at = ooff                       # in its own place
                else:                                            # longer: at the end, the directory pointed there
                    of.seek(0, 2); at = (of.tell() + 2047) // 2048
                of.seek(at * 2048); of.write(data)
                if at == ooff and dsec < osec: of.write(b'\0' * ((osec - dsec) * 2048))   # the rest of its old space cleared
                of.seek(8 + 32 * k); of.write(struct.pack('<IHH', at, dsec, 0))
                say('   ', name, '%d sectors%s' % (dsec, '' if at == ooff else ' (moved to the end)'))
    hot_coffee(mod)
    say('== done:', mod)

# v1.0 main.scm: the second GF_SEX launch (0913 run_external_script 26 $GF 0 ...) at this offset, its 0 the switch
HC_AT = 0x22f86
HC_EXPECT = bytes.fromhex('130904 1a 021406 0400'.replace(' ', ''))
def hot_coffee(mod):
    src = os.path.join(mod, 'data', 'script', 'main.scm')
    d = bytearray(open(src, 'rb').read())
    if bytes(d[HC_AT:HC_AT + len(HC_EXPECT)]) != HC_EXPECT:
        say('   (not the v1.0 script this switch knows: no Hot Coffee switch)'); return
    d[HC_AT + len(HC_EXPECT) - 1] = 1
    out = os.path.join(mod + '-hot-coffee', 'data', 'script', 'main.scm'); os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'wb').write(d); say('== the Hot Coffee switch:', mod + '-hot-coffee')

def main():
    if len(sys.argv) != 5: sys.exit(__doc__)
    game_id, src, game, mod = sys.argv[1:]
    if game_id in ('gta3', 'vc'):
        say('Nothing to restore: the Steam release of this game holds the original disc data unchanged.'); return 0
    if game_id != 'sa': sys.exit('unknown game ' + game_id)
    mounted = None
    if os.path.isfile(src): mounted = mount(src); disc = mounted
    else: disc = src
    try: restore_sa(disc, game, mod)
    finally:
        if mounted: subprocess.run(['hdiutil', 'detach', '-quiet', mounted])
    return 0

if __name__ == '__main__': sys.exit(main())
