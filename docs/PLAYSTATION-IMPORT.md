# Plan: PlayStation content from your own discs

Goal: from a disc image of your own PlayStation copy of a game, pull in what the PlayStation version does better than the
GOG PC release, and install it as mods in the launcher's Mods window. Each kind is its own mod ("PlayStation music",
"PlayStation voices", ...), so it can be switched on or off and ordered like any other mod. Nothing in the game folders
changes, and nothing from a disc is committed or leaves this Mac. This is the same idea as the GTA port's
Restore from Disc (`gta_restore.py`) and Theme Park's original-content imports.

## What the PC releases ship (measured on these GOG copies)

| | Music | Voices | Sound effects | Movies |
|---|---|---|---|---|
| Resident Evil | WAV, **8-bit mono 22 kHz** | 16-bit mono 22 kHz | 16-bit 22 kHz | Cinepak 320×240 at **10 fps** |
| Resident Evil 2 | MS ADPCM 22 kHz (`.SAP`) | MS ADPCM 22 kHz | (to check) | Indeo 5, 320×160 at 30 fps |
| Resident Evil 3 | PlayStation-style sequences (`.bgm` + `.vb` banks) | 22 kHz | (to check) | MPEG-1 320×160 |
| Dino Crisis | **8-bit stereo 22 kHz** | **8-bit mono 22 kHz** | **8-bit mono 22 kHz** | MPEG-1 640×480 |

The PlayStation discs carry the same material at a higher quality:
- Voices and streamed audio are XA ADPCM at 37.8 kHz.
- Sequenced music plays through the SPU at 44.1 kHz with its hardware reverb.
- Movies are STR files: MDEC video with XA audio.

## What the community counts as better on PlayStation, and what stays PC

**Resident Evil**
- **Take from the disc:** the music (the PC's 8-bit mono WAVs are its weakest point), voices, and the movies other than
  the intro (the PC's are 10 fps Cinepak).
- **Keep from PC:** the intro. Western PlayStation discs have a cut black-and-white intro and a cut Kenneth scene,
  while the PC's is uncut and in colour. Also keep the PC's extra weapons and outfits.
- **With a Japanese *Biohazard* disc (optional):** its vocal opening and ending songs and its file backgrounds.
- **With a Director's Cut Dual Shock disc:** its re-scored soundtrack is disliked by many. It would be a separate mod,
  off by default; the original soundtrack is the one to take.

**Resident Evil 2**
- **Take from the disc:** the music (sequenced, rendered at 44.1 kHz with reverb), voices, and the movies. The 1998 PC
  release is widely called the weakest version for its movies.
- **Keep from PC:** the 640×480 backgrounds, the Extreme Battle and gallery extras, and everything from the Dual Shock
  version that the PC already has.

**Resident Evil 3**
- **Take from the disc:** the voices (community packs rip them from the PlayStation at 37.8 kHz), the movies (the best
  fan packs sync the PlayStation audio to them), and possibly the sound banks (`.vb`) if the PC's are downsampled.
- **To check against the disc:** the scenes Europe censored (the hunter in the hospital, kicking off a crawling
  zombie's head). The PC is based on the Dreamcast build, and later releases on every platform carry those cuts. If the
  PlayStation disc is uncut, restoring them means room data, not just audio, so it's its own, later step.
- **Keep from PC:** the 640×480 backgrounds and the instant door skips.

**Dino Crisis**
- **Take from the disc:** all audio (music, effects, voices). The PC's is entirely 8-bit 22 kHz; Classic REbirth's
  biggest fix is the audio, and its Dino Crisis 2 rebuilds the effects from PlayStation data.
- **Keep from PC:** the 640×480 movies (the PlayStation's are 320×240), the higher-detail 3D, the Arrange mode and the
  unlocked extras.

## What to give me

- **Best: BIN/CUE (raw 2352-byte sectors), or CHD.** These keep the XA audio and STR movies. A CHD is unpacked with
  `chdman` (Homebrew's `rom-tools`), installed while the import runs, as ffmpeg is for the movies.
- **A plain 2048-byte `.iso` is not enough:** it drops XA audio and movies, so only ordinary files survive (sequenced
  music and sound banks still work).
- **Multi-disc games:** all the discs (Resident Evil 2 has two).
- **Region:** any. The disc's serial in `SYSTEM.CNF` says which release it is, and the recipe adapts (US, PAL, Japanese,
  Director's Cut and Dual Shock versions).

## How it will work

1. **Restore from Disc…**, a new button in the Mods window, plus `--restore-disc <game> <image>` from a terminal.
2. **Identify** the disc by its serial, and refuse with a clear message if it doesn't match the chosen game.
3. **Extract**: walk the ISO 9660 file system, keeping each file's raw sectors so XA and STR streams stay whole.
4. **Convert**:
   - **XA ADPCM to PCM:** ffmpeg, already used at install for the movies (its `psxstr` demuxer and `adpcm_xa`
     decoder; checked on this Mac).
   - **STR to MPEG-1:** ffmpeg (`psxstr` and its `mdec` decoder).
   - **Sequenced music to WAV:** a SEQ/SEP player over the VAB sample banks, with the SPU's ADPCM and reverb. This is
     the largest new piece. It plays each track for its intro and one loop, with the loop points the PC files expect.
5. **Map** each converted asset to the PC file it replaces, in the format the PC game reads. The runtime already plays
   16-bit 44.1 kHz WAV through its DirectSound layer, so the PC's 8-bit limit doesn't apply. Each game gets a recipe
   table:

   | Game | PlayStation source | PC file it replaces |
   |---|---|---|
   | Resident Evil | music sequence `n` | `USA/Sound/bgm_xx.wav` |
   | Resident Evil 2 | XA channel `c` of a file | `Pl0/voice/StageN/Vnnn.SAP` (a `.SAP` header around the WAV) |
   | Resident Evil 3 | its voices | `/data_a/voice/...` inside Rofs (the mod layer already reads Rofs) |
   | Dino Crisis | its XA voices | `Sound/VOICE/xa*.dat` |

   The tables are built by matching lengths and content (cross-correlating the decoded PC and PlayStation audio), and
   are written down per disc serial.
6. **Install** as mods, newest on top:
   - PlayStation music
   - PlayStation voices
   - PlayStation sound effects
   - PlayStation movies
   - for Resident Evil, a separate mod for the Japanese songs, off by default
   - for Resident Evil 3, uncut scenes later, if the disc has them

   Every file is placed by the existing installer, so Resident Evil 3's archive files work as other mods do.

## How it gets checked

- **Per asset:**
  - decoded lengths match the PC file they replace, within the loop points
  - no clipping
  - sample rates and channels as recorded
- **In game:**
  - the usual QA runs with each mod on (no faults, mod files read)
  - A/B screenshots of the movies
  - listening tests run unmuted, since they are audio tests
  - Resident Evil 1's intro still the PC's colour cut
- **Afterwards:** a checksum of the game folders before and after, to show they are untouched.

## Order of work

1. The disc reader and identification, then the voices (XA through ffmpeg), since they are the clearest win.
2. Movies (ffmpeg), with Resident Evil 1 the most visible.
3. The sequenced-music renderer, then music for Resident Evil 1 and 2 and Dino Crisis.
4. Sound effects where the PlayStation banks are better (Dino Crisis above all).
5. Resident Evil 3's uncut scenes, only if the disc shows they exist.

## Open points, settled once a disc is here

- Whether each game's PlayStation music is sequenced (SEQ/VAB) or streamed (XA). It decides how much of step 3 each
  game needs.
- Resident Evil 2's PlayStation movies against the PC's Indeo ones: compared frame by frame before either is chosen.
- Which PC files have no PlayStation counterpart (PC-only content). These are left as the PC has them.
