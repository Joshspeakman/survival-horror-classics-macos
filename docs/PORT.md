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

## Logs (development)

`DSR_LOG=1` (DirectDraw: surfaces, locks, texture handles, keys, clears, presents), `DSR_DRAWSTATE=1` (each new
draw state; 2: by vertex alpha and texture size too), `DSR_DRAWSEQ=<s>,<n>` (+`_TEX=<id>`: draws in order), `DSR_BLTLOG=1`,
`DSR_TEXDUMP=<folder>` (textures as PPM), `DSR_VLOG=x0,x1`, `DSR_RSLOG=1`, `W32_MARNILOG=1` (Resident Evil 2's renderer errors), `W32_FILELOG=1`, `W32_JOYLOG=1`, `DSR_STAGELOG=1`, `W32_MOVIELOG=1`, `W32_AUDIOLOG=1`, `RE1_TASKLOG=1`, `W32_RELOG=1`, `W32_TRACE=<file>`
(+ `W32_TRACE_ONLY=ddraw,...`), `W32_SEHLOG=1`, `DS_TRACE_HOOKS=<addrs>` at build time with `DS_HOOKTRACE=1`.
