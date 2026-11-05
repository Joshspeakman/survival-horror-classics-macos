# How the port works

What is specific to GTA III, Vice City and San Andreas in this repository, and how each part was checked. The
recompiler itself (analysis, lifting, the execution model) is described in [recomp/README.md](../recomp/README.md).

## The executables

| | GTA III | Vice City | San Andreas |
|---|---|---|---|
| Steam release | Mac, app 12230 (TransGaming Cider build 7713) | Windows, app 12110, depot 12111 | Mac, app 12250 |
| Executable | `gta3.exe` 1.1 (2010), sha256 `7fd27490…9267` | `gta-vc.exe` 1.1 for Steam, sha256 `8c2318d6…848f` | `gta_sa.exe` 3.0 (`recomp/tools/gtasa.sha256`) |
| Wrapper | SteamStub 2.1, undone on a copy (`tools/steamstub21.py`) | SteamStub 2.1 | SteamStub 2.1 |
| Images lifted | the exe | the exe | the exe + `msvcr71.dll`, `msvcp71.dll` (rebased 0x14000000), `vorbisfile.dll`, `vorbis.dll` (0x11000000), `ogg.dll` (0x12000000), `eax.dll` (0x13000000) |
| Graphics | Direct3D 8 | Direct3D 8 | Direct3D 9 (with vertex shaders) |
| Sound | Miles (`mss32`) | Miles | DirectSound |

The Mac release of Vice City carries SecuROM 7 with online activation inside its wrapper; this project does not touch
it and uses the Windows release instead. Each build script checks the executable's checksum. RenderWare keeps
hand-written x87 code in GTA III's `_rwcseg` (flagged as data): `tools/pe.py` treats the range as code.

## Graphics

- **Direct3D 8** (`recomp/runtime/win32/d3d8.inc`) runs on the Direct3D 9 layer (`d3d9.c`): `Direct3DCreate8` swaps
  every vtable for a Direct3D 8 one; methods with the same arguments are d3d9.c's, the others convert (sampler states
  that were texture-stage states, SetStreamSource without offset, the FVF through SetVertexShader, the Direct3D 8
  layouts of present parameters, caps and surface descriptions, state block tokens). `CopyRects` from a picture the
  GPU drew is a GPU copy (the Trails copy the screen every frame).
- **Direct3D 9 on Metal** (`src/renderer`): fixed-function lighting (materials, directional/point/spot lights,
  COLORVERTEX and the material sources), generated texture coordinates (the cars' environment maps), table fog,
  blend operations, multisampled targets resolved at the end of each pass, and San Andreas' own vertex shaders
  translated to Metal by MojoShader (`d3d9_shader.c`: peds and CJ skinned on the GPU).
- **Full screen**: the window takes the mode's size before the device is made (RenderWare checks the client rectangle).

## Game-specific hooks

Hooks run before a chosen instruction of the original code (tools/lift.py `GTA3_HOOKS`, `GTAVC_HOOKS`, `GTASA_HOOKS`;
the code in `recomp/runtime/win32/gta3_mode.c`); overrides replace a whole function (`GTA3_OVERRIDES`,
`GTAVC_OVERRIDES`). Adding one needs a full build of that game.

| What | GTA III | Vice City | San Andreas |
|---|---|---|---|
| The launcher's video mode, colour depth and antialiasing, before the saved mode is read (`psSelectDevice`) | 0x5829f8 | 0x600a95 | 0x74daec (+ AA setting 0xb880b8) |
| Every shape of video mode listed | | | 0x74d3be |
| Widescreen: `CameraSize`, the frustum's half angle, the aim aspect | 0x527b00, 0x46f4c9, constants 0x5fe9c0/bc | 0x580690, 0x46c2dc, 0x54a1ba | 0x7389a0, 0x518350, 0x70b0aa/bb/c8 |
| The games' Widescreen setting (followed and kept 0 in play) | 0x96e05b | 0x86865a | 0xb8803b |
| Frame limiter (RsGlobal.maxFPS, the setting) | 0x9055a8, 0x600e20 | 0x9b38f4, 0x86865d | 0xbf8504, 0xb8803c |
| High frame rates (time steps, frame counters, particles) | ~50 sites, `CParticle::Update` 0x50e630 | ~70 sites, 0x561d00 | 134 sites |
| PS2 trails kept on (`CMBlur::BlurOn`) | (Display Setup) | 0x48fdbf, 0x49975f | |
| PS2 colour filter passes; radiosity after them | | | 0x718a6c, 0x718abd, 0x718ac0 |
| The streaming read's lost release (`CdStreamSync`) | 0x40604a | 0x40840a | 0x407a9e |

Their addresses were found with FusionFix's (MIT) search patterns, and checked by disassembling each build.

## Sound

- **Miles** (`recomp/runtime/win32/miles.c`): 2D and 3D samples (distance attenuation and panning, as Miles' fast 2D
  positional provider), raw PCM effects, loop blocks, timers and end-of-sample callbacks on a guest thread, and
  streams. The games open every stream at start to learn its length, so a stream's header is read when it is opened
  and its audio is decoded only while it plays, from the play position, a few seconds ahead into a ring. Vice City's
  radio (`.adf`) is read through the game's own file callbacks. The 3D provider says it plays 64 samples at once:
  GTA III turned its sound off at the stub's former answer, 0, and Vice City wants a DirectSound3D provider by name,
  so the three a PC of the time listed are offered.
- **DirectSound** (San Andreas): `recomp/runtime/win32/dsound.c`, on Core Audio.

## Checking

Test mode (`DS_TEST=1`) runs a game in the background with scripted input: `DS_SCRIPT="frame:key:13;frame:shot:<png>"`
or `DS_CMDFILE=<file>` (lines appended while it runs: `key:13`, `shot:<png>`, `peek:<address>`); `DS_SHOT_FINAL=1`
saves screenshots as shown (after gamma and the 16-bit look); `W32_AUDIO_MUTE=1` mixes but plays nothing and
`W32_AUDIOLOG=1` reports the output level each second; `GTA_FAKE_PAD="frame:buttons:lx:ly;..."` is a controller.
For rendering: `D3D9_FRAMELOG=<frame>` (or `D3D9_FRAMELOG_FILE=<path>`: the frame after the file appears) logs one
frame's render targets, copies, clears and draws, reading small targets back; with `D3D9_PROBE=x,y` it names each
draw that changes that pixel. `D3D8_COPYLOG=1` logs Direct3D 8 copies; `D3D9_SHADERLOG=2` prints the translated
shaders.

Checked this way at 1600×900, 2054×1290 and 4112×2658: all three games from the menus into the opening missions;
widescreen on and off from each game's menu; resolution, colour depth and antialiasing from San Andreas' menu; 16-bit
pictures have exactly 32/64/32 levels; sound in GTA III and Vice City (cutscene, ambience, effects, Flash FM in a car)
by the mixer's output level; 11-minute runs at 60 fps without a fault.

Not faults: GTA III's intro cutscene's heavy teal trails are the game's own motion blur (its intro mode). Vice City's
menu backdrop is its own grey `background` texture. The blue cone over the BMX in San Andreas' first alley is the
mission's objective marker.
