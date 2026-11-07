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
| Resident Evil | `ResidentEvil.exe` | Plays: title, intro, the mansion; Direct3D 1 execute buffers, its own task system |
| Resident Evil 2 | `LeonU.exe`, `ClaireU.exe` | Plays: movies, menus, the city and the police station with their backgrounds, 3D at 30 fps |
| Resident Evil 3: Nemesis | `ResidentEvil3.exe`, `RE3_MERCE.exe` | Plays: movies, menus, the city, 3D |
| Dino Crisis | `DINO.exe` | Plays: title, briefing, movies, the facility; its text and sprites keyed as on Windows |

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

A card per game showing its own title screen, over that title screen blurred behind frosted glass (captured from your
copy at install; GOG's store banner from the game folder until then). Under the cards: Resident Evil 2's **Leon /
Claire** and Resident Evil 3's **game / The Mercenaries**. Then:

- **Options**: Display (full screen with Game Mode, or a window), Around the Notch (below it, or around it), VSync,
  Texture Filtering, Movies, Test Aids.
- **Saves**, **Game Files**, **Updates** (rebuilds the app from the branch it follows), **Quit**, **Play**.

A controller works in the launch window: d-pad or left stick to choose a game (up/down: the version), A or Start to
play, Y for Options (up/down a row, left/right/A to change it), B to quit or close Options. The keyboard does the same
with the arrows, Return, Space and Escape.

## Controllers

Any controller macOS knows (Xbox, PlayStation, Switch Pro, MFi) is the games' joystick: DirectInput's gamepad for
Resident Evil 3 and Dino Crisis, winmm's joystick for Resident Evil and Resident Evil 2. It is always there for the
games, idle until a controller connects, so one turned on after the game started still works. The d-pad also drives
the left stick (the games read the stick to walk and in menus); Menu is Escape. Buttons are mapped in each game's own
controller settings, as on Windows.

## Frame rate

The four games run their world in fixed 30-frame steps (menus at 60); a higher rate would only make them run faster, so
the frame rate stays the games' own. VSync paces the frames to the display (ProMotion included), with Game Mode in full
screen.

## Test Aids

Options > Test Aids (or `SHC_AIDS=1`): the player's health is held full (Resident Evil, Resident Evil 2, Resident Evil
3, The Mercenaries), and Resident Evil 2 starts with every weapon (GOG's own `-wepall`). For testing a room:
`SHC_ROOM=<stage><room>` (as the room files are named, `101` = stage 1 room 01) makes the first room a new game loads
that one (Resident Evil, 2 and 3), and `SHC_ROOM_AT=<n>` the n-th. `recomp/tools/re_qa.sh <folder>` is the QA pass: each
version from the installed app through a new game and a set of rooms, with a contact sheet and a report.

## How it works

See [docs/PORT.md](docs/PORT.md): the recompiler, DirectDraw 1-4 and Direct3D 1-3 on Metal, GOG's patches, the movie
player, Resident Evil's task system, and what each game needed.
