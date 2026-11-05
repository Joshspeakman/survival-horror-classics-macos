# Recompiler: GTA III, Vice City and San Andreas as native code

Static recompilation of the user's own Steam executables (`gta3.exe`, `gta-vc.exe`, `gta_sa.exe` and the DLLs San
Andreas loads) into C, compiled for Apple Silicon and linked with a Windows runtime that draws with Metal. Nothing
derived from the games is committed: the build scripts regenerate everything locally from the user's files. Installing
and playing: [README](../README.md); what is specific to these games: [docs/PORT.md](../docs/PORT.md).

```sh
recomp/build_gta3.sh "<GTAIII folder>"                       # -> recomp/work/gta3
recomp/build_gtavc.sh "<Grand Theft Auto Vice City folder>"  # -> recomp/work/gtavc
recomp/build_gtasa.sh "<GTA San Andreas folder>"             # -> recomp/work/gtasa
```

Each script checks the executable's SHA-256, undoes SteamStub 2.1 on a copy (`tools/steamstub21.py`), analyses and
lifts it (`tools/analyze.py`, `tools/lift.py`; San Andreas' DLLs as further images at their own bases), compiles
everything in parallel and links it with the runtime and host (`tools/build_runtime.sh`, `tools/build_app.sh`).
`GTA3_RELINK=1` (GTA III) or `GTA_RELINK=1` (Vice City, San Andreas) keeps the lifted objects and rebuilds only the
runtime and host.

The recompiler (analysis, lifter, execution model, x87, indirect calls, test harnesses) was inherited from the
Dungeon Siege II port; the runtime also carries modules of the earlier ports (DirectDraw, DirectPlay, HLSL effects,
a Linux host and renderer) that these games don't use. The Linux paths are not built or tested for these games.

## Status

| Part | State |
|---|---|
| Analysis and lifting | all three executables and San Andreas' six DLLs; RenderWare's hand-written x87 (`_rwcseg`) |
| Game-specific hooks (`tools/lift.py` `GTA3_HOOKS` / `GTAVC_HOOKS` / `GTASA_HOOKS`, `runtime/win32/gta3_mode.c`) | video modes, widescreen, frame rate, PS2 lighting, streaming: see [docs/PORT.md](../docs/PORT.md#game-specific-hooks) |
| Direct3D 8 and 9 (`runtime/win32/d3d8.inc`, `d3d9.c`, `d3d9_shader.c`) on the renderer in `../src/renderer` | fixed-function lighting, game vertex shaders via MojoShader, MSAA, 16-bit look |
| Sound | Miles (`miles.c`) and DirectSound (`dsound.c`) on Core Audio |
| Input | DirectInput 8 keyboard, mouse and game controller (`dinput8.c`, host `main.m`) |
| Host (`host/main.m`), launch window (`host/classics_launcher.m`), Mods window (`host/classics_mods.m`) | macOS (Apple Silicon) |

Test mode: `DS_TEST=1` (background window, no mouse capture, no launch window), `W32_AUDIO_MUTE=1`,
`DS_SCRIPT=...` or `DS_CMDFILE=<file>` (scripted input in the game's display-mode coordinates), `DS_SHOT_FINAL=1`.
Logs: `W32_TRACE=<file>` (every Windows call), `W32_SEHLOG=1`, `W32_AUDIOLOG=1`, `D3D9_FRAMELOG=<frame>`,
`D3D9_SHADERLOG=2`; more in [docs/PORT.md](../docs/PORT.md#checking). Crash and hang reports go to
`<data>/CrashReports`.

## Execution model

- **Memory:** one 4 GB host region; guest address = offset. The image sits at its preferred base `0x400000`, so all
  pointers the game stores (vtables, saves, its own structures) keep their 32-bit values.
- **Functions:** one C function per original function. Registers are C locals, written back to a per-thread context
  around calls. The guest stack is real (`call` pushes the original return address; arguments are read where the
  original reads them), so calling conventions, `this` pointers and stack-walking code behave as before.
- **Flags:** lazy — flag-setting instructions record their operands; consumers (`jcc`, `setcc`, `adc`, `sahf` …)
  compute only the flags they need.
- **x87:** eight doubles + TOP + control/status words, the precision model Dungeon Siege II was measured to run with
  under Rosetta: double arithmetic, float rounding at 32-bit stores, `fist(p)` honouring the rounding
  mode. x87 register forms are decoded from the opcode bytes (Intel semantics), not from disassembler text.
- **Indirect calls/jumps:** an address → function table; jump tables become `switch` statements; imports are dispatched
  through `rt_import` (IAT slots hold thunk addresses, so a call through a register still lands there).
- Junk decodes (data read as code) and padding become `rt_unhandled()` traps.

## Verification and known gaps

The instruction-level harness (`tests/harness.py`, `tests/fuzz_insn.py`, `tests/test_slice.py`) comes from the
Dungeon Siege 1 port, where every instruction form the game used was checked bit-for-bit against Unicorn.
`tests/fuzz_sse.py` checks MMX/SSE forms (on arm64 too), and `tests/replay.py` runs one call through the original
x86 and the recompiled code from a snapshot and compares them. How the games were checked: [docs/PORT.md](../docs/PORT.md#checking).

- **SEH:** access violations and RaiseException go through the game's handlers. First touches of reserved archive
  pages pass over C++ `catch(...)` frames (the game reads archives inside them); C++ catch continuations are not
  implemented, and the game has not needed them so far.
- **Indirect calls** to addresses outside the function table stop the game with the address logged (add it as a
  seed in `work/<game>/observed_targets.json` and rebuild).
- **x87 tag word** is not modelled; transcendentals use the host math library.

## A note from the author

These projects are roughly 90% built with AI, with human testing along the way. I started them because some of my
favorite games either ran badly on macOS or wouldn’t run at all, and Wine and CrossOver didn’t always get me where I
wanted.

I’m a 40-year-old dad with a full-time job. I don’t have the time or desire to write all of this from scratch, and AI
made it possible to get somewhere with it.

If that puts you off, no hard feelings. Use them, skip them, fork them. It’s all good. I would have happily used an
existing, human-written native port, but I couldn’t find one that did what I wanted. Mostly, I just want to play my
games.

The aim is straightforward native builds without unwanted extras or a stack of translation layers. Forks and
improvements are welcome.
