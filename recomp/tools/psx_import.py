#!/usr/bin/env python3
"""psx_import.py: PlayStation content from the player's own discs, as mods (docs/PLAYSTATION-IMPORT.md). This Mac only;
nothing from a disc is part of the project.

  psx_import.py voices <re1|re2|dc> <disc .cue>[,<disc 2 .cue>] <game folder> <mod folder> [--report <file>]
      The disc's voice lines (XA, 37.8 kHz) in place of the PC's (22 kHz). Each XA channel of the disc's voice files
      is decoded whole (ffmpeg): a channel holds many lines one after another, which the game finds by position. Each
      PC clip is then looked for in every channel by the shape of its loudness over time (a sliding comparison, by
      FFT, at 10 ms and then 1 ms), and the disc's audio at that place, the PC clip's own length, is written as a
      16-bit 44.1 kHz WAV at the PC clip's path below <mod folder>, at the PC clip's loudness: the game's timing and
      mix stay as they were. A match is confirmed on the waveform itself (above 0.9); PC clips with none keep the
      PC's (reported).

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

def main(argv):
    if len(argv) >= 5 and argv[0] == 'voices' and argv[1] in GAMES:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        voices(argv[1], argv[2], argv[3], argv[4], rep)
    else: sys.exit(__doc__)

if __name__ == '__main__':
    main(sys.argv[1:])
