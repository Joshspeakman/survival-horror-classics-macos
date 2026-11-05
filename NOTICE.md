# Notices and credits

**Grand Theft Auto III**, **Grand Theft Auto: Vice City** and **Grand Theft Auto: San Andreas** © Rockstar Games /
Take-Two Interactive. This project contains no part of them. The build recompiles the executables of copies the user
owns (bought on Steam), after verifying their checksums; it undoes the Steam wrapper on copies in the local build
folder so the code can be read, and the app reads the games' data from the user's copies. The recompiled programs are
for that user only. The launch window's art is downloaded from the games' Steam store pages at install time and kept
on the user's Mac. *Restore from Disc* reads the user's own San Andreas DVD on their Mac; nothing from it is part of
this project. This project does not remove or work around SecuROM or online activation (the Mac release of Vice City
is not used for that reason).

This repository started from the Dungeon Siege II / Fallout: New Vegas / The Sims / Delta Force recompiler and runtime.
No part of those games is included either.

Prior work this project follows:
- **FusionFix** by ThirteenAG (https://github.com/ThirteenAG/WidescreenFixesPack, MIT): the widescreen (Hor+) and high
  frame rate fixes are ported from it, and its search patterns found the same code in these builds.
- **SkyGfx** by aap (https://github.com/aap/skygfx): its descriptions of the PS2's rendering (San Andreas' colour filter
  and radiosity, the light clamping, the trails) are what the PS2 lighting option reproduces. SkyGfx has no licence,
  so its formulas were reimplemented here and none of its code is used.
- **Steamless** by atom0s (https://github.com/atom0s/Steamless) documents the SteamStub 2.1 format;
  `recomp/tools/steamstub21.py` is an independent implementation from that description, not a copy of its code.
- `src/dsr/dsr_snapshot.bin` records device capabilities reported by Wine's DirectDraw implementation (Wine, LGPL).

Included:
- **MojoShader** (zlib): `recomp/third_party/mojoshader`, translates the games' vertex shaders to Metal.
- **minimp3** (CC0): `recomp/third_party/minimp3`.
- **Vulkan-Headers** (Apache-2.0 / MIT, The Khronos Group): `recomp/third_party/vulkan`, inherited (not used by these
  games).

Used at build time (installed into `recomp/.venv` with pip, not redistributed here):
- **Capstone** (BSD) — disassembly for the recompiler. https://www.capstone-engine.org/
- **Unicorn** (GPL-2.0) — x86 emulation, used only by the tests. https://www.unicorn-engine.org/
- **unshield** (MIT, Homebrew) — reads the San Andreas DVD's installer archives for *Restore from Disc*.
