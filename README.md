# Grand Theft Auto III, native for Apple Silicon

Rockstar's **Grand Theft Auto III** (2001), statically recompiled from your own Steam copy into native arm64 code: no
Wine, no Rosetta, no emulator. It uses the same recompiler and runtime as the author's Dungeon Siege, Delta Force,
Diablo II, Fallout: New Vegas and Sims ports, with a Direct3D 8 layer on its Metal renderer.

The Mac release on Steam (app 12230) is the Windows game inside TransGaming's Cider wrapper, a 32-bit Intel program
that no longer runs on current macOS. This port takes the `gta3.exe` from inside it.

Nothing from the games is in this repository. `./install.sh` imports your copies into `~/Games/<GTA3|GTAVC|GTASA>Native/game-files`
(an APFS clone: no extra space), undoes the Steam wrapper on a copy in the build folder, recompiles that copy and
installs one app, **GTA Classics**, with a card for each game (Vice City and San Andreas are being ported alongside).

## Installing

```sh
./install.sh                          # every game found in Steam's folder (or imported earlier)
./install.sh --games vc               # just Vice City: games already in the app are kept as they are
./install.sh --branch nightly         # "GTA Classics Nightly.app", following the nightly branch
```

| Option | |
|---|---|
| `--games gta3,vc,sa` | which games to build (default: every one found) |
| `--steam-dir DIR` | Steam's `steamapps/common` (default `~/Library/Application Support/Steam/steamapps/common`) |
| `--gta3-dir`, `--vc-dir`, `--sa-dir` | a game's folder (the one holding `gta3.exe`, `gta-vc.exe`, `gta_sa.exe`) |
| `--app-dir DIR` | where the app goes (default `~/Applications`) |
| `--branch main\|nightly` | which branch the app's updater follows |
| `--no-downloads` | no Steam store art: the launch window writes the titles |
| `--in-place` | read the games where they were found instead of importing them |

The games are the Mac Steam releases (GTA III 12230, Vice City 12240, San Andreas 12250), found as
`<steam-dir>/<game>/<Game>.app/Contents/Resources/transgaming/c_drive/Program Files/Rockstar Games/<GTAIII | Grand
Theft Auto Vice City | GTA San Andreas>`. With the game imported, the Steam copy can go. GTA III's radio and cutscene
audio and San Andreas's `msvcr71.dll`/`msvcp71.dll` come from the wrapper's `c_drive` and are copied beside the game.
A game whose port isn't in the checkout yet (no `recomp/build_<gta3|gtavc|gtasa>.sh`) is skipped. Saves and settings
stay in each game's data folder (`drive_c/Users/player/Documents/<GTA3 | GTA Vice City | GTA San Andreas> User Files`).

## The launch window

GTA Classics opens on a card per game, with Steam's library art for the chosen one (a game that isn't installed is
dimmed and says how to install it). ←/→ choose a game, ↑/↓ the tiles, Return or a double-click plays, Esc quits.

- **Graphics Options** (for every game): Display (Full Screen / Window), Notch (Below It / Around It, on a display
  with one), Resolution (Standard / Retina / As Set in Game: a 4:3 mode the height of the display), VSync.
- **Saves** and **Game Files** open the chosen game's folders. **Updates** rebuilds every installed game from the
  newest commit of the app's branch.

Play starts the game in the launcher's own process (`Contents/MacOS/GTA3`, `GTAVC` or `GTASA`), so macOS keeps Game
Mode on. Its log is `~/Games/<X>Native/<GTA3|GTAVC|GTASA>.log`; the launcher's choices are kept in
`~/Games/GTAClassics/launcher.plist` and the art in `~/Games/GTAClassics/art`.

## State

**In development.** The game starts on Metal; see [docs/GTA3-PORT.md](docs/GTA3-PORT.md) for what works.

| Part | State |
|---|---|
| Recompilation | `gta3.exe` 1.1 (Steam, 2010): 20,593 functions lifted, including RenderWare's `_rwcseg` |
| Graphics | Direct3D 8 on the Direct3D 9 → Metal layer (fixed function) |
| Sound | Miles Sound System on Core Audio (raw PCM effects, 3D samples, radio streams) |
| Input | DirectInput 8 keyboard and mouse |
| Movies | not yet (MPEG-1) |

## Building

```sh
recomp/build_gta3.sh "<GTAIII folder>"     # -> recomp/work/gta3
```

The GTAIII folder is `Grand Theft Auto 3.app/Contents/Resources/transgaming/c_drive/Program Files/Rockstar Games/GTAIII`
inside the Steam app. Needs the Xcode command line tools and Python 3.
