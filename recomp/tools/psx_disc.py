#!/usr/bin/env python3
"""psx_disc.py: reads the player's own PlayStation disc images (docs/PLAYSTATION-IMPORT.md), on this Mac only.

  psx_disc.py info <image>              the disc's serial and title line (SYSTEM.CNF), its tracks, its sector format
  psx_disc.py ls <image>                every file: path, size, first sector, and whether it is XA (interleaved audio or
                                        movie sectors, which a 2048-byte .iso cannot hold)
  psx_disc.py extract <image> <folder>  every file: ordinary files as their data; XA files as raw 2352-byte sectors
                                        (what ffmpeg's psxstr reads for STR movies and XA audio)

Images: .cue (with its .bin), a lone .bin (2352-byte sectors), .chd (unpacked with chdman, Homebrew's rom-tools), and
.iso (2048-byte sectors: data files only). The disc's own ISO 9660 file system is read directly; nothing is mounted.
"""
import os, re, shutil, struct, subprocess, sys, tempfile

SYNC = b'\x00' + b'\xff' * 10 + b'\x00'

class Disc:
    """the data track of a disc image: sector(n) -> 2352 raw bytes (2048-byte images: synthesised as Mode 2 Form 1)"""
    def __init__(self, path):
        self.path, self.tmp = path, None
        ext = os.path.splitext(path)[1].lower()
        if ext == '.chd': path = self._unchd(path); ext = '.cue'
        self.tracks = []
        if ext == '.cue': bin_path, self.ssize, self.offset, self.tracks = self._cue(path)
        else: bin_path, self.offset = path, 0
        self.f = open(bin_path, 'rb')
        if ext != '.cue': self.ssize = self._guess_size()
        self.raw = self.ssize == 2352

    def _unchd(self, chd):
        tool = shutil.which('chdman') or next((p for p in ('/opt/homebrew/bin/chdman', '/usr/local/bin/chdman') if os.path.exists(p)), None)
        if not tool: sys.exit('a .chd needs chdman: brew install rom-tools')
        self.tmp = tempfile.mkdtemp(prefix='psx-chd-'); cue = os.path.join(self.tmp, 'disc.cue')
        r = subprocess.run([tool, 'extractcd', '-i', chd, '-o', cue, '-ob', os.path.join(self.tmp, 'disc.bin')], capture_output=True, text=True)
        if r.returncode: sys.exit('chdman could not unpack the image: ' + (r.stderr or r.stdout).strip()[-300:])
        return cue

    def _cue(self, cue):
        base = os.path.dirname(cue); tracks = []; cur_file = None
        for line in open(cue, encoding='latin1'):
            w = line.strip()
            m = re.match(r'FILE\s+"?(.*?)"?\s+\w+$', w, re.I)
            if m: cur_file = os.path.join(base, m.group(1)); continue
            m = re.match(r'TRACK\s+(\d+)\s+(\S+)', w, re.I)
            if m: tracks.append({'n': int(m.group(1)), 'mode': m.group(2).upper(), 'file': cur_file, 'index': None}); continue
            m = re.match(r'INDEX\s+01\s+(\d+):(\d+):(\d+)', w, re.I)
            if m and tracks: mm, ss, ff = map(int, m.groups()); tracks[-1]['index'] = (mm * 60 + ss) * 75 + ff
        data = next((t for t in tracks if t['mode'] != 'AUDIO'), None)
        if not data: sys.exit('the .cue has no data track')
        if not os.path.exists(data['file']):                       # a .cue naming its .bin in another case or spelling
            cands = [n for n in os.listdir(base) if n.lower().endswith('.bin')]
            if len(cands) == 1: data['file'] = os.path.join(base, cands[0])
            else: sys.exit('the .bin the .cue names is missing: ' + data['file'])
        size = 2352 if data['mode'] in ('MODE2/2352', 'MODE1/2352') else 2336 if data['mode'] == 'MODE2/2336' else 2048
        return data['file'], size, (data['index'] or 0) * size, tracks

    def _guess_size(self):
        self.f.seek(0); head = self.f.read(16 * 2352 + 16)
        if head[:12] == SYNC: return 2352
        self.f.seek(16 * 2048); return 2048 if self.f.read(6)[1:6] == b'CD001' else 2352

    def sector(self, n):
        if self.ssize == 2352: self.f.seek(self.offset + n * 2352); return self.f.read(2352)
        if self.ssize == 2336: self.f.seek(self.offset + n * 2336); return SYNC + b'\0\0\0\x02' + self.f.read(2336)
        self.f.seek(self.offset + n * 2048); d = self.f.read(2048)              # cooked: as Mode 2 Form 1
        return SYNC + b'\0\0\0\x02' + b'\0\0\x08\0\0\0\x08\0' + d + b'\0' * 280

    def data(self, n):
        """a sector's user data: 2048 bytes (Form 1) or 2324 (Form 2)"""
        s = self.sector(n)
        if len(s) < 2352: return b''
        if s[15] == 1: return s[16:16 + 2048]                          # Mode 1
        form2 = s[18] & 0x20
        return s[24:24 + 2324] if form2 else s[24:24 + 2048]

    def close(self):
        self.f.close()
        if self.tmp: shutil.rmtree(self.tmp, ignore_errors=True)

def walk(disc):
    """every file of the ISO 9660 file system: (path, first sector, size, xa) where xa says its sectors are interleaved
    or Form 2 (XA audio, STR movies)"""
    pvd = disc.data(16)
    if pvd[1:6] != b'CD001': sys.exit('no ISO 9660 file system on this image (not a PlayStation data disc?)')
    root = pvd[156:156 + 34]; out = []
    def rec(lba, size, prefix, seen):
        if lba in seen: return
        seen.add(lba); buf = b''.join(disc.data(lba + k)[:2048] for k in range((size + 2047) // 2048)); p = 0
        while p < len(buf):
            n = buf[p]
            if n == 0: p = (p // 2048 + 1) * 2048; continue
            r = buf[p:p + n]; p += n
            ext, dlen, flags, nlen = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0], r[25], r[32]
            name = r[33:33 + nlen]
            if name in (b'\0', b'\1'): continue
            name = name.decode('latin1').split(';')[0]
            su = r[33 + nlen + (1 - nlen % 2):]                         # system use: the XA record
            xa = len(su) >= 14 and su[6:8] == b'XA' and bool(struct.unpack('>H', su[4:6])[0] & 0x3000)
            path = prefix + name
            if flags & 2: rec(ext, dlen, path + '/', seen)
            else: out.append((path, ext, dlen, xa))
    rec(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '', set())
    return out

def read_file(disc, lba, size):
    out = bytearray(); k = 0
    while len(out) < size: out += disc.data(lba + k)[:2048]; k += 1
    return bytes(out[:size])

def system_cnf(disc, files):
    f = next((x for x in files if x[0].upper() == 'SYSTEM.CNF'), None)
    if not f: return None, ''
    t = read_file(disc, f[1], f[2]).decode('latin1', 'replace')
    m = re.search(r'BOOT\s*=\s*cdrom:\\?\\?([A-Z]{4})[_-]?(\d{3})\.?(\d{2})', t, re.I)
    return (f'{m.group(1).upper()}-{m.group(2)}{m.group(3)}' if m else None), t

def main(argv):
    if len(argv) < 2 or argv[0] not in ('info', 'ls', 'extract'): sys.exit(__doc__)
    disc = Disc(argv[1])
    try:
        files = walk(disc); serial, cnf = system_cnf(disc, files)
        if argv[0] == 'info':
            print(f'serial {serial or "?"}  sectors {disc.ssize}-byte{"" if disc.raw else " (no XA audio or movies: a BIN/CUE or CHD keeps them)"}')
            print(f'tracks {", ".join(t["mode"] for t in disc.tracks) or "one"}; {len(files)} files, {sum(1 for f in files if f[3])} XA')
            print(cnf.strip())
        elif argv[0] == 'ls':
            for p, lba, size, xa in files: print(f'{lba:7d} {size:10d} {"XA" if xa else "  "} {p}')
        else:
            out = argv[2] if len(argv) > 2 else sys.exit('extract: which folder?')
            for p, lba, size, xa in files:
                dst = os.path.join(out, *p.split('/')); os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as w:
                    if xa and disc.raw:                                 # raw sectors, as ffmpeg's psxstr wants them
                        for k in range((size + 2047) // 2048): w.write(disc.sector(lba + k))
                    else: w.write(read_file(disc, lba, size))
            print(f'{len(files)} files from {serial or "?"} into {out}')
    finally: disc.close()

if __name__ == '__main__':
    main(sys.argv[1:])
