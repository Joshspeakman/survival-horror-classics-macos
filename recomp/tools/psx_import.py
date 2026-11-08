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

  psx_import.py movies <re1|re2|re3|dc> <disc .cue>[,<another disc .cue>] <game folder> <mod folder> [--report <file>]
      The disc's movies (MDEC, 37.8 kHz sound) in place of the PC's (Resident Evil: Cinepak, 10 fps, 22 kHz), each at its
      real frame rate (frames over playing time; ffmpeg takes every STR for 15 fps, and Resident Evil 2's and Dino
      Crisis's are 30), converted to MPEG-1 (what the port's player decodes; 30 fps) as "<PC movie>.mpg"
      below <mod folder>, which the port plays in its place (amstream.inc, dshow.c). The discs are tried in the order
      given (Resident Evil: the Ultimate Director's Cut first, for its uncut colour opening and endings, then the
      original release for the staff rolls it lacks). Each pairing is checked frame by frame against the PC's movie
      (the same scene: thumbnails at a quarter, half and three quarters through); one that differs is left out.

  psx_import.py restore <re1|re2|re3|dc> <disc folder or .cue/.chd/.iso, comma-separated> <game folder> <mods folder>
                        [--app <the Survival Horror Classics app>]
      Everything above for one game, from whichever of its discs are given (each known by its serial), into the launcher's
      mods (<mods folder>/<game>/, its index.plist): "PlayStation voices" and, for Resident Evil, "PlayStation movies",
      on, at the top of the list (a restore again replaces them). Resident Evil 3's PC voices are packed in its archives:
      they are unpacked by the game's own reader, the app's ResidentEvil3 run in test mode (--app).

  psx_import.py music re2 <disc .cue> <game folder> <mod folder> [--report <file>]
      Resident Evil 2's music from the disc's own sequences and sample banks (COMMON/SOUND/*.BGM: a SEQ and a VAB, the
      same data the PC ships), rendered as the PlayStation's sound chip plays it (psx_seq.c: its ADPCM, pitch counter,
      Gaussian interpolation, ADSR and reverb; Studio Medium, the best fit to the PC's own recordings) at 44.1 kHz
      stereo. The PC plays 22 kHz ADPCM recordings of these (Common/Sound/bgm/MAINxx.SAP, MAINxx_1/_2.SAP for an intro
      and its loop, SUBxx.SAP); each is replaced by the render cut at the same place (found by the music's pitch and
      loudness patterns, the loop's from a second time round so its reverb carries over the seam) for the same length,
      at the recording's loudness. A track whose render doesn't match its recording (chroma below 0.75) keeps the PC's.

  psx_import.py music dc <disc .cue> <game folder> <mod folder> [--report <file>]
      Dino Crisis's music: every sequence on the disc (Capcom's own formats, PSX/DATA/*.DAT) rendered, matched to the
      PC's Sound/BGM tracks (8-bit 22 kHz) and written at 16-bit 44.1 kHz; loops cut from the render's own loop at the
      PC's loop points (DINO.exe's music table; the runtime scales them to the mod's rate, re_mods.c).

  psx_import.py effects <re1|dc> <disc .cue> <game folder> <mod folder> [--report <file>]
      The PC's sound effects replaced by the disc's samples they were converted from: every bank's samples decoded
      (psx_seq --samples), each PC effect found by its pitch-shifted spectrum and confirmed on the waveform, written at
      its pitch, timing and loudness at 16-bit 44.1 kHz (Resident Evil: only where the PC cut the rate down).

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
    're1': {'disc': lambda p: p.upper().split('/')[0] in ('PSX', 'PSXUSA') and '/VOICE/' in p.upper() and p.upper().endswith('.XAS'),
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
    're1': {'pc': [('USA/Movie', '.avi')], 'disc_dirs': ('PSXUSA/ZMOVIE/', 'PSX/MOVIE/'), 'on': True,
            'note': '15 fps, 37.8 kHz sound (the PC: 10 fps Cinepak); the uncut colour cuts where a disc has them',
            'names': {'pu': 'pj', 'ou': 'oj', 'eu4': 'ed4', 'eu5': 'ed5'},
            'keep': ('staf_r', 'stfc_r', 'stfj_r', 'stfz_r')},   # the PC's staff rolls carry the credits in the picture;
                                                                 # the PlayStation drew them over its rolls in the game
    're2': {'pc': [('Pl0/Zmovie', '.bin'), ('Pl1/Zmovie', '.bin'), ('Zmovie', '.bin')], 'disc_dirs': ('PL0/ZMOVIE/', 'PL1/ZMOVIE/', 'ZMOVIE/'),
            'names': {'r704le': 'r704l', 'r704ce': 'r704c', 'titlele': 'titlel', 'titlece': 'titlec'}, 'keep': ('staffe',), 'on': False,
            'note': 'the PlayStation encode, 30 fps like the PC\'s, a touch darker, 37.8 kHz sound (the PC: Indeo 5); off: switch on to compare'},
    're3': {'pc': [('zmovie', '.dat')], 'disc_dirs': ('CD_DATA/ZMOVIE/',), 'on': False,
            'note': 'the PlayStation encode at 15 fps with its 37.8 kHz sound (the PC: the same frames, MPEG-1); off: switch on to compare'},
    'dc':  {'pc': [('Movie', '.dat')], 'disc_dirs': ('PSX/MOVIE/',), 'on': False,
            'names': {'e_op': 'opening', 'e_ed1': 'end1', 'e_ed2': 'end2', 'e_ed3': 'end3', 'e_maku': 'maku'},
            'keep': ('staff',),                                  # its credits are in the picture (the PlayStation drew them)
            'note': 'the PlayStation movies: 320x160 at 30 fps (the PC: 640x480); off: switch on to compare'},
}

def str_rate(path, sectors):
    """a PlayStation movie's real frame rate: its frames over its playing time (the disc streams 150 sectors a second);
    ffmpeg takes every STR for 15 fps, and Resident Evil 2's and Dino Crisis's are 30"""
    r = subprocess.run([FFMPEG[:-6] + 'ffprobe' if FFMPEG.endswith('ffmpeg') else 'ffprobe', '-v', 'error', '-f', 'psxstr', '-select_streams', 'v:0',
                        '-count_packets', '-show_entries', 'stream=nb_read_packets', '-of', 'csv=p=0', path], capture_output=True, text=True)
    try: n = int(r.stdout.strip().split(',')[0])
    except ValueError: return 15.0
    secs = sectors / 150.0
    return round(n / secs, 3) if secs > 0 and n > 0 else 15.0

def frames(path, fmt=None, fps=4, rate=None):
    """a movie as grey 32x24 frames, fps a second: an array (frames, 768); rate: the input's real frame rate. Black
    borders (a letterbox inside a 640x480 frame, say) are trimmed first, so two cuts of a movie framed differently compare"""
    args = [FFMPEG, '-v', 'error'] + (['-f', fmt] if fmt else []) + (['-r', str(rate)] if rate else []) + ['-i', path,
            '-vf', f'fps={fps},scale=96:72,format=gray', '-f', 'rawvideo', '-']
    r = subprocess.run(args, capture_output=True); n = len(r.stdout) // (96 * 72)
    if not n: return np.zeros((0, 768), np.float32)
    m = np.frombuffer(r.stdout[:n * 96 * 72], np.uint8).astype(np.float32).reshape(n, 72, 96)
    avg = m.mean(0); rows = np.where(avg.mean(1) > 10)[0]; cols = np.where(avg.mean(0) > 10)[0]
    if len(rows) > 8 and len(cols) > 8: m = m[:, rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1]
    ys = np.linspace(0, m.shape[1] - 1, 24).astype(int); xs = np.linspace(0, m.shape[2] - 1, 32).astype(int)
    return m[:, ys][:, :, xs].reshape(n, 768)

def colour(path, fmt=None):
    """how colourful a movie is: the mean chroma distance from grey (0 for a black-and-white one), from 2 frames a second"""
    args = [FFMPEG, '-v', 'error'] + (['-f', fmt] if fmt else []) + ['-i', path, '-vf', 'fps=2,scale=32:24,format=rgb24', '-f', 'rawvideo', '-']
    r = subprocess.run(args, capture_output=True); n = len(r.stdout) // 3
    if not n: return 0.0
    px = np.frombuffer(r.stdout[:n * 3], np.uint8).astype(np.float32).reshape(n, 3)
    return float(np.abs(px - px.mean(1, keepdims=True)).mean())

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

def convert_one(g, dd, path, lba, size, pc_path, rel, name, mod, lines, why, disc_name):
    """one disc's copy of a PC movie: checked against the PC's, converted when it is the same scene in colour"""
    with tempfile.NamedTemporaryFile(suffix='.str', delete=False) as tmp:
        for k in range((size + 2047) // 2048):
            sec = dd.sector(lba + k)
            if len(sec) < 2352: break
            tmp.write(sec)
        raw = tmp.name
    try:
        dpc = duration(pc_path); dps = (size // 2048) / 150.0
        rate = str_rate(raw, size // 2048)
        score = same_scene(frames(pc_path), frames(raw, 'psxstr', rate=rate))
        if colour(pc_path) > 6 and colour(raw, 'psxstr') < 2:          # a black-and-white cut of a colour movie
            why.append(f'{disc_name} {path} is in black and white'); return False
        if score < 0.6: why.append(f'{disc_name} {path} looks different ({score:.2f})'); return False
        out = os.path.join(mod, *rel.lower().split('/')) + '.mpg'; os.makedirs(os.path.dirname(out), exist_ok=True)
        r = subprocess.run([FFMPEG, '-nostdin', '-v', 'error', '-y', '-f', 'psxstr', '-r', str(rate), '-i', raw, '-c:v', 'mpeg1video', '-q:v', '2', '-r', '30',
                            '-c:a', 'mp2', '-b:a', '224k', '-ar', '44100', '-f', 'mpeg', out + '.part'], capture_output=True)
        if r.returncode: why.append(f'{disc_name} {path}: conversion failed: {r.stderr.decode()[-200:]}'); return False
        os.replace(out + '.part', out)
        lines.append(f'{score:.2f}  {name}  <-  {disc_name} {path} ({dps:.1f} s at {rate:g} fps; PC {dpc:.1f} s)'); return True
    finally: os.unlink(raw)

def movies(game, cues, game_dir, mod, report=None):
    g = MOVIES[game]; lines = []; pcs = []
    for folder, ext in g['pc']:
        d = find_ci(game_dir, folder)
        if d: pcs += [(os.path.join(d, n), f'{folder}/{n}', ext) for n in sorted(os.listdir(d)) if n.lower().endswith(ext)]
    discs = [psx_disc.Disc(c) for c in cues.split(',')]
    try:
        tables = [{p.upper(): (p, lba, size) for p, lba, size, xa in psx_disc.walk(dd)} for dd in discs]
        for pc_path, rel, ext in pcs:
            name = os.path.basename(rel); stem = name[:-len(ext)].lower(); want = g.get('names', {}).get(stem, stem).upper()
            if stem in g.get('keep', ()): lines.append(f'  -    {name}: the PC\'s (kept)'); continue
            srcs = []                                   # every disc's copy, in the order given: the first that matches is taken
            for dd, t in zip(discs, tables):
                for prefix in g['disc_dirs']:
                    e = t.get(prefix + want + '.STR')
                    if e: srcs.append((dd, e)); break
            if not srcs: lines.append(f'  -    {name}: not on these discs (kept)'); continue
            why = []
            for dd, (path, lba, size) in srcs:
                if convert_one(g, dd, path, lba, size, pc_path, rel, name, mod, lines, why, os.path.basename(cues.split(",")[discs.index(dd)])): break
            else: lines.append(f'  -    {name}: ' + '; '.join(why) + ': kept')
    finally:
        for dd in discs: dd.close()
    for l in lines: say(l)
    if report: open(report, 'w').write('\n'.join(lines) + '\n')

# the discs, by the serial in SYSTEM.CNF: (game, which release)
SERIALS = {'SLUS-00170': ('re1', 'original'), 'SLUS-00551': ('re1', "Director's Cut"), 'SLUS-00747': ('re1', "Director's Cut Dual Shock"),
           'SLUS-00421': ('re2', 'Leon disc'), 'SLUS-00592': ('re2', 'Claire disc'),
           'SLUS-00748': ('re2', 'Dual Shock Leon disc'), 'SLUS-00756': ('re2', 'Dual Shock Claire disc'),
           'SLUS-00923': ('re3', 'original'), 'SLUS-00922': ('dc', 'original')}
TITLES = {'re1': 'Resident Evil', 're2': 'Resident Evil 2', 're3': 'Resident Evil 3', 'dc': 'Dino Crisis'}

def find_discs(spec):
    """disc images below a folder (or listed): [(path, serial, release, game)]"""
    paths = []
    for one in spec.split(','):
        if os.path.isdir(one):
            names = sorted(os.listdir(one))
            cues = [n for n in names if n.lower().endswith(('.cue', '.chd', '.iso'))]
            covered = set()
            for n in cues:
                if n.lower().endswith('.cue'):
                    for line in open(os.path.join(one, n), encoding='latin1'):
                        m = line.strip().split('"')
                        if line.strip().upper().startswith('FILE') and len(m) > 1: covered.add(m[1].lower())
            paths += [os.path.join(one, n) for n in cues]
            paths += [os.path.join(one, n) for n in names if n.lower().endswith('.bin') and n.lower() not in covered]
        elif os.path.exists(one): paths.append(one)
    out = []
    for p in paths:
        try:
            d = psx_disc.Disc(p)
            try: serial, _ = psx_disc.system_cnf(d, psx_disc.walk(d))
            finally: d.close()
        except SystemExit: continue
        except Exception: continue
        g = SERIALS.get(serial)
        if g: out.append((p, serial, g[1], g[0]))
    return out

def xa_count(path):
    d = psx_disc.Disc(path)
    try: return sum(1 for f in psx_disc.walk(d) if f[3]), sum(f[2] for f in psx_disc.walk(d) if f[0].upper().endswith('PJ.STR'))
    finally: d.close()

def index_put(mods_root, game, entries):
    """the launcher's list for the game: entries (by folder) replaced or added, at the top"""
    import plistlib, datetime
    path = os.path.join(mods_root, game, 'index.plist')
    try: cur = plistlib.load(open(path, 'rb'))
    except Exception: cur = []
    folders = {e['folder'] for e in entries}
    cur = [m for m in cur if m.get('folder') not in folders]
    now = datetime.datetime.now()
    for e in entries: e.setdefault('added', now)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    plistlib.dump(entries + cur, open(path, 'wb'))

def unpack_re3_voices(app, game_dir, out):
    """Resident Evil 3's voices as its own reader unpacks them (re_mods.c W32_ROFSDUMP): the app's ResidentEvil3, test mode"""
    rofs = find_ci(game_dir, 'Rofs14.dat') or ''
    names = []
    d = open(rofs, 'rb').read(0x40000) if rofs else b''
    if d:
        p = 0x14
        while d[p] == 0: p += 1
        e = d.index(b'\0', p); base = d[p:e].decode(); p = e + 1; off = struct.unpack_from('<I', d, p)[0]; p += 8; e = d.index(b'\0', p); sub = d[p:e].decode()
        q = off * 8; n = struct.unpack_from('<I', d, q)[0]; q += 4
        for _ in range(n): q += 8; e = d.index(b'\0', q); names.append(f'/{base.lower()}/{sub.lower()}/{d[q:e].decode()}'); q = e + 1
    if not names: say('Resident Evil 3: its voice archive (Rofs14.dat) was not found'); return False
    macos = os.path.join(app, 'Contents', 'MacOS'); res = os.path.join(app, 'Contents', 'Resources')
    binary, exe = os.path.join(macos, 'ResidentEvil3'), os.path.join(res, 're3-ResidentEvil3.exe')
    if not (os.path.exists(binary) and os.path.exists(exe)): say('Resident Evil 3 is not in the app: its voices cannot be unpacked'); return False
    work = tempfile.mkdtemp(prefix='re3-voices-'); lst = os.path.join(work, 'list.txt'); open(lst, 'w').write('\n'.join(names) + '\n')
    env = dict(os.environ, DS_TEST='1', DS_NO_LAUNCHER='1', W32_AUDIO_MUTE='1', RE_NO_MOVIES='1', W32_ROFSDUMP=out, W32_ROFSDUMP_LIST=lst)
    say('Unpacking Resident Evil 3\'s voices with the game\'s own reader…')
    proc = subprocess.Popen([binary, '--exe', exe, '--game', game_dir, '--data', os.path.join(work, 'data')], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, errors='replace')
    ok = False
    try:
        import time; t0 = time.time()
        for line in proc.stderr:
            if 're: archive dump:' in line: ok = True; break
            if time.time() - t0 > 120: break
    finally:
        proc.kill(); proc.wait()
    return ok and os.path.isdir(out)

def restore(game, spec, game_dir, mods_root, app=None):
    discs = [d for d in find_discs(spec) if d[3] == game]
    if not discs: say(f'No {TITLES[game]} disc found there (BIN/CUE, CHD or ISO of the PlayStation release).'); return 1
    for p, serial, rel, _ in discs: say(f'Found {TITLES[game]} ({rel}, {serial}): {os.path.basename(p)}')
    cues = [p for p, *_ in discs]
    raw = [p for p in cues if psx_disc.Disc(p).raw]
    if not raw: say('These images keep only data files (2048-byte .iso): the voices and movies need a BIN/CUE or CHD.'); return 1
    game_dir_pc = game_dir; entries = []; folder_v = os.path.join(mods_root, game, 'playstation-voices')
    import shutil
    if game == 're3':
        pcv = tempfile.mkdtemp(prefix='re3-pcvoices-')
        if not app or not unpack_re3_voices(app, game_dir, pcv): return 1
        game_dir_pc = pcv
    # voices: the discs' (Resident Evil: the original release's, else any)
    vdiscs = raw
    if game == 're1': vdiscs = [p for p, s, *_ in discs if s == 'SLUS-00170' and p in raw] or raw[:1]
    tmp = folder_v + '.new'; shutil.rmtree(tmp, ignore_errors=True)
    n, total = voices(game, ','.join(vdiscs), game_dir_pc, tmp)
    if n:
        shutil.rmtree(folder_v, ignore_errors=True); os.replace(tmp, folder_v)
        entries.append({'name': 'PlayStation voices (your disc)', 'folder': 'playstation-voices', 'kind': 'files', 'enabled': True, 'game': game,
                        'source': ', '.join(os.path.basename(p) for p in vdiscs),
                        'note': f'{n} of {total} voice lines from your PlayStation disc ({"18.9" if game == "dc" else "37.8"} kHz XA, '
                                f'{"no 8-bit hiss" if game == "dc" else "the full top end"}), at the PC lines\' timing and loudness'})
    else: shutil.rmtree(tmp, ignore_errors=True)
    # movies (Resident Evil): the uncut colour ones first (the largest opening), then the rest
    if game in MOVIES:
        order = sorted(raw, key=lambda p: -xa_count(p)[1])
        folder_m = os.path.join(mods_root, game, 'playstation-movies'); tmp = folder_m + '.new'; shutil.rmtree(tmp, ignore_errors=True)
        say('Converting the movies…')
        movies(game, ','.join(order), game_dir, tmp)
        nm = sum(1 for _, _, fs in os.walk(tmp) for f in fs if f.endswith('.mpg')) if os.path.isdir(tmp) else 0
        if nm:
            shutil.rmtree(folder_m, ignore_errors=True); os.replace(tmp, folder_m)
            entries.append({'name': 'PlayStation movies (your disc)', 'folder': 'playstation-movies', 'kind': 'files', 'enabled': MOVIES[game]['on'], 'game': game,
                            'source': ', '.join(os.path.basename(p) for p in order),
                            'note': f'{nm} movies from your PlayStation discs: {MOVIES[game]["note"]}'})
        else: shutil.rmtree(tmp, ignore_errors=True)
    # music (Resident Evil 2, Dino Crisis: rebuilt from the disc's own sequences and sample banks)
    if game in ('re2', 'dc'):
        folder_u = os.path.join(mods_root, game, 'playstation-music'); tmp = folder_u + '.new'; shutil.rmtree(tmp, ignore_errors=True)
        say('Rendering the music from the disc…')
        nu, tu = music(game, raw[0], game_dir, tmp)
        if nu:
            shutil.rmtree(folder_u, ignore_errors=True); os.replace(tmp, folder_u)
            entries.append({'name': 'PlayStation music (your disc)', 'folder': 'playstation-music', 'kind': 'files', 'enabled': True, 'game': game,
                            'source': os.path.basename(raw[0]),
                            'note': f'{nu} of {tu} tracks rendered from your disc\'s own sequences and samples at 44.1 kHz stereo, as the '
                                    f'PlayStation plays them (the PC: {"8-bit 22 kHz" if game == "dc" else "22 kHz ADPCM"} recordings); the rest stay the PC\'s'})
        else: shutil.rmtree(tmp, ignore_errors=True)
    # sound effects (Dino Crisis: its PC effects are its PlayStation samples cut to 8 bits; Resident Evil: some cut to 22 kHz)
    if game in EFFECTS:
        folder_e = os.path.join(mods_root, game, 'playstation-effects'); tmp = folder_e + '.new'; shutil.rmtree(tmp, ignore_errors=True)
        say('Matching the sound effects…')
        ne, te = effects(game, ','.join(vdiscs if game == 're1' else raw[:1]), game_dir, tmp)
        if ne:
            shutil.rmtree(folder_e, ignore_errors=True); os.replace(tmp, folder_e)
            entries.append({'name': 'PlayStation sound effects (your disc)', 'folder': 'playstation-effects', 'kind': 'files', 'enabled': True, 'game': game,
                            'source': os.path.basename(raw[0]),
                            'note': f'{ne} of {te} sound effects from your disc\'s own samples: {EFFECTS[game]["note"]}; the rest stay the PC\'s'})
        else: shutil.rmtree(tmp, ignore_errors=True)
    if not entries: say('Nothing from these discs matched the game.'); return 1
    index_put(mods_root, game, entries)
    say('Restored from your disc: ' + ', '.join(e['name'] for e in entries) + ' (on, at the top of the list).')
    return 0

def psx_seq_bin():
    """the sequence renderer: beside this script (the app's Resources), else built from psx_seq.c into a temporary folder"""
    here = os.path.dirname(os.path.abspath(__file__)); b = os.path.join(here, 'psx_seq')
    if os.path.exists(b) and os.access(b, os.X_OK): return b
    src = os.path.join(here, 'psx_seq.c'); out = os.path.join(tempfile.gettempdir(), 'psx_seq-' + str(os.getuid()))
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        subprocess.run(['clang', '-O2', '-o', out + '.new', src, '-lm'], check=True); os.replace(out + '.new', out)
    return out

def music_feats(a, rate, hop=0.05):
    """log energy in 16 bands every hop seconds (mono)"""
    if rate != 11025:
        n = int(len(a) * 11025 / rate); a = np.interp(np.arange(n) / 11025, np.arange(len(a)) / rate, a)
    h = max(1, int(11025 * hop)); w = 1024; n = (len(a) - w) // h
    if n < 4: return np.zeros((1, 16), np.float32)
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    X = np.abs(np.fft.rfft(a[idx] * np.hanning(w), axis=1)) ** 2
    edges = np.unique(np.geomspace(2, X.shape[1] - 1, 17).astype(int))
    B = np.stack([X[:, edges[k]:edges[k + 1]].sum(1) for k in range(len(edges) - 1)], 1)
    return np.log10(B + 1e-6).astype(np.float32)

def music_chroma(a, rate, hop=0.1):
    if rate != 11025:
        n = int(len(a) * 11025 / rate); a = np.interp(np.arange(n) / 11025, np.arange(len(a)) / rate, a)
    h = int(11025 * hop); w = 4096; n = (len(a) - w) // h
    if n < 4: return np.zeros((1, 12), np.float32)
    idx = np.arange(w)[None, :] + h * np.arange(n)[:, None]
    X = np.abs(np.fft.rfft(a[idx] * np.hanning(w), axis=1)); f = np.fft.rfftfreq(w, 1 / 11025); ok = (f > 65) & (f < 2100)
    pc = np.round(12 * np.log2(f[ok] / 261.63)).astype(int) % 12; C = np.zeros((n, 12), np.float32)
    for k in range(12): C[:, k] = X[:, ok][:, pc == k].sum(1)
    C = np.log1p(C); C = C - C.mean(1, keepdims=True); return C / (np.linalg.norm(C, axis=1, keepdims=True) + 1e-6)

def band_scores(pc_f, ren_f):
    """the mean (over bands) normalised correlation of the PC's band features at every offset into the render's"""
    tot = None
    for b in range(pc_f.shape[1]):
        r = ncc(ren_f[:, b].astype(np.float64), pc_f[:, b].astype(np.float64)); tot = r if tot is None else tot + r
    return tot / pc_f.shape[1]

def read_stereo(path):
    with wave.open(path, 'rb') as w:
        n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate(); raw = w.readframes(n)
    a = np.frombuffer(raw, dtype='<i2').astype(np.float32) / 32768.0
    return (a.reshape(-1, ch) if ch > 1 else np.stack([a, a], 1)), rate

def write_stereo(path, a, rate=44100, store='wav'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = (np.clip(a, -1, 1) * 32767).astype('<i2')
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as t: name = t.name
    with wave.open(name, 'wb') as w: w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())
    body = open(name, 'rb').read(); os.unlink(name)
    with open(path, 'wb') as f: f.write((b'\x01\0\0\0\0\0\0\0' if store == 'sap' else b'') + body)

def split_bundle(b):
    """a SEQ followed by a VAB (Resident Evil 2's .BGM): (seq, vh, vb); the samples are the bank's last bytes (its
    header can be followed by padding)"""
    i = b.find(b'pBAV')
    if i < 0: return None
    ps = struct.unpack_from('<H', b, i + 18)[0]; nv = struct.unpack_from('<H', b, i + 22)[0]
    hdr = 32 + 128 * 16 + ps * 16 * 32 + 512; sizes = struct.unpack_from('<256H', b, i + hdr - 512); body = sum(sizes[:nv + 1]) * 8
    return b[:i], b[i:i + hdr], b[len(b) - body:]

def render(seq, vh, vb, seconds, reverb=3, depth=48):
    work = tempfile.mkdtemp(prefix='psx-music-')
    try:
        for n, d in (('a.seq', seq), ('a.vh', vh), ('a.vb', vb)): open(os.path.join(work, n), 'wb').write(d)
        out = os.path.join(work, 'a.wav')
        subprocess.run([psx_seq_bin(), os.path.join(work, 'a.vh'), os.path.join(work, 'a.vb'), os.path.join(work, 'a.seq'), out,
                        '-s', str(seconds), '-r', str(reverb), '-d', str(depth)], capture_output=True, check=True)
        return read_stereo(out)
    finally:
        import shutil; shutil.rmtree(work, ignore_errors=True)

def place(pc_mono, pc_rate, ren, ren_rate, loop=False, first=False):
    """where the PC recording starts in the render (44.1 kHz samples), how well they agree (bands, chroma)"""
    rm = ren.mean(1); sc = band_scores(music_feats(pc_mono, pc_rate), music_feats(rm, ren_rate))
    j = int(np.argmax(sc)); best = float(sc[j])
    if loop:   # a loop: the latest place nearly as good (a second time round: its reverb carries over the seam)
        cand = np.where(sc >= best - 0.03)[0]; j = int(cand.max())
    elif first:  # the start: the earliest place nearly as good (later ones can be the same music a loop later)
        cand = np.where(sc >= best - 0.03)[0]; j0 = int(cand.min()); j = j0 + int(np.argmax(sc[j0:j0 + 40]))   # its best within 2 s
    # to 10 ms, then 1 ms, around it
    h = int(0.05 * ren_rate); lo = max(0, j * h - h); seg = rm[lo:lo + len(pc_mono) * ren_rate // pc_rate + 2 * h]
    sc2 = band_scores(music_feats(pc_mono, pc_rate, 0.01), music_feats(seg, ren_rate, 0.01))
    start = lo + int(np.argmax(sc2)) * int(0.01 * ren_rate)
    n = int(len(pc_mono) * ren_rate / pc_rate); c = music_chroma(rm[start:start + n], ren_rate); p = music_chroma(pc_mono, pc_rate)
    m = min(len(c), len(p)); chroma = float((c[:m] * p[:m]).sum(1).mean()) if m else 0.0
    return start, best, chroma

def music(game, cue, game_dir, mod, report=None):
    if game == 'dc': return dc_music(cue, game_dir, mod, report)
    if game != 're2': say('Music: Resident Evil 2 and Dino Crisis'); return 0, 0
    d = psx_disc.Disc(cue.split(',')[0]); bundles = {}
    try:
        for path, lba, size, xa in psx_disc.walk(d):
            if path.upper().startswith('COMMON/SOUND/') and path.upper().endswith('.BGM'): bundles[os.path.basename(path).upper()[:-4]] = psx_disc.read_file(d, lba, size)
    finally: d.close()
    folder = find_ci(game_dir, 'Common/Sound/bgm'); lines = []; done = 0; total = 0
    split = {n: split_bundle(b) for n, b in bundles.items()}
    mains = [n for n in sorted(split) if split[n] and n.startswith('MAIN')]
    for name in sorted(bundles):
        parts = split[name]
        if not parts: continue
        if name.startswith('SUB'):
            # a sub track layers over a room's main music and plays that bank's instruments: the bank its notes find
            def missing(vh, vb):
                work = tempfile.mkdtemp(prefix='psx-sub-')
                try:
                    for n_, d_ in (('a.seq', parts[0]), ('a.vh', vh), ('a.vb', vb)): open(os.path.join(work, n_), 'wb').write(d_)
                    r = subprocess.run([psx_seq_bin(), os.path.join(work, 'a.vh'), os.path.join(work, 'a.vb'), os.path.join(work, 'a.seq'),
                                        os.path.join(work, 'a.wav'), '-s', '30'], capture_output=True, text=True)
                    import re as _re; m = _re.search(r'(\d+) notes, (\d+) with no tone', r.stderr)
                    return (int(m.group(2)), -int(m.group(1))) if m else (1 << 30, 0)
                finally: import shutil as _sh; _sh.rmtree(work, ignore_errors=True)
            own = missing(parts[1], parts[2])
            if own[0]:
                best = min(((missing(split[m][1], split[m][2]), m) for m in mains), default=None)
                if best and best[0][0] < own[0]: parts = (parts[0], split[best[1]][1], split[best[1]][2]); lines.append(f'       {name}: on {best[1]}\'s bank')
        stem = name.replace('SUB_', 'SUB')
        pcs = [n for n in sorted(os.listdir(folder)) if n.upper() in (f'{stem}.SAP', f'{stem}_1.SAP', f'{stem}_2.SAP')]
        if not pcs: continue
        longest = max(os.path.getsize(os.path.join(folder, n)) for n in pcs) / 22000.0
        ren, rate = render(*parts, seconds=min(600, 2 * longest + 40))
        for n in pcs:
            total += 1; p, pr = read_pc(os.path.join(folder, n))
            if len(p) < pr: lines.append(f'  -    {n}: too short'); continue
            start, bands, chroma = place(p, pr, ren, rate, loop=n.upper().endswith('_2.SAP') or '_' not in n)
            if chroma < 0.75 and not (name.startswith('SUB') and bands >= 0.9):   # ambience (no tune): its loudness pattern
                lines.append(f'  -    {n}: the render doesn\'t match the recording (chroma {chroma:.2f}, bands {bands:.2f}): kept'); continue
            cnt = int(len(p) * rate / pr); seg = ren[start:start + cnt]
            if len(seg) < cnt: lines.append(f'  -    {n}: the render ends too soon: kept'); continue
            seg = seg * (rms(p) / max(rms(seg.mean(1)), 1e-6))
            peak = float(np.abs(seg).max())
            if peak > 0.999: seg = seg * (0.999 / peak)
            write_stereo(os.path.join(mod, 'common', 'sound', 'bgm', n.lower()), seg, rate, store='sap')
            done += 1; lines.append(f'{chroma:.2f}  {n}  <-  {name}.BGM at {start / rate:.2f} s ({cnt / rate:.1f} s; bands {bands:.2f})')
    for l in lines: say(l)
    say(f'music: {done} of {total} recordings rendered from the disc')
    if report: open(report, 'w').write('\n'.join(lines) + '\n')
    return done, total

# ---- Dino Crisis: Capcom's own sound formats (psx_seq.c reads them) ----
DC_REVERB, DC_DEPTH = 3, 64                     # Studio Medium, depth 64: the best fit to the PC's own recordings
DC_MUSIC_TABLE = 0x625448                       # DINO.exe: rows of id, flags, name ("bgm\me_00SL"), loop point (frames)

def dc_chunks(b):
    """a Dino Crisis data file: a table of (kind, size, address, slot) rows in its first 2 KB, each chunk after it at the
    next 2 KB boundary. Kinds: 3 a "Gian" bank header, 4 its samples, 5 a sequence."""
    rows = []; p = 0
    while p + 16 <= min(0x800, len(b)):
        k, sz, a, c = struct.unpack_from('<IIII', b, p)
        if k > 16 or sz == 0 or sz > len(b): break
        rows.append((k, sz, a, c)); p += 16
    off = 0x800; out = []
    for k, sz, a, c in rows: out.append((k, a, c, b[off:off + sz])); off += (sz + 0x7ff) & ~0x7ff
    return out

def dc_table(exe):
    """the PC's music table: {name: (flags, loop frames)}; flags 0x08 plays once, 0x0a loops the whole file, 0x2a loops
    from the loop point (in frames of the PC's 22050 Hz file) to its end"""
    b = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', b, 0x3c)[0]; ns = struct.unpack_from('<H', b, pe + 6)[0]; opt = struct.unpack_from('<H', b, pe + 20)[0]
    base = struct.unpack_from('<I', b, pe + 52)[0]; secs = []
    for k in range(ns):
        o = pe + 24 + opt + 40 * k; vs, va, rs, ra = struct.unpack_from('<IIII', b, o + 8); secs.append((va, ra, rs))
    def f(v):
        for va, ra, rs in secs:
            if base + va <= v < base + va + rs: return v - base - va + ra
    out = {}; q = f(DC_MUSIC_TABLE)
    while q is not None:
        i, fl, nm, val = struct.unpack_from('<IIII', b, q); n = f(nm) if nm else None
        if n is None: break
        name = b[n:b.index(b'\0', n)].decode('latin1')
        if name.lower().startswith('bgm\\'): out.setdefault(name[4:], (fl, val))
        q += 16
    return out

def dc_render(work, stem, sq, slot, seconds, tempo=1.0):
    """a render (44.1 kHz stereo) and its loop: (start, end) in seconds, or None"""
    sp = os.path.join(work, stem); out = os.path.join(work, f'{stem}.{sq}.wav')
    r = subprocess.run([psx_seq_bin(), f'{sp}.{slot}.vh', f'{sp}.{slot}.vb', f'{sp}.{sq}.{slot}', out, '-b', open(f'{sp}.{slot}.base').read(),
                        '-s', str(seconds), '-r', str(DC_REVERB), '-d', str(DC_DEPTH), '-t', '%.6f' % tempo, '-v'], capture_output=True, text=True)
    ls = [float(l.split()[2]) for l in r.stderr.splitlines() if l.startswith('loop start')]
    le = [float(l.split()[2]) for l in r.stderr.splitlines() if l.startswith('loop end')]
    loop = (ls[0], le[0]) if ls and le and le[0] - ls[0] > 1 else None
    return out, loop

def dc_music(cue, game_dir, mod, report=None):
    """Dino Crisis's music: every sequence on the disc (PSX/DATA/*.DAT: Gian banks and Capcom sequences) rendered as the
    PlayStation plays it, each PC track (Sound/BGM/*.dat, 8-bit 22 kHz) matched to its render by the music's loudness
    and pitch patterns, and written as 16-bit 44.1 kHz stereo at the PC track's loudness. Its loops are cut from the
    render's own loop (a second time round, so the reverb carries over the seam): a track that loops from a point keeps
    that point (the runtime scales it to the new rate, re_mods.c), one that loops whole is one loop long."""
    import hashlib, shutil, concurrent.futures as cf
    exe = find_ci(game_dir, 'DINO.exe'); folder = find_ci(game_dir, 'Sound/BGM')
    if not exe or not folder: say('Dino Crisis: DINO.exe or Sound/BGM is missing'); return 0, 0
    table = dc_table(exe)
    work = tempfile.mkdtemp(prefix='dc-music-'); pairs = []; seen = set()
    try:
        d = psx_disc.Disc(cue.split(',')[0])
        try:
            for path, lba, size, xa in psx_disc.walk(d):
                up = path.upper()
                if not (up.startswith('PSX/DATA/') and up.endswith('.DAT')) or xa: continue
                head = psx_disc.read_file(d, lba, min(size, 0x800)); kinds = set()
                for q in range(0, len(head) - 15, 16):
                    k, sz = struct.unpack_from('<II', head, q)
                    if k > 16 or sz == 0: break
                    kinds.add(k)
                if 5 not in kinds: continue
                cs = dc_chunks(psx_disc.read_file(d, lba, size)); stem = os.path.basename(up)[:-4]; banks = {}
                for k, a, c, data in cs:
                    if k == 3 and data[:4] == b'Gian': banks.setdefault(c & 15, {})['vh'] = (data, a)
                    if k == 4: banks.setdefault(c & 15, {})['vb'] = (data, a)
                for slot, b in banks.items():
                    if 'vh' in b and 'vb' in b:
                        open(os.path.join(work, f'{stem}.{slot}.vh'), 'wb').write(b['vh'][0]); open(os.path.join(work, f'{stem}.{slot}.vb'), 'wb').write(b['vb'][0])
                        open(os.path.join(work, f'{stem}.{slot}.base'), 'w').write('%x' % b['vh'][1])
                for k, a, c, data in cs:
                    if k != 5: continue
                    idx = c & 15; slot = 4 if idx == 0 and 4 in banks else 5 if 5 in banks else 4
                    if slot not in banks or 'vb' not in banks[slot] or 'vh' not in banks[slot]: continue
                    key = (hashlib.md5(data).hexdigest(), hashlib.md5(banks[slot]['vb'][0]).hexdigest())
                    if key in seen: continue
                    seen.add(key); open(os.path.join(work, f'{stem}.seq{idx}.{slot}'), 'wb').write(data); pairs.append((stem, f'seq{idx}', slot))
        finally: d.close()
        if not pairs: say('Dino Crisis: no music on this disc'); return 0, 0
        say(f'Rendering {len(pairs)} pieces of music from the disc…'); psx_seq_bin()
        feats = {}
        def first(pr):
            stem, sq, slot = pr; out, loop = dc_render(work, stem, sq, slot, 180)
            a, rate = read_stereo(out); m = a.mean(1); os.unlink(out)
            return pr, music_feats(m, rate), music_chroma(m, rate), loop
        with cf.ThreadPoolExecutor(max(2, (os.cpu_count() or 4) - 1)) as ex:
            for pr, f, c, loop in ex.map(first, pairs): feats[pr] = (f, c, loop)
        lines = []; done = 0; names = sorted(table)
        for n in names:
            pcf = next((x for x in os.listdir(folder) if x.lower() == n.lower() + '.dat'), None)
            if not pcf: continue
            fl, val = table[n]; p, pr_ = read_pc(os.path.join(folder, pcf)); L = len(p) / pr_
            pf, pch = music_feats(p, pr_), music_chroma(p, pr_); code = n[:5].replace('_', '').upper()
            cands = []
            for k, (rf, rc, loop) in feats.items():
                if len(rf) < len(pf): continue
                sc = band_scores(pf, rf); j = int(np.argmax(sc)); b = float(sc[j])
                cj = int(j * 0.05 / 0.1); m = min(len(pch), len(rc) - cj); ch = float((pch[:m] * rc[cj:cj + m]).sum(1).mean()) if m > 0 else 0.0
                named = code in k[0].replace('_', '')
                cands.append((b, ch, named, k))
            ok = [c for c in cands if c[2] and (c[0] >= 0.6 and c[1] >= 0.35 or c[0] >= 0.45 and c[1] >= 0.7)] or \
                 [c for c in cands if c[1] >= 0.68 and c[0] >= 0.65]
            if not ok:
                best = sorted(cands, key=lambda c: -(c[0] + c[1]))[:2]
                lines.append(f'  -    {pcf}: no piece of the disc\'s music matches it: kept (nearest: ' +
                             ', '.join(f'{c[3][0]} {c[3][1]} bands {c[0]:.2f} chroma {c[1]:.2f}' for c in best) + ')'); continue
            b, ch, named, k = max(ok, key=lambda c: (c[2], c[0] + c[1]))
            loop = feats[k][2]; T = val / 22050.0
            need = 2 * L + 20 + (2 * (loop[1] - loop[0]) if loop else 0)
            out, loop = dc_render(work, *k, min(600, need)); ren, rate = read_stereo(out); os.unlink(out)
            whole = (fl & 0x22) == 0x02
            start, b2, ch2 = place(p, pr_, ren, rate, loop=whole, first=not whole)
            S = start / rate; how = 'as the PC'
            if fl & 0x02 and not (loop and (fl & 0x20 and S + T >= loop[0] - 0.05 or not fl & 0x20 and S >= loop[0] - 0.05)) and L > 12:
                # a loop the sequence's own doesn't give: the PC's, at the PC's tempo (the render runs a little slower),
                # measured by where the PC's last seconds fall in the render
                tail = p[int((L - 9) * pr_):int((L - 1) * pr_)]; lo = max(0, start + int((L - 11) * rate)); hi = min(len(ren), start + int((L + 1) * rate))
                sc = band_scores(music_feats(tail, pr_, 0.01), music_feats(ren[lo:hi].mean(1), rate, 0.01))
                at = (lo + int(np.argmax(sc)) * int(0.01 * rate)) / rate; f = (at - S) / (L - 9)
                if 0.98 < f < 1.02 and abs(f - 1) > 0.0003:
                    out, loop = dc_render(work, *k, min(600, need), tempo=f); ren, rate = read_stereo(out); os.unlink(out)
                    start, b2, ch2 = place(p, pr_, ren, rate, first=True); S = start / rate; how = f'as the PC, at {f:.4f}x the tempo'
            if loop and fl & 0x20 and S + T >= loop[0] - 0.05:           # loops from a point: a second time round after it
                P = loop[1] - loop[0]; a0 = int(round(start + T * rate)); a1 = int(round(start + (T + P) * rate)); a2 = int(round(start + (T + 2 * P) * rate))
                seg = np.concatenate([ren[start:a0], ren[a1:a2]]) if a2 <= len(ren) else None; how = f'loop at {T:.2f} s, {P:.2f} s long'
            elif loop and fl & 0x02 and not fl & 0x20 and S >= loop[0] - 0.05:   # loops whole: one loop
                P = loop[1] - loop[0]; seg = ren[start:start + int(round(P * rate))]; how = f'one loop, {P:.2f} s'
            else:
                seg = ren[start:start + int(round(L * rate))]
            if seg is None or len(seg) < rate // 2 or how == 'as the PC' and len(seg) < int(0.98 * L * rate):
                lines.append(f'  -    {pcf}: the render ends too soon: kept'); continue
            seg = seg * (rms(p) / max(rms(seg.mean(1)), 1e-6)); peak = float(np.abs(seg).max())
            if peak > 0.999: seg = seg * (0.999 / peak)
            write_stereo(os.path.join(mod, 'Sound', 'BGM', pcf), seg, rate)
            done += 1; lines.append(f'{ch2:.2f}  {pcf}  <-  {k[0]}.DAT {k[1]} at {S:.2f} s ({len(seg) / rate:.1f} s; {how}; bands {b2:.2f})')
        for l in lines: say(l)
        say(f'music: {done} of {len(names)} tracks rendered from the disc')
        if report: open(report, 'w').write('\n'.join(lines) + '\n')
        return done, len(names)
    finally:
        shutil.rmtree(work, ignore_errors=True)

# ---- Dino Crisis's sound effects: the PC's are its PlayStation samples converted to 8-bit 22 kHz ----
SE_K, SE_F0 = 96, 40.0                          # semitone bands from 40 Hz, for the pitch-shift search

def se_trim(a, rel=0.02):
    t = max(float(np.abs(a).max()) * rel, 1e-4) if len(a) else 1; idx = np.where(np.abs(a) > t)[0]
    return (int(idx[0]), int(idx[-1]) + 1) if len(idx) else (0, 0)

def se_env(a, n=48):
    e = np.sqrt(np.add.reduceat(a.astype(np.float64) ** 2, np.linspace(0, len(a), n, endpoint=False).astype(int)) + 1e-9)
    e = np.log(e + 1e-4); e = e - e.mean(); return e / (np.linalg.norm(e) + 1e-9)

def se_bands(a, rate):
    n = 1 << int(np.ceil(np.log2(max(len(a), 256)))); X = np.abs(np.fft.rfft(a, n)) ** 2; f = np.fft.rfftfreq(n, 1 / rate)
    k = np.floor(12 * np.log2(np.maximum(f, 1) / SE_F0)).astype(int); ok = (k >= 0) & (k < SE_K) & (f < 0.45 * rate)
    B = np.log(np.bincount(k[ok], X[ok], SE_K) + 1e-6); B[~np.isin(np.arange(SE_K), np.unique(k[ok]))] = np.nan; return B

def se_corr(a, b):
    if len(a) >= len(b): c = ncc(a.astype(np.float64), b.astype(np.float64)); j = int(np.argmax(c)); return float(c[j]), j
    c = ncc(b.astype(np.float64), a.astype(np.float64)); j = int(np.argmax(c)); return float(c[j]), -j

def se_rs(x, r): n = int(len(x) * r); return np.interp(np.arange(n) / r, np.arange(len(x)), x)

def se_search(d, p, r0):
    """the resampling ratio near r0 (PC samples per disc sample) and lag where the disc sample's waveform best matches"""
    best = (-1.0, r0, 0); k = 4 if len(d) > 3000 else 1; dd, pp = d[::k], p[::k]
    for r in np.linspace(r0 * 0.955, r0 * 1.045, 31):
        if int(len(d) * r) < 32: continue
        c, j = se_corr(pp, se_rs(dd, r))
        if c > best[0]: best = (c, float(r), j * k)
    if best[0] < 0.4: return best
    for span, steps in ((0.005, 21), (0.0003, 7)):
        r1 = best[1]
        for r in np.linspace(r1 * (1 - span), r1 * (1 + span), steps):
            c, j = se_corr(p, se_rs(d, r))
            if c > best[0]: best = (c, float(r), j)
    return best

_SE = {}
def _se_init(folder):
    """a worker's disc samples: every .raw below folder, trimmed, with their loudness shape and semitone spectrum"""
    import glob, hashlib
    seen = set(); keys = []; data = []
    for f in sorted(glob.glob(os.path.join(folder, '*', '*.raw'))):
        b = open(f, 'rb').read(); h = hashlib.md5(b).hexdigest()
        if h in seen or len(b) < 400: continue
        seen.add(h); a = np.frombuffer(b, '<i2').astype(np.float32) / 32768; s, e = se_trim(a)
        if e - s < 100: continue
        keys.append(f); data.append((a, s, e))
    _SE['keys'] = keys; _SE['data'] = data
    _SE['E'] = np.stack([se_env(a[s:e]) for a, s, e in data]); _SE['B'] = np.stack([se_bands(a[s:e], 44100) for a, s, e in data])

def _se_match(path):
    """one PC effect: (path, score, ratio, lag, disc sample index, PC trim start) or a score of -1"""
    p, rate = read_pc(path); s, e = se_trim(p); pt = p[s:e]
    if len(pt) < 100 or rate != 22050: return path, -1.0, 0, 0, -1, 0
    E, B = _SE['E'], _SE['B']; pb = se_bands(pt, 22050); es = E @ se_env(pt)
    best = np.full(len(E), -9.0); bm = np.zeros(len(E), int)
    for m in range(-16, 41):                     # a pitch m semitones off: the PC's band k is the disc's k + m (ratio 0.5 * 2^(m/12))
        lo, hi = max(0, -m), min(SE_K, SE_K - m); x = pb[lo:hi]; Y = B[:, lo + m:hi + m]
        ok = ~np.isnan(x)[None, :] & ~np.isnan(Y); n = ok.sum(1)
        xs = np.where(ok, x[None, :], 0); ys = np.where(ok, Y, 0)
        xc = np.where(ok, xs - (xs.sum(1) / np.maximum(n, 1))[:, None], 0); yc = np.where(ok, ys - (ys.sum(1) / np.maximum(n, 1))[:, None], 0)
        c = (xc * yc).sum(1) / (np.sqrt((xc ** 2).sum(1) * (yc ** 2).sum(1)) + 1e-9); c[n < 24] = -9
        better = c > best; best[better] = c[better]; bm[better] = m
    sc = best + 0.5 * es; top = (-1.0, 0, 0, -1)
    for i in np.argsort(-sc)[:12]:
        a, ds, de = _SE['data'][i]
        for dm in (0, -1, 1):
            c, r, j = se_search(a[ds:de], pt, 0.5 * 2 ** ((bm[i] + dm) / 12))
            if c > top[0]: top = (c, r, j, int(i))
            if c > 0.9: break
        if top[0] > 0.97: break
    return path, top[0], top[1], top[2], top[3], s

def fft_resample(x, n):
    """x at n samples, band-limited (zero-padded so the ends don't wrap)"""
    pad = len(x) // 8 + 64; y = np.concatenate([np.zeros(pad), x, np.zeros(pad)]); m = len(y); N = int(round(m * n / len(x)))
    Y = np.fft.irfft(np.fft.rfft(y), N) * (N / m); q = int(round(pad * n / len(x))); return Y[q:q + n]

def disc_banks(game, d):
    """every sample bank on the disc: (vh, vb, sound RAM address or None). Dino Crisis: the Gian banks in PSX/DATA/*.DAT;
    Resident Evil: PSX/SOUND/*.HED (a VAB header after a small table) with its .VB, and the VAB in each room (.RDT)"""
    files = list(psx_disc.walk(d))
    if game == 'dc':
        for path, lba, size, xa in files:
            up = path.upper()
            if xa or not (up.startswith('PSX/DATA/') and up.endswith('.DAT')): continue
            banks = {}
            for k, a, c, data in dc_chunks(psx_disc.read_file(d, lba, size)):
                if k == 3 and data[:4] == b'Gian': banks.setdefault(c & 15, {})['vh'] = (data, a)
                if k == 4: banks.setdefault(c & 15, {})['vb'] = (data, a)
            for b in banks.values():
                if 'vh' in b and 'vb' in b: yield b['vh'][0], b['vb'][0], b['vh'][1]
    elif game == 're1':
        byname = {p.upper(): (l, s) for p, l, s, x in files}
        for path, lba, size, xa in files:
            up = path.upper()
            if up.startswith('PSX/SOUND/') and up.endswith('.HED') and up[:-4] + '.VB' in byname:
                h = psx_disc.read_file(d, lba, size); i = h.find(b'pBAV')
                if i >= 0: yield h[i:], psx_disc.read_file(d, *byname[up[:-4] + '.VB']), None
            elif up.endswith('.RDT'):
                b = psx_disc.read_file(d, lba, size); i = b.find(b'pBAV')
                if i < 0 or i + 32 > len(b): continue
                ps, ts, vs = struct.unpack_from('<HHH', b, i + 18); vh = 32 + 2048 + ps * 512 + 512
                if i + vh > len(b): continue
                vbl = sum(struct.unpack_from('<256H', b, i + vh - 512)[:vs + 1]) * 8
                yield b[i:i + vh], b[i + vh:i + vh + vbl], None

EFFECTS = {   # the PC's effects folder, which of its files are effects, and when a disc sample is worth taking
    'dc':  {'folder': 'Sound/SE', 'pick': lambda n: n.lower().endswith('.dat'), 'take': lambda score, r: score >= 0.85,
            'note': '16-bit 44.1 kHz at the PC\'s pitch, timing and loudness (the PC: 8-bit 22 kHz)'},
    're1': {'folder': 'USA/Sound', 'pick': lambda n: n.lower().endswith('.wav') and not n.lower().startswith('bgm'),
            'take': lambda score, r: score >= 0.85 and r < 0.95,       # only where the PC cut the disc's sample rate down
            'note': 'the ones the PC cut down to 22 kHz, at the disc\'s full rate (44.1 kHz files) and the PC\'s pitch, timing and loudness'},
}

def effects(game, cue, game_dir, mod, report=None):
    """the PC's sound effects (Dino Crisis: 8-bit 22 kHz; Resident Evil: 22 kHz) are the PlayStation's samples converted.
    Each sample of every bank on the disc is decoded (psx_seq --samples); each PC effect is looked for among them by its
    pitch-shifted spectrum and loudness shape, then confirmed on the waveform itself at the best resampling ratio (0.85
    or better); the disc's sample is then written at that pitch, 16-bit 44.1 kHz, at the PC effect's place, length,
    loudness and channels. Effects with no match (or, for Resident Evil, no more detail on the disc) keep the PC's."""
    import hashlib, shutil, concurrent.futures as cf
    cfg = EFFECTS[game]; folder = find_ci(game_dir, cfg['folder'])
    if not folder: say(f'{TITLES[game]}: {cfg["folder"]} is missing'); return 0, 0
    work = tempfile.mkdtemp(prefix=f'{game}-se-'); seen = set(); nb = 0
    try:
        tool = psx_seq_bin()
        for c in cue.split(','):
            d = psx_disc.Disc(c)
            try:
                for vh, vb, base in disc_banks(game, d):
                    h = hashlib.md5(vb).hexdigest()
                    if h in seen or not vb: continue
                    seen.add(h); nb += 1; out = os.path.join(work, f'{nb:04d}'); os.makedirs(out)
                    open(out + '.vh', 'wb').write(vh); open(out + '.vb', 'wb').write(vb)
                    subprocess.run([tool, '--samples', out + '.vh', out + '.vb', out, 'x'] + (['-b', '%x' % base] if base is not None else []), capture_output=True)
            finally: d.close()
        pcs = sorted(os.path.join(r, f) for r, _, fs in os.walk(folder) for f in fs if cfg['pick'](f))
        say(f'Matching {len(pcs)} sound effects with {nb} of the disc\'s sample banks…')
        _se_init(work); keys, data = _SE['keys'], _SE['data']; done = 0; lines = []
        with cf.ProcessPoolExecutor(max(2, (os.cpu_count() or 4) - 1), initializer=_se_init, initargs=(work,)) as ex:
            for k, (path, score, r, j, i, ps) in enumerate(ex.map(_se_match, pcs, chunksize=4)):
                rel = os.path.relpath(path, game_dir)
                if (k + 1) % 50 == 0: say(f'Sound effects: {k + 1} of {len(pcs)}')
                if i < 0 or not cfg['take'](score, r): lines.append(f'  -    {rel}: {"no match" if i < 0 else f"best {score:.2f} at {r:.2f}"}: kept'); continue
                a, ds, de = data[i]
                raw = open(path, 'rb').read(); ch = struct.unpack_from('<H', raw, 22)[0] if raw[:4] == b'RIFF' else 1
                pcm, pr = read_pc(path); n_out = 2 * len(pcm)
                # the disc sample's start in the PC clip: its trimmed start at ps + j, at r PC samples per disc sample
                start = 2 * (ps + j - ds * r); x = fft_resample(a.astype(np.float64), int(round(len(a) * 2 * r)))
                o = int(round(start)); y = np.zeros(n_out)
                lo, hi = max(0, o), min(n_out, o + len(x))
                if hi > lo: y[lo:hi] = x[lo - o:hi - o]
                # loudness: least squares against the PC clip (at 22 kHz, its own channels)
                if ch == 2:
                    st = _stereo_pc(path)
                    gl = _ls_gain(st[:, 0], y[::2][:len(st)]); gr = _ls_gain(st[:, 1], y[::2][:len(st)])
                    outv = np.stack([y * gl, y * gr], 1)
                else:
                    outv = (y * _ls_gain(pcm, y[::2][:len(pcm)]))[:, None]
                peak = float(np.abs(outv).max()) if outv.size else 0
                if peak > 0.999: outv *= 0.999 / peak
                _write_pcm(os.path.join(mod, rel), outv, 44100)
                done += 1; lines.append(f'{score:.3f}  {rel}  <-  {os.path.basename(os.path.dirname(keys[i]))}/{os.path.basename(keys[i])} at {r * 2:.4f}x')
        say(f'sound effects: {done} of {len(pcs)} from the disc')
        if report: open(report, 'w').write('\n'.join(lines) + '\n')
        return done, len(pcs)
    finally:
        shutil.rmtree(work, ignore_errors=True)

def _stereo_pc(path):
    r = subprocess.run([FFMPEG, '-v', 'error', '-i', path, '-ac', '2', '-f', 's16le', '-'], capture_output=True)
    return np.frombuffer(r.stdout, '<i2').astype(np.float64).reshape(-1, 2) / 32768

def _ls_gain(target, x):
    """the gain that gives x the target's loudness where both play (its sign: the target's polarity)"""
    n = min(len(target), len(x)); t, y = target[:n], x[:n]; on = np.abs(t) > 0.01
    if on.sum() < 32: on = np.ones(n, bool)
    ry = float(np.sqrt((y[on] ** 2).mean())); g = float(np.sqrt((t[on] ** 2).mean())) / ry if ry > 1e-9 else 0.0
    return g if float((t * y).sum()) >= 0 else -g

def _write_pcm(path, a, rate):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pcm = (np.clip(a, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w: w.setnchannels(a.shape[1]); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())

def main(argv):
    if len(argv) >= 5 and argv[0] == 'restore' and argv[1] in GAMES:
        app = argv[argv.index('--app') + 1] if '--app' in argv else None
        sys.exit(restore(argv[1], argv[2], argv[3], argv[4], app))
    if len(argv) >= 5 and argv[0] == 'effects' and argv[1] in EFFECTS:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        effects(argv[1], argv[2], argv[3], argv[4], rep); return
    if len(argv) >= 5 and argv[0] == 'music':
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        music(argv[1], argv[2], argv[3], argv[4], rep); return
    if len(argv) >= 5 and argv[0] == 'movies' and argv[1] in MOVIES:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        movies(argv[1], argv[2], argv[3], argv[4], rep)
    elif len(argv) >= 5 and argv[0] == 'voices' and argv[1] in GAMES:
        rep = argv[argv.index('--report') + 1] if '--report' in argv else None
        voices(argv[1], argv[2], argv[3], argv[4], rep)
    else: sys.exit(__doc__)

if __name__ == '__main__':
    main(sys.argv[1:])
