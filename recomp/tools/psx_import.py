#!/usr/bin/env python3
"""psx_import.py: PlayStation content from the player's own discs, as mods (docs/PLAYSTATION-IMPORT.md). This Mac only;
nothing from a disc is part of the project.

  psx_import.py voices <re1|re2|re3|dc> <disc .cue>[,<disc 2 .cue>] <game folder> <mod folder> [--report <file>]
      (Resident Evil 3: <game folder> is the voices unpacked from its archives by the game's own reader, re_mods.c's
      W32_ROFSDUMP, since the PC keeps them packed)
      The disc's voice lines (XA, 37.8 kHz) in place of the PC's (22 kHz). Each XA channel of the disc's voice files
      is decoded whole (ffmpeg): a channel holds many lines one after another, which the game finds by position. Each
      PC clip is then looked for in every channel by the shape of its loudness over time (a sliding comparison, by
      FFT, at 10 ms and then 1 ms), and the disc's audio at that place, the PC clip's own length, is written as a
      16-bit 44.1 kHz WAV at the PC clip's path below <mod folder>, at the PC clip's loudness: the game's timing and
      mix stay as they were. A match is confirmed on the waveform itself (above 0.9); PC clips with none keep the
      PC's (reported).

  psx_import.py movies re1 <disc .cue>[,<another disc .cue>] <game folder> <mod folder> [--report <file>]
      The disc's movies (MDEC, 15 fps, 37.8 kHz sound) in place of the PC's (Resident Evil: Cinepak, 10 fps, 22 kHz),
      each converted to MPEG-1 (what the port's player decodes; 30 fps, each frame shown twice) as "<PC movie>.mpg"
      below <mod folder>, which the port plays in its place (amstream.inc, dshow.c). The discs are tried in the order
      given (Resident Evil: the Ultimate Director's Cut first, for its uncut colour opening and endings, then the
      original release for the staff rolls it lacks). Each pairing is checked frame by frame against the PC's movie
      (the same scene: thumbnails at a quarter, half and three quarters through); one that differs is left out.

Needs: ffmpeg (Homebrew), numpy (the project's recomp/.venv). Run with recomp/.venv/bin/python.
"""
import os, struct, subprocess, sys, tempfile, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psx_disc

FFMPEG = next((p for p in ('/opt/homebrew/bin/ffmpeg', '/usr/local/bin/ffmpeg') if os.path.exists(p)), 'ffmpeg')
ENV_HZ = 100                                    # loudness envelope: 10 ms steps

# per game: the disc's voice files, the PC's voice clips (folders below the game folder, searched below too, and their
# extension) and how a replacement is stored: 'wav' (RIFF WAVE, 16-bit 44.1 kHz, under the PC clip's name) or 'sap'
# (Resident Evil 2: an 8-byte header, 01 00 00 00 00 00 00 00, before the RIFF WAVE)
GAMES = {
    're1': {'disc': lambda p: p.upper().startswith('PSX/VOICE/') and p.upper().endswith('.XAS'),
            'pc': [('USA/Voice', '.wav')], 'store': 'wav'},
    're2': {'disc': lambda p: '/VOICE/' in p.upper() and p.upper().endswith('.XAS'),
            'pc': [('Pl0/Voice', '.sap'), ('Pl1/Voice', '.sap')], 'store': 'sap'},
    're3': {'disc': lambda p: p.upper().startswith('CD_DATA/') and p.upper().endswith('.XAS'),
            'pc': [('data_a/voice', '.wav')], 'store': 'wav'},      # <game folder>: the game's own unpacked voices
                                                                     # (re_mods.c W32_ROFSDUMP), as the archive names them
    'dc':  {'disc': lambda p: p.upper().startswith('PSX/DATA_XA/') and p.upper().endswith('.XAS'),
            'pc': [('Sound/VOICE', '.dat')], 'store': 'wav'},
}

def say(*a): print(*a, flush=True)

def xa_channels(disc, lba, size):
    """an XA file's audio sectors, per channel: {channel: [raw sectors]}"""
    chans = {}
    for k in range((size + 2047) // 2048):
        s = disc.sector(lba + k)
        if len(s) < 2352: break
        if s[18] & 0x04: chans.setdefault(s[17], []).append(s)           # audio
    return chans

def decode_xa(sectors, rate=44100):
    """raw XA sectors -> mono float samples at rate (ffmpeg)"""
    with tempfile.NamedTemporaryFile(suffix='.str', delete=False) as t:
        for s in sectors: t.write(s)
        name = t.name
    try:
        r = subprocess.run([FFMPEG, '-v', 'error', '-f', 'psxstr', '-i', name, '-ac', '1', '-ar', str(rate), '-f', 's16le', '-'],
                           capture_output=True)
        return np.frombuffer(r.stdout, dtype='<i2').astype(np.float32) / 32768.0
    finally: os.unlink(name)

def read_pc(path):
    """a PC clip (RIFF WAVE in any format ffmpeg reads, or Resident Evil 2's .SAP around one) -> (mono float, rate)"""
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' and data[8:12] == b'RIFF': data = data[8:]                    # .SAP
    rate = struct.unpack_from('<I', data, 24)[0] if data[:4] == b'RIFF' and len(data) >= 28 else 22050
    if data[:4] == b'RIFF' and struct.unpack_from('<H', data, 20)[0] == 1:
        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as t: t.write(data); name = t.name
        try: return read_wav(name)
        finally: os.unlink(name)
    r = subprocess.run([FFMPEG, '-v', 'error', '-f', 'wav', '-i', 'pipe:0', '-ac', '1', '-f', 's16le', '-'], input=data, capture_output=True)
    return np.frombuffer(r.stdout, dtype='<i2').astype(np.float32) / 32768.0, rate

def read_wav(path):
    """a PCM WAV -> (mono float samples, rate)"""
    with wave.open(path, 'rb') as w:
        n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if width == 1: a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128) / 128
    elif width == 2: a = np.frombuffer(raw, dtype='<i2').astype(np.float32) / 32768
    else: raise ValueError('unsupported sample width')
    if ch > 1: a = a.reshape(-1, ch).mean(axis=1)
    return a, rate

def envelope(a, rate):
    """loudness over time: RMS per 10 ms, as a log level"""
    step = max(1, int(rate / ENV_HZ)); n = len(a) // step
    if n == 0: return np.zeros(1, np.float32)
    e = np.sqrt((a[:n * step].reshape(n, step) ** 2).mean(axis=1) + 1e-9)
    return np.log10(e + 1e-4).astype(np.float32)

def ncc(long, short):
    """normalised cross-correlation of short at every offset into long (FFT): an array of len(long) - len(short) + 1"""
    n, m = len(long), len(short)
    if m < 2 or n < m: return np.full(1, -1.0)
    s = short - short.mean(); ss = np.sqrt((s ** 2).sum())
    if ss < 1e-9: return np.full(n - m + 1, -1.0)
    size = 1 << int(np.ceil(np.log2(n + m)))
    dot = np.fft.irfft(np.fft.rfft(long, size) * np.conj(np.fft.rfft(s, size)), size)[:n - m + 1]
    c1 = np.concatenate([[0], np.cumsum(long, dtype=np.float64)]); c2 = np.concatenate([[0], np.cumsum(long.astype(np.float64) ** 2)])
    win = c1[m:] - c1[:-m]; win2 = c2[m:] - c2[:-m]
    var = np.maximum(win2 - win ** 2 / m, 1e-12)
    return dot / (np.sqrt(var) * ss)

def write_wav(path, a, rate=44100, store='wav'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = (np.clip(a, -1, 1) * 32767).astype('<i2')
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as t: name = t.name
    with wave.open(name, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())
    body = open(name, 'rb').read(); os.unlink(name)
    with open(path, 'wb') as f: f.write((b'\x01\0\0\0\0\0\0\0' if store == 'sap' else b'') + body)

def find_ci(root, rel):
    cur = root
    for part in rel.split('/'):
        try: m = [n for n in os.listdir(cur) if n.lower() == part.lower()]
        except OSError: return None
        if not m: return None
        cur = os.path.join(cur, m[0])
    return cur

def voices(game, cue, game_dir, mod, report=None):
    g = GAMES[game]
    # the PC's clips
    pcs = []
    for folder, ext in g['pc']:
        d = find_ci(game_dir, folder)
        for root, _, names in os.walk(d) if d else []:
            for n in sorted(names):
                if not n.lower().endswith(ext): continue
                rel = os.path.relpath(os.path.join(root, n), game_dir).replace(os.sep, '/')
                a, rate = read_pc(os.path.join(root, n))
                if len(a): pcs.append({'rel': rel, 'a': a, 'rate': rate, 'dur': len(a) / rate, 'env': envelope(a, rate)})
    say(f'{len(pcs)} PC voice clips')
    # the disc's channels, each decoded whole
    chans = []
    for cue_one in cue.split(','):
        disc = psx_disc.Disc(cue_one)
        try:
            for path, lba, size, xa in psx_disc.walk(disc):
                if not g['disc'](path): continue
                for ch, secs in sorted(xa_channels(disc, lba, size).items()):
                    a = decode_xa(secs)
                    if len(a) >= 4410: chans.append({'src': f'{os.path.basename(cue_one)}:{path}#{ch}', 'a': a, 'env': envelope(a, 44100)})
                say(f'{path}: {sum(1 for c in chans if path in c["src"])} channels')
        finally: disc.close()
    say(f'{sum(len(c["a"]) for c in chans) / 44100 / 60:.1f} minutes of disc voice audio')
    # each PC clip: the best few places in any channel by loudness (10 ms), each confirmed on the waveform itself (the
    # disc's audio there, cut down to the PC's rate, against the PC clip): the best above 0.9 is the line, placed to
    # the sample
    matched, lost = [], []
    for p in pcs:
        if len(p['env']) < 10 or p['env'].std() < 0.05: lost.append((p['rel'], 'silent or too short')); continue
        cands = []
        for k, c in enumerate(chans):
            r = ncc(c['env'], p['env'])
            for _ in range(3):                                          # this channel's three best places, apart
                j = int(np.argmax(r))
                if r[j] < 0.6: break
                cands.append((float(r[j]), k, j)); r[max(0, j - 50):j + 50] = -2
        cands.sort(reverse=True)
        best = (0.0, None, 0)
        for score, k, j in cands[:8]:
            corr, start = confirm(chans[k]['a'], j * 441, p['a'], p['rate'])
            if corr > best[0]: best = (corr, k, start)
        corr, k, start = best
        if corr < 0.9: lost.append((p['rel'], f'best waveform match {corr:.2f}')); continue
        c = chans[k]; n = int(round(len(p['a']) * 44100 / p['rate']))
        seg = c['a'][max(0, start):max(0, start) + n]
        if start < 0: seg = np.concatenate([np.zeros(-start, np.float32), seg])
        if len(seg) < n: seg = np.concatenate([seg, np.zeros(n - len(seg), np.float32)])
        seg = seg[:n] * (rms(p['a']) / max(rms(seg[:n]), 1e-6))           # at the PC clip's loudness: the game's mix
        write_wav(os.path.join(mod, *p['rel'].lower().split('/')), seg, store=g['store'])
        matched.append((corr, p['rel'], c['src'], start / 44.1))
    scores = [m[0] for m in matched]
    say(f'matched {len(matched)} of {len(pcs)} PC clips; agreement: lowest {min(scores, default=0):.3f}, '
        f'median {float(np.median(scores)) if scores else 0:.3f}; kept the PC\'s: {len(lost)}')
    if report:
        with open(report, 'w') as r:
            for s_, rel, src, off in sorted(matched, key=lambda m: m[1]): r.write(f'{s_:.3f}  {rel}  <-  {src} at {off / 1000:.3f} s\n')
            for rel, why in lost: r.write(f'  -    {rel}  (kept: {why})\n')
    return len(matched), len(pcs)

def confirm(chan, near, pc, pc_rate):
    """the disc's audio around sample near (44.1 kHz), low-passed and brought to the PC's rate, cross-correlated with the
    PC clip: (correlation, where the clip starts in the channel, in 44.1 kHz samples)"""
    pad = 8820; lo = max(0, near - pad); seg = chan[lo:near + int(len(pc) * 44100 / pc_rate) + pad]
    if len(seg) < 64: return 0.0, 0
    S = np.fft.rfft(seg); S[np.fft.rfftfreq(len(seg), 1 / 44100) > pc_rate / 2 * 0.95] = 0
    low = np.fft.irfft(S, len(seg)); m = int(len(seg) * pc_rate / 44100)
    d = np.interp(np.arange(m) / pc_rate, np.arange(len(seg)) / 44100, low).astype(np.float32)
    if len(d) < len(pc): return 0.0, 0
    r = ncc(d, pc); j = int(np.argmax(r))
    return float(r[j]), lo + int(round(j * 44100 / pc_rate))

def rms(a): return float(np.sqrt((np.asarray(a, np.float64) ** 2).mean())) if len(a) else 0.0

def fine_env(a, rate):
    """loudness per 1 ms (for placing a match exactly)"""
    step = max(1, int(rate / 1000)); n = len(a) // step
    if n == 0: return np.zeros(1, np.float32)
    return np.log10(np.sqrt((a[:n * step].reshape(n, step) ** 2).mean(axis=1) + 1e-9) + 1e-4).astype(np.float32)

# the PC's movies and the disc's names for them (Resident Evil: the US PC names its opening and two endings by region)
MOVIES = {
    're1': {'pc': 'USA/Movie', 'ext': '.avi', 'disc_dirs': ('PSXUSA/ZMOVIE/', 'PSX/MOVIE/'),
            'names': {'pu': 'pj', 'ou': 'oj', 'eu4': 'ed4', 'eu5': 'ed5'},
            'keep': ('staf_r', 'stfc_r', 'stfj_r', 'stfz_r')},   # the PC's staff rolls carry the credits in the picture;
                                                                 # the PlayStation drew them over its rolls in the game
}

def frames(path, fmt=None, fps=4):
    """a movie as grey 32x24 frames, fps a second: an array (frames, 768)"""
    args = [FFMPEG, '-v', 'error'] + (['-f', fmt] if fmt else []) + ['-i', path, '-vf', f'fps={fps},scale=32:24,format=gray', '-f', 'rawvideo', '-']
    r = subprocess.run(args, capture_output=True); n = len(r.stdout) // 768
    return np.frombuffer(r.stdout[:n * 768], np.uint8).astype(np.float32).reshape(n, 768)

def same_scene(a, b, fps=4, max_shift=3.0):
    """how alike two movies look: the mean frame correlation at the best offset within +-max_shift seconds"""
    if not len(a) or not len(b): return 0.0
    def z(m): m = m - m.mean(1, keepdims=True); return m / (np.linalg.norm(m, axis=1, keepdims=True) + 1e-6)
    a, b = z(a), z(b); best = -1.0
    for sh in range(-int(max_shift * fps), int(max_shift * fps) + 1):
        x, y = (a[sh:], b) if sh >= 0 else (a, b[-sh:])
        n = min(len(x), len(y))
        if n < max(4, min(len(a), len(b)) // 2): continue
        best = max(best, float((x[:n] * y[:n]).sum(1).mean()))
    return best

def duration(path, fmt=None):
    args = ['ffprobe' if not FFMPEG.endswith('ffmpeg') else FFMPEG[:-6] + 'ffprobe', '-v', 'error'] + (['-f', fmt] if fmt else []) + \
           ['-show_entries', 'format=duration', '-of', 'csv=p=0', path]
    r = subprocess.run(args, capture_output=True, text=True)
    try: return float(r.stdout.strip())
    except ValueError: return 0.0

def movies(game, cues, game_dir, mod, report=None):
    g = MOVIES[game]; d = find_ci(game_dir, g['pc']); lines = []
    pcs = sorted(n for n in os.listdir(d) if n.lower().endswith(g['ext']))
    discs = [psx_disc.Disc(c) for c in cues.split(',')]
    try:
        tables = [{p.upper(): (p, lba, size) for p, lba, size, xa in psx_disc.walk(dd)} for dd in discs]
        for name in pcs:
            stem = name[:-len(g['ext'])].lower(); want = g['names'].get(stem, stem).upper()
            if stem in g.get('keep', ()): lines.append(f'  -    {name}: the PC\'s (kept)'); continue
            src = None
            for dd, t in zip(discs, tables):
                for prefix in g['disc_dirs']:
                    e = t.get(prefix + want + '.STR')
                    if e: src = (dd, e); break
                if src: break
            if not src: lines.append(f'  -    {name}: not on these discs (kept)'); continue
            dd, (path, lba, size) = src
            with tempfile.NamedTemporaryFile(suffix='.str', delete=False) as tmp:
                for k in range((size + 2047) // 2048):
                    sec = dd.sector(lba + k)
                    if len(sec) < 2352: break
                    tmp.write(sec)
                raw = tmp.name
            try:
                pc_path = os.path.join(d, name); dpc = duration(pc_path); dps = (size // 2048) / 150.0
                score = same_scene(frames(pc_path), frames(raw, 'psxstr'))
                if score < 0.6:
                    lines.append(f'  -    {name}: {os.path.basename(cues.split(",")[discs.index(dd)])} {path} looks different ({score:.2f}): kept'); continue
                out = os.path.join(mod, *g['pc'].lower().split('/'), name.lower() + '.mpg'); os.makedirs(os.path.dirname(out), exist_ok=True)
                r = subprocess.run([FFMPEG, '-nostdin', '-v', 'error', '-y', '-f', 'psxstr', '-i', raw, '-c:v', 'mpeg1video', '-q:v', '2', '-r', '30',
                                    '-c:a', 'mp2', '-b:a', '224k', '-ar', '44100', '-f', 'mpeg', out + '.part'], capture_output=True)
                if r.returncode: lines.append(f'  -    {name}: conversion failed: {r.stderr.decode()[-200:]}'); continue
                os.replace(out + '.part', out)
                lines.append(f'{score:.2f}  {name}  <-  {path} ({dps:.1f} s; PC {dpc:.1f} s)')
            finally: os.unlink(raw)
    finally:
        for dd in discs: dd.close()
    for l in lines: say(l)
    if report: open(report, 'w').write('\n'.join(lines) + '\n')

def main(argv):
    if len(argv) >= 5 and argv[0] == 'movies' and argv[1] in MOVIES:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        movies(argv[1], argv[2], argv[3], argv[4], rep)
    elif len(argv) >= 5 and argv[0] == 'voices' and argv[1] in GAMES:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        voices(argv[1], argv[2], argv[3], argv[4], rep)
    else: sys.exit(__doc__)

if __name__ == '__main__':
    main(sys.argv[1:])
