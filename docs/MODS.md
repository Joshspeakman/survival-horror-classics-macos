# Mods and the original disc content

## The Mods window

The launch window's **Mods** tile opens a window for the chosen game: its installed mods (switch each on or off, order
them, remove them), a browser of the game's mods on GameBanana (search, most downloaded, newest; *Install* downloads
and installs one) and *Install from File* for an archive you have (zip, rar or 7z; or drop it on the window).

Each mod is kept in `~/Games/<X>Native/mods/<folder>`, listed in priority order in `mods/index.plist`; the game folder
itself is never changed. What an archive holds decides what kind of mod it is:

| Kind | What it is | Here |
|---|---|---|
| files | game files at their game-folder paths (`data/`, `models/`, `audio/`, `text/`, ...) | read in place of the game's own, the first mod in the list winning (`GTA_MODS`, `recomp/runtime/win32/files.c`); on when installed |
| img | loose models and textures (`.dff`, `.txd`) meant to replace entries inside the game's `.img` archives | kept, off: replacing archive entries is not supported yet |
| code | ASI plugins, DLLs, CLEO scripts | native Windows code: these would have to be recompiled with the game, which is not supported |
| saves | savegames | copied into the game's user files folder |

## San Andreas: restore from disc

The Steam release of San Andreas (3.0) changed the game as first released in 2005. *Restore from Disc* in the Mods
window (`recomp/tools/gta_restore.py`) reads your own original PC DVD (a disc image or the mounted disc; it needs
`unshield`: `brew install unshield`) and makes a mod, *original-disc-content*, with:

- the radio stations as released (3.0 removed licensed songs from eight of them) and their track index;
- the loading screens with the EAX and NVIDIA logos;
- the mission scripts as released (`main.scm`, `script.img`);
- `models/gta3.img` with the 2005 downtown Los Santos shop fronts and signs (an APFS clone of your game's archive with
  two entries rewritten).

It also makes **original-disc-content-hot-coffee**, off until you switch it on: the 2005 scripts with the one byte
that keeps the hidden girlfriend minigame switched off changed, as the 2005 PC mod did.

GTA III and Vice City need nothing restored: the Steam copies (Vice City's Windows release) hold the discs' data
unchanged, checked file by file. (The *Mac* Steam release of Vice City has the cut radio; this port uses the Windows
one.)

Nothing from the disc is part of this project; everything stays on your Mac.
