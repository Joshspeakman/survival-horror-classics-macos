# GTA III port: notes

What is specific to Grand Theft Auto III in this repository, and how each part was checked.

## The executable

- Steam app 12230 (the Mac release, TransGaming Cider build 7713). The game is the Windows `gta3.exe` 1.1 (PE32,
  2010-03, image base 0x400000, SizeOfImage 0x5ea000), sha256
  `7fd2749072b1d1b04737b44d7d8173c2c00cd419bb8f1983c0067578e6229267`, in
  `Grand Theft Auto 3.app/Contents/Resources/transgaming/c_drive/Program Files/Rockstar Games/GTAIII`.
- SteamStub 2.1 (`.bind`); `tools/steamstub21.py` undoes it on a copy in the build folder (entry point 0x5c7c60).
- Imports: d3d8, ddraw (DirectDrawCreateEx), dinput8, mss32 (56 functions: 2D/3D samples, streams), winmm, ole32
  (DirectShow for the movies), wsock32, user32/kernel32/advapi32.
- RenderWare keeps hand-written x87 code in `_rwcseg` (0x631000, flagged as data): control-word helpers, matrix
  multiplies and point/vector transforms. `tools/pe.py` makes the code range run from `.text` to its end;
  `tools/analyze.py` takes function starts only inside the two sections.

## Startup checks

- DirectX 8.1: the game loads `D3D8.DLL` and `dpnhpast.dll` (DirectPlay's NAT helper, new in 8.1); both are in the
  runtime's list of DLLs (`runtime/win32/core.c`).

## Direct3D 8 (`runtime/win32/d3d8.inc`)

On the Direct3D 9 layer (`d3d9.c`, which includes it): Direct3DCreate8 swaps every vtable for a Direct3D 8 one.
Same-argument methods are d3d9.c's; the others convert (texture-stage states 13-21/25 are sampler states,
SetStreamSource without offset, SetIndices with the base vertex, FVF through SetVertexShader, Direct3D 8 layouts of
present parameters, adapter identifier, caps and surface descriptions, state block tokens, CopyRects,
CreateImageSurface). Vertex and pixel shaders are not run (fixed function only).

## Fixed function in the Metal renderer (`src/renderer/renderer.m`, `shaders.metal`)

RenderWare's Direct3D 8 pipelines lean on parts of the fixed function the renderer (written for Direct3D 7 games that
draw unlit) did not have:
- Vertex lighting. Cars and peds are drawn with D3DRS_LIGHTING on, a material (SetMaterial) and directional/ambient
  lights (SetLight, LightEnable, D3DRS_AMBIENT); the car's paint colour is the material colour. The layer sends the
  material and lights (DSR_MATERIAL, DSR_LIGHT) and the vertex shader lights as Direct3D does (emissive + ambient +
  diffuse N.L, point/spot attenuation, specular when D3DRS_SPECULARENABLE, COLORVERTEX and the *MATERIALSOURCE
  states, normals through the inverse transpose of world*view). Without it every lit draw was white: cars had no paint
  colour, Vice City's cars came out solid white and its buildings near black. The world is prelit (vertex colours,
  lighting off) and unchanged.
- Generated texture coordinates. The car reflection pass (MatFX environment map) is a second, additive pass
  (SRCALPHA, ONE) with D3DTSS_TEXCOORDINDEX = TCI_CAMERASPACENORMAL, COUNT2 and a texture matrix that maps the
  camera-space normal's x, y into 0..1 (scale 0.5, -0.5, offset 0.5), MODULATE(texture, TFACTOR). The shader used to
  ignore the TCI bits and sampled the (mottled) reflection texture with the body's own UVs: white blotches all over
  the paint. Camera-space normal, position and reflection vector are now generated, on either stage (Vice City puts
  its reflection on stage 1); coordinate set 1 is passed to stage 1.
- Fog. The game uses table fog (D3DRS_FOGTABLEMODE linear); only vertex fog was drawn, so there was none.

Not bugs: the intro cutscene's heavy teal trails are the game's own motion blur (CMBlur's intro mode): the back buffer
is copied into a 1024x512 render-target texture each frame (CopyRects; `D3D8_COPYLOG=1` shows it) and laid over the
next frame with colour (100, 220, 230) at alpha 158, then a white pass at alpha 30. Vice City's main-menu backdrop is
its own `background` texture (fronten1.txd, DXT1 512x512): a grey mottled pattern.

## Sound (`runtime/win32/miles.c`)

Raw PCM sound effects (AIL_set_sample_type + AIL_set_sample_address, AIL_set_3D_sample_info), loop blocks, 2D pan,
stream position and pan; streams opened by file name when the game sets no file callbacks.

## Log

- 2026-11-04: SteamStub undone, 20,593 functions lifted (0 internal errors), all 171 imports implemented; Direct3D 8
  device created at 640x480.
