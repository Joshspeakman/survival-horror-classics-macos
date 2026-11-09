# How the port works

Each game's Windows program (x86, 1997-2000) is translated function by function into C (`recomp/tools/analyze.py`,
`lift.py`), compiled for arm64 and linked with a Windows runtime written for these ports (`recomp/runtime`): the
Windows calls the games make, DirectDraw and Direct3D drawn with Metal (`src/renderer`), DirectSound and DirectInput
on macOS's audio and GameController. The game's data is read from its own folder.

Build one program: `recomp/build_re.sh <re1|re2|re2c|re3|re3m|dc> "<game folder>"` (`RE_RELINK=1`: runtime changes
only) into `recomp/work/<id>/app/`. Development runs: `recomp/run_re.sh <id> <seconds> [VAR=value...]` (test mode: a
background window, muted, screenshots by `DS_SCRIPT`).

## GOG's releases

- The programs are the original executables with GOG's fixes as **code caves**: past `.text`'s VirtualSize (the rest
  of its last page), and in Dino Crisis inside `.rdata`, which GOG made executable. The recompiler covers every
  executable section to the end of its raw data (`pe.py code_range`), and the loader maps those bytes as Windows does.
- GOG's `ddraw.dll`, `dinput.dll` and `dshow.dll` are compatibility shims: not recompiled. `dshow.dll` re-exports COM's
  entry points (the loader maps it to ole32).
- Settings are registry values written by GOG's installer: the runtime reads `goggame-*.script` from the game folder
  and writes its `setRegistry` values at start (`registry.c`), keeping values the game has written since.
  Resident Evil 2 also needs Capcom's `Install Path` (GOG's script leaves it out) and Windows' `ProgramFilesDir`.

## DirectDraw and Direct3D (`recomp/runtime/win32/ddraw.c`, `ddraw_old.inc`)

The runtime's DirectDraw 7 / Direct3D 7 is the base; the older interfaces are vtables over the same objects:

- IDirectDraw, 2, 4 and IDirectDrawSurface 1-4 (prefixes of the version 7 vtables; the 108-byte DDSURFACEDESC and
  4-byte DDSCAPS of versions 1-3), clippers.
- 16-bit surfaces (565, 555, 1555, 4444) converted to the GPU's 32-bit on upload and back on readback; source colour
  keys upload as alpha 0 (a keyed copy discards them; a draw with COLORKEYENABLE, or any Direct3D 1 draw, adds an
  alpha test). GetDC / ReleaseDC give GDI the guest pixels (Resident Evil 3 copies its texture pages with BitBlt).
- Direct3D 2 and 3: devices enumerated as DirectX 5-6 did (Ramp, RGB, MMX, HAL: Resident Evil 2 picks by position),
  viewports, materials and lights as objects, texture handles, the render states of before texture stages
  (TEXTUREMAPBLEND, filters, COLORKEYENABLE) as stage 0's state, Begin/Vertex/End, vertex types and FVF codes.
- Direct3D 1 (Resident Evil): execute buffers, interpreted at Execute (process vertices, triangles, lines and points,
  states, matrices, texture loads, branches).
- A copy onto the primary surface covering at least a quarter of it presents it.
- Textures not created in system memory report DDSCAPS_VIDEOMEMORY, as DirectDraw does: Resident Evil 2's renderer
  (Capcom's Marni) reads each texture's caps back and draws room backgrounds only from textures in video memory.
- A plain copy into a texture with a colour key is uploaded with that key (Dino Crisis fills its font and sprite pages
  by Blt from system-memory surfaces that have none).
- A point-sampled screen-space draw has its texture coordinates moved a sixteenth of a texel inwards: its first pixels
  land exactly on a texel edge, and Resident Evil 2's foreground tiles (side by side on 256x256 sheets) showed the
  neighbouring tile as thin lines along their first row or column (`DSR_NO_TEXEL_BIAS=1`: the old picture).
- A screen-space draw whose texture coordinates stay inside the texture samples it clamped (Resident Evil's background
  tiles start at u = -0.0001: wrapped and filtered, a dark column showed where two tiles meet).

## Movies (`amstream.inc`, `dshow.c`)

- Resident Evil 3 and Dino Crisis play MPEG-1 through DirectShow's multimedia streams into DirectDraw surfaces:
  `CLSID_AMMultiMediaStream`, decoded by pl_mpeg (`recomp/third_party/pl_mpeg`, MIT), the sound from a second decoder.
- Resident Evil 2 renders its movies with a DirectShow graph and a video window: the host plays the converted copy
  with AVFoundation in a layer over the game.
- `RE_NO_MOVIES=1` skips them.

## What else each game needed

- **Resident Evil 3**: its task switcher enters a critical section on one thread and leaves it on another, which
  Windows allows: critical sections are an owner and a count (`core.c`).
- **Resident Evil 2**: GOG forces display mode 1 (320x240 copied small); the launcher passes `-mode0` (640x480).
  GOG's command-line options: `-menuN -modeN -t0 -t1 -t2 -vc -soff -moff -specialN -lifeN -wepallN -ushinabe -new`
  (the digit right after the name). A callback found only by the gap sweep needed the analyzer to validate its
  candidates there too.
- **Resident Evil**: its C runtime calls `__finally` blocks as local subroutines (`push label; jmp block`), kept
  inside their function by the lifter; its cooperative tasks switch stacks by hand, so each task runs on a host
  thread of its own and the switch points hand over (`re1_tasks.c`).

## Test aids (`recomp/runtime/win32/re_aids.c`)

Health (held each frame by the host): Resident Evil 0xbe636c (s16; held at the most seen this game: Chris 140, Jill
96), Resident Evil 2 0x994646 / Claire 0x9946c6 (200), Resident Evil 3 and The Mercenaries 0xa5ccc0 (its maximum at
0xa5ccc2). Room jump: a hook just before each game's room loader reads the stage and room (Resident Evil 0x477da2,
bytes 0xbe9820/1; Resident Evil 2 0x4e1ecd / Claire 0x4e212d, words 0x999114/6 and 0x999194/6; Resident Evil 3 0x4684f8
/ The Mercenaries 0x4685d8, words 0xa61fa6/8, a jump to another stage loading that stage first as a door does). They
were found by watching memory as the player is hurt and as rooms load (DS_SCRIPT dump/peek/pin, W32_FILELOG's return
addresses) and confirmed by holding them.

## Mods (`recomp/host/horror_mods.m`, `files.c`, `re_mods.c`)

The launcher passes the enabled mods' folders, first wins, in `GTA_MODS` (the shared runtime's name for it). Resident
Evil, Resident Evil 2 and Dino Crisis open loose files with `CreateFileA` under the game folder. The file layer looks
for each one in the mods before the game folder, any case (`files.c`, as for the GTA port). Writes still go to the
overlay in the data folder.

Resident Evil 3 reads everything through its archive reader (the object at 0x5349dc). The reader's own methods are
open(name) 0x4214c0, close 0x414f10, read 0x414f70 and seek 0x414fd0, the same in both programs. They are thiscall
(the reader in ecx, arguments popped by the method), and the game's own handles are 0. File loaders reach them
through cdecl wrappers (0x507b00/20/40/60; The Mercenaries 0x507b70/90/b0/d0), voice streams through the sound
object (0x5349e0, its stream open 0x506e70).

The methods are taken over (`re_mods.c`, `lift.py` RE3_OVERRIDES). A name the enabled mods have
(`/data/pld/pl00.pld`, `/data_a/voice/m101a010.wav`, below an optional `bio19/`) opens the mod's file with a handle of
0x4d4f0000 + slot. Seek, read and close then serve it as plain bytes, as the reader returns an entry once unpacked.
Everything else goes to the game's reader.

`W32_ROFSDUMP=<folder>` with `W32_ROFSDUMP_LIST=<names>` saves listed files unpacked by the game's own reader;
Restore from Disc uses it for the PC voices. `W32_ROFSVOICE=<name>` starts a voice line as a scene would.

The installer reads each `Rofs<n>.dat` directory to know where a bare file goes. The header holds the folder name
(`DATA`, `PLD`), then at 8 × its offset comes the directory: a count, then per file its offset / 8, size and name. A
new game's requests: `W32_ROFSLOG=1`.

## Sound and movies: what each game needed

- **Resident Evil's sounds** load through `mmio` (its wave reader seeks back to find the `data` chunk): `mmioSeek` and
  `mmioSetInfo` are real (`fnv_imports.c`). They were stubs before, so every sound was an empty 20-byte buffer.
- **Resident Evil's movies** play through MCI's AVI device ("avivideo"). `winmm.c` has one on the host's player:
  - `MCI_OPEN` of the device, then of the movie as a child window;
  - `MCI_STATUS` of that window;
  - `MCI_PLAY` with `MCI_NOTIFY` (`MM_MCINOTIFY` posted to the game's window at the end);
  - `MCI_STOP` / `MCI_CLOSE` when skipped.

  The movie played is `<file>.mpg`: a mod's first, then the installer's MPEG-1 copy. Before this, each movie ended in
  the game's "Failed to initialize the video sequence.(13)", which test mode closes unseen.
- **Resident Evil 2** opens an ACM stream for every `.SAP`. PCM sources are converted at their own rate
  (`df_imports.c`), so a mod's `.SAP` can hold plain PCM.
- **Movie lookups** (`amstream.inc`, `dshow.c`, the MCI device) try `<file>.mpg` through the file layer, so mods can
  supply movies.
- **Dino Crisis's music never played** before 2026-11-07. Its music player streams each `Sound/BGM` track through a
  3-second DirectSound buffer, refilled when the play cursor passes the positions it registers through
  `IDirectSoundNotify` (17 of them). The runtime had no such interface, so the open failed (`E_NOINTERFACE`) and the
  game played no music at all. `dsound.c` now gives every secondary buffer an `IDirectSoundNotify` (the object's fourth
  interface, +24): the mixer sets each event as the play cursor passes its offset, and the stop ones on `Stop` or at
  a one-shot buffer's end. A dump of the played stream (`W32_DSDUMP`) matches the file sample for sample, through
  the loop.
- **Dino Crisis's loop points** are in its music table (`DINO.exe` 0x625448: 16-byte rows of id, flags, name, loop
  point; flags 0x08 once, 0x0a loop the whole file, 0x2a loop from the point), in sample frames of the PC's 22050 Hz
  file. The player turns them into a byte offset with the file's own format (0x4049a9). A mod at another rate would
  loop in the wrong place, so `re_mods.c` takes over the game's first sound-table setup (0x4043e0, `lift.py`) and
  scales the loop point of each track a mod replaces to the mod file's rate. `W32_DC_MUSIC_AS=mf_16=me_00SL`
  (development) plays the second track where the game plays the first, to hear a loop early on.
- **Sound buffers**: up to 4096 at once (`NBUF`). Dino Crisis duplicates a buffer for every effect it plays and
  frees them with the room; a long stretch held about 800, past the old 512, after which new sounds failed.

## Logs (development)

`DSR_LOG=1` (DirectDraw: surfaces, locks, texture handles, keys, clears, presents), `DSR_DRAWSTATE=1` (each new
draw state; 2: by vertex alpha and texture size too), `DSR_DRAWSEQ=<s>,<n>` (+`_TEX=<id>`: draws in order), `DSR_QUADLOG=<s>` (every vertex of one frame's screen-space draws), `DSR_BLTLOG=1`,
`DSR_TEXDUMP=<folder>` (textures as PPM), `DSR_VLOG=x0,x1`, `DSR_RSLOG=1`, `W32_MARNILOG=1` (Resident Evil 2's renderer errors), `W32_FILELOG=1`, `W32_ROFSLOG=1` (Resident Evil 3's archive reads and mod files), `W32_JOYLOG=1`, `DSR_STAGELOG=1`, `W32_MOVIELOG=1`, `W32_AUDIOLOG=1`, `W32_DSLOG=1` (DirectSound buffers and notifications), `W32_DSDUMP=<folder>` (each buffer as played), `RE1_TASKLOG=1`, `W32_RELOG=1`, `W32_TRACE=<file>`
(+ `W32_TRACE_ONLY=ddraw,...`), `W32_SEHLOG=1`, `DS_TRACE_HOOKS=<addrs>` at build time with `DS_HOOKTRACE=1`.
