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
  blend operations, the texture stages as shaders specialised per combination (function constants), multisampled
  targets kept in tile memory and resolved as each pass ends, depth only in the passes whose draws use it, new targets
  cleared to black, and San Andreas' own vertex shaders
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
| The frame limiter paced by a steady clock | 0x583be3, 0x583bff | 0x6000e0, 0x6000fc | 0x75072d, 0x750744 |
| The car camera's lean with the steering kept off (`TheCamera`, each frame) | | 0x7e3690 | |
| `Idle`'s own wait for 14 ms since the last frame, ended when the launcher sets the frame rate | | | 0x54265a |
| Modern: the car camera swung round the car by the right stick (`TheCamera`'s active camera, each frame) | 0x70be38 | 0x7e3690 | |

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

## Movies

The intro movies (`movies/Logo.mpg`, `GTAtitles.mpg`: MPEG-1 with MP2 sound) are played by the host with AVFoundation
in a layer over the game's picture (`host/main.m` `gta_movie_play`). The Mac releases of GTA III and San Andreas ask
TransGaming's wrapper for `TGPlayIntros` (user32) before they make their window; the runtime provides it
(`gta_imports.c`) and the host shows the movies in a window of their own, where a key, a click or a pad button skips
one. Those games still look for their own DirectShow graph when they are made active while their state is a movie's
(their window procedures call its IMediaControl unchecked), so activation that arrives then waits until the state moves
on (`gta3_mode.c` `gta_hold_message`): skipping the intros a few seconds in crashed San Andreas. Vice City, the Windows release, plays them through DirectShow (`dshow.c`), which completes the graph when the movie
ends or is stopped (it skips them itself when the app isn't in front). `GTA_NO_MOVIES=1` skips them.

## Checking

Test mode (`DS_TEST=1`) runs a game in the background with scripted input: `DS_SCRIPT="frame:key:13;frame:shot:<png>"`
or `DS_CMDFILE=<file>` (lines appended while it runs: `key:13`, `shot:<png>`, `peek:<address>`); `DS_SHOT_FINAL=1`
saves screenshots as shown (after gamma and the 16-bit look); `W32_AUDIO_MUTE=1` mixes but plays nothing and
`W32_AUDIOLOG=1` reports the output level each second; `GTA_FAKE_PAD="frame:buttons:lx:ly[:rx:ry];..."` is a
controller (`@seconds` in place of a frame number: by time).

Effects QA: `recomp/tools/gta_qa.py <gta3|vc|sa> <plan>` plays a game into its first scene and goes through a plan of
scenes (weather and time by the games' own cheats, typed key by key, and the clock held; night and infrared vision by
their flags; San Andreas teleports), a screenshot of each in `recomp/work/qa`. The command file also takes `poke:addr=byte`,
`pin:` / `unpin:` (a byte held every frame), `dump:addr,len,file`, `tp:x,y,z` (San Andreas: the game's own
CEntity::Teleport) and `gta:weather,N` (the forced, new and old weather at once), `gta:vhealth,F` (the player's
vehicle's health: fire below 250, an explosion at 0) and `gta:vspeed,F,frames` (driven forward: trails, speed blur);
`GTA_POKE=addr=byte,...` holds bytes from the start; `DS_EARLY_MSG` posts window messages as the window is made.

Its options: `--msaa`, `--mode`, `--light`, `--controls`, `--fps` (the cap), `--no-shots` (each screenshot stalls the
renderer, about 140 ms at 2056×1290: for timing runs), `--movies "0.5,3"` (the intro movies played and each skipped that
many seconds in, `DS_MOVIE_SKIP`; `gta3-start`, `sa-start`), `--keep`. Each run's summary lists crashes (the game's and
the host's; their reports are copied out of the data folder), the frame rate, the game's heap in play (a steady climb
is flagged) and the CPU it used. A controller is connected from the start (`GTA_FAKE_PAD`), driven by `pad:` lines. The
soaks tour each map (`sa-soak`; `gta3-soak` and `vc-soak` from road pieces in the games' own `data/maps/*.ipl`, with
`tp:` through `Teleport`, virtual 11 in GTA III and Vice City; `…-soak2` twice round, to tell streaming from a leak);
`…-aim` tries Modern aiming; `sa-effects3` San Andreas' screen effects by their switches. `DS_TEST_FAULT=audio` faults
the sound mixer's thread (the crash report must name it).

For speed: `DS_BENCH=1` shows each frame in a texture instead of the (background, compositor-paced) window, so the
frame rate is what the game and GPU manage; `DSR_PROFILE=1` counts each frame's GPU passes by kind and the waits for
the GPU (each pass in a command buffer of its own: their times are only relative); `DSR_PASSLOG=<frame>` lists that
frame's passes and copies; `DSR_RINGLOG=1` the upload ring's use; `DSR_STATS=1` the commands a second and the time the
game waited for the GPU; `W32_SLEEPLOG=1` who calls `Sleep`; `kill -INFO <pid>` writes every guest thread's stack to the
data folder's CrashReports. `W32_PROFILE=<file> W32_PROFILE_GAME=1` samples the game's main thread every millisecond
from a thread of its own (suspended, its registers read and its frame chain walked; the plain `W32_PROFILE` signal lands
on whichever thread macOS picks, often a sleeping one): `tools/profile.py <binary> <file>` lists where the time went,
inclusive, `PROFILE_CALLERS=f_<address>` who called a function, and with `GTA_SLOWLOG=<ms>` (frames whose work took
longer are logged) `PROFILE_WINDOWS=<game.log>` only those frames. That is how San Andreas' `Idle` wait was found.
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
