# Survival Horror Classics: Resident Evil 1-3 and Dino Crisis, native for Apple Silicon

Capcom's **Resident Evil** (1997 PC), **Resident Evil 2** (1999 PC), **Resident Evil 3: Nemesis** (2000 PC) and
**Dino Crisis** (2000 PC), statically recompiled from **your own GOG copies** into native arm64 code and drawn with
Metal: no Wine, no Rosetta, no emulator. One app, **Survival Horror Classics**, with a card for each game. It uses the
recompiler and Windows runtime of the author's GTA Classics and Dungeon Siege ports.

No game files are in this repository. The installer reads the games you bought: the GOG releases, whose programs carry
no copy protection (the Steam releases are wrapped in Enigma and are not used).

## Status

| Game | Program | State |
|---|---|---|
| Resident Evil | `ResidentEvil.exe` | Title, intro, the mansion; Direct3D 1 execute buffers, its own task system |
| Resident Evil 2 | `LeonU.exe`, `ClaireU.exe` | Menus, movies, 3D at 30 fps; room backgrounds missing ([#1](../../issues/1)) |
| Resident Evil 3: Nemesis | `ResidentEvil3.exe`, `RE3_MERCE.exe` | Plays: movies, menus, the city, 3D |
| Dino Crisis | `DINO.exe` | Title, briefing, movies, the facility (its demo) |

## Install

```sh
brew install innoextract ffmpeg          # unpacking the GOG installers; converting two games' movies
./install.sh --branch nightly            # every game found; or --games re1,re2,re3,dc
```

The games are read where they are: `<games root>/<Game>Native/game-files/<title>`, by default on the
*Expansion Pak* SD card (`/Volumes/Expansion Pak/Games`; `--games-root DIR` for another place). A game that is not
there yet is unpacked from its GOG installer (`setup_*.exe` and its `.bin` parts in `--installers DIR`, by default
`/Volumes/Expansion Pak/GOG Installers`). Saves and settings stay on the Mac in `~/Games/SurvivalHorrorClassics`.
When the card is out, the launch window says to insert it.

Resident Evil's Cinepak movies and Resident Evil 2's Indeo 5 movies get MPEG-1 copies beside them (`<file>.mpg`,
`recomp/tools/convert_movies.sh`, ffmpeg while it runs); Resident Evil 3 and Dino Crisis are MPEG-1 already. The
originals are not changed.

## The launch window

A card per game, Resident Evil 2's **Leon / Claire** and Resident Evil 3's **game / The Mercenaries** under it, and:

- **Options**: Display (full screen with Game Mode, or a window), Around the Notch, VSync, Texture Filtering,
  Movies, Test Aids (Resident Evil 2: GOG's own invincibility and all-weapons switches).
- **Saves**, **Game Files**, **Updates** (rebuilds the app from the branch it follows), **Quit**, **Play**.

Controllers: the one in use last (Xbox, PlayStation, ...) is the games' joystick, through DirectInput (Resident Evil
3, Dino Crisis) or winmm (Resident Evil, Resident Evil 2); Menu is Escape. The games' own controller settings map it.

## How it works

See [docs/PORT.md](docs/PORT.md): the recompiler, DirectDraw 1-4 and Direct3D 1-3 on Metal, GOG's patches, the movie
player, Resident Evil's task system, and what each game needed.
