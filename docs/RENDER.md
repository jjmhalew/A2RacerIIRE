# A2 Racer II - 3D render pipeline (spel.dat)

Scope: Direct3D setup, render states, projection/viewport, per-frame draw order, fog, sky backdrop, cameras and the
alpha-blended effects (clouds, skid marks, sparks, object lights, particles). Mesh/road formats, the 2D HUD and image
formats are documented elsewhere. Addresses are spel.dat virtual addresses; names are in
[names/spel_render.txt](names/spel_render.txt). "(?)" marks an uncertain item.

## 1. Modes and device

`Raser.ini [PROGRAM] RESOLUTION` (global `[0x4895b0]`) selects everything, see `0x423d20` init_game and `0x420770`:

| RESOLUTION | Screen | `g_res_index 0x4d9a6c` | `g_8bit 0x4d9a70` | `g_hw3d 0x4d9aac` | D3D device |
|---|---|---|---|---|---|
| 0 | 320x200x8 | 0 | 1 | 0 | RAMP (software) |
| 1 | 640x480x8 | 1 | 1 | 0 | RAMP (software) |
| 2, 3 (default 3) | 640x480x16 | 1 | 0 | 1 | HAL, falls back to RGB |
| 5 (hidden) | 16 bit, `0x44c750(1)` forces the RGB emulation device | 0 | 0 | 0 | RGB (software) (?) |

`g_hw3d` (the "accelerated" path, TGA textures) switches on: the textured sky backdrop, MODULATE texture blending,
bilinear filtering, Gouraud shade mode, fog, clouds/lights/skid marks (alpha caps permitting). The 8-bit modes are
the old software renderer path (2D sky blit, TEXTUREMAPBLEND COPY, nearest filtering).

What `g_hw_accel` 0 / `g_8bit` 1 change, call site by call site (xrefs of `0x4d9aac`, `0x4d9a70`, `0x4d9a6c`):

| Where | 8-bit modes (RESOLUTION 0, 1) |
|---|---|
| `init_3d_library` 0x4234e0 | `texture_set_partition(8.0)` and the accumulator texture `init.pcx` (all textures in 256x256 slots of one 2048x2048 surface); no backdrop image, no `texture_set_source_type(1)` (so the **.pcx** files), no colour key call, no alpha caps check; then `load_game_palette` 0x420900 loads the etappe's `e0N_pal.pcx` (`[PALET] ETAPPEn` first name) into the hardware palette |
| `rceng_set_hw_uv` 0x441e90 | the road's UVs remapped into the accumulator's slots (the same picture) |
| `load_anim_config` 0x4236d0 | the `*8_packed` / `*8` preload lists instead of `*16` (which anims are loaded; same meshes and textures) |
| `set_game_render_states` 0x423e40 | TEXTUREMAPBLEND 7 COPY (`0x423e94`), FILTER: NEAREST (LINEAR needs g_hw3d), no SHADEMODE GOURAUD |
| `gfx_asci_init_fonts` 0x418330 | font set 0 (`lo_*`) with its own spacing; at g_res_index 0 `[0x4a0268]` = 0: glyphs are not put at even x (HUD.md 2) |
| `hud_load_anims` 0x41bd50 | the 8-bit sprite list 0x496118; layout row 0 (HUD.md) |
| `sky_init_moving_sky` 0x41e740 / `render_game` 0x4250b1 | the 2D sky blit (7. Software path) instead of the backdrop (`init_bkg` / `draw_bkg` return at once: **no fog**) |
| `light_init` 0x41b343 / `light_draw` 0x41b757 | no object lights: the brake lights are texture copies (11.) |
| `cloud_init` 0x4201b1 | clouds = 0: no tyre smoke |
| `skid_draw` 0x41e430 / 0x41e6e8 | the skid marks are drawn **without** the blend states (opaque, COPY: the texture alone) |
| `flag_draw` 0x41af8d / finish callback 0x4155fb | the finish flag is only started with g_hw3d |
| `txt_blit_anim` 0x42aec6 / 0x42af0d | the 8-bit anim of a copy, x/y + the texture's accumulator slot |
| `radar_*` (by g_res_index) | LORADAR 64x64 and rect row 0 at 320x200 (HUD.md 4.8); the 8-bit map branch, dots of anim 43 |
| `gfx_draw_wait_screen` 0x41bde0 | `[wait_lo]` / `[wait_hi]` by g_res_index, drawn as the .pcx with its own palette |
| `cops_event_camera` 0x4156d0.. | the speed-camera flash: the whole palette (235, 235, 235) for 2 frames |
| `handle_game` 0x424ca1 | the frame-rate governor stops at 15 visible chunks instead of 25 (2.) |
| `ev_sweep_text_*` 0x415e80 | (the game-over texts use g_screen_w / g_screen_h: 320x200 there) |

The shadows (`shadow_draw_cb`) and sparks have no mode test: with COPY the shadow is the texture's opaque black.

Port (src/screen.c, screen.h): all three modes. RESOLUTION 0 and 1 draw the race at 320x200 / 640x480 into an
offscreen target (a framebuffer object with a 16-bit depth buffer, gfx_gl.c `gfx_target_begin`) that is scaled up into
the window with nearest filtering, into the largest 4:3 rectangle (black bars beside or above it), as a monitor of the
time showed these modes (320x200 with 1.2 times higher pixels). The 3D projection keeps the 81 degrees across the
target (dvClipY = h / w, so 320x200 shows less height than 640x480). Everything of the table above is done except the
accumulator texture and the anim preload lists (no visible effect). In 8-bit mode every 8-bit image (the .pcx
textures, the sky, the RC sprites, the fonts, the radar colours) is shown through the etappe's palette, as the
hardware palette did (index 254 the colour key); the wait screens through their own palette; no gamma (Game.ini gamma
changes 16-bit conversions only). RAMP's handling of the alpha-blend states is not known: the port draws the 8-bit
modes' blended effects with COPY's texture alpha, so the car shadow is an opaque black rectangle (?). RESOLUTION 2 is
3 (the original's 2 differs only in `load_game_palette`'s jump table 0x42098c, which gives it the 8-bit `e0N_pal`
for the 16-bit sprites); 4 (fatal in the original), 5 (a hidden 16-bit software mode at g_res_index 0) and anything else are drawn as 3 too,
and a missing key reads 3 (the original reads 0). Without framebuffer objects the 8-bit modes are drawn at the
window's size.

Device creation, d3dlib.cpp / d3dev.cpp:
- `0x44c7b0` d3dlib_init: `0x44c760` QueryInterface IID_IDirect3D2 from DirectDraw -> `g_d3d2 0x4ea9cc`; `0x44c930`
  creates a **16-bit Z-buffer** (screen size); device; palette; `0x4628a0`/`0x4629f0` texture format
  (TXF_16BPP_RGB if bpp > 8 else TXF_8BPP_RAMP); `0x44d5b0` light array (10); `0x44e990` materials (sets
  LIGHTSTATE_MATERIAL to the default material `0x49b008`); viewport; `0x44cab0` first clear (TARGET|ZBUFFER).
- `0x4601e0` d3ddev_create_auto: bpp > 8: try **HAL** `{84E63DE0-46AA-11CF-816F-0000C020156E}` (type 1), else **RGB**
  `{A4665C60-...}` (type 3); 8 bpp: **RAMP** `{F2086B20-...}` (type 4). `0x460340` creates a given type.
- Device object `g_d3ddev 0x4ea5a0` (C++ class, ctor `0x45f750`), IDirect3DDevice2* at +0x25c (`0x4ea7fc`).
  It caches render states and re-applies them in `0x45fac0` d3ddev_restore_states (after creation / mode switch).

IDirect3DDevice2 vtable offsets used: 0x28 BeginScene, 0x2c EndScene, 0x34 SetCurrentViewport, 0x5c SetRenderState,
0x64 SetLightState, 0x68 SetTransform, 0x74 DrawPrimitive, 0x78 DrawIndexedPrimitive.

Device object layout (offsets from `0x4ea5a0`):

| Off | Global | Meaning | Ctor default |
|---|---|---|---|
| +0x19c | 0x4ea73c | projection matrix (D3DMATRIX) | see 2. |
| +0x1dc | 0x4ea77c | view matrix (set by the camera code, `0x4142b0`) | |
| +0x21c | 0x4ea7bc | world matrix (set per object) | identity |
| +0x25c | 0x4ea7fc | IDirect3DDevice2* | |
| +0x274 | 0x4ea814 | initialised flag | |
| +0x278 | | device type 1 HAL / 3 RGB / 4 RAMP | |
| +0x27c | 0x4ea81c | RS 28 FOGENABLE | 0 |
| +0x280 | 0x4ea820 | RS 34 FOGCOLOR | 0xffffff |
| +0x284 | 0x4ea824 | LS 4 FOGMODE | 3 = LINEAR |
| +0x288 | 0x4ea828 | LS 7 FOGDENSITY | 0.8 |
| +0x28c | 0x4ea82c | LS 5 FOGSTART | 3.25 |
| +0x290 | 0x4ea830 | LS 6 FOGEND | 50.0 |
| +0x294 | 0x4ea834 | RS 1 TEXTUREHANDLE cache | -1 |
| +0x298 | | RS 4 TEXTUREPERSPECTIVE | 1 |
| +0x29c | 0x4ea83c | RS 7 ZENABLE | 1 |
| +0x2a0 | | RS 41 COLORKEYENABLE | 1 |
| +0x2a4 | 0x4ea844 | RS 21 TEXTUREMAPBLEND | 1 (DECAL) |
| +0x2b4 | 0x4ea854 | RS 17/18 TEXTUREMAG/MIN | 0 |
| +0x2c0 | 0x4ea860 | RS 9 SHADEMODE | 2 |
| +0x2c4 | 0x4ea864 | RS 8 FILLMODE | 3 (SOLID) |

## 2. Projection and viewport

- `0x45d860` d3d_projection_matrix(out, near, far, fov) = the DX5 SDK `ProjectionMatrix`: with h = fov/2,
  c = cos h, s = sin h, Q = s / (1 - near/far): `_11 = _22 = c, _33 = Q, _34 = s, _43 = -Q*near, _44 = 0`.
- Game projection, set once in `0x423d20` (SetTransform PROJECTION): **near = 10.0, far = 12500.0,
  fov = pi * 0.45 = 81.0 deg** (`[0x47fc6c] * [0x47f774]`). (Device ctor default: near 10, far 12500, fov 0.2*pi.)
  No other SetTransform(PROJECTION) is done in the game, so the FOV is the same for every camera (?, see cameras).
- `0x45d1f0` viewport_ctor fills a D3DVIEWPORT2: x = y = 0, w x h = screen, dvClipX = -1, dvClipY = h/w,
  dvClipWidth = 2, dvClipHeight = 2*h/w, MinZ 0, MaxZ 1. So the 81 deg is the **horizontal** FOV and the
  vertical one follows the aspect: tan(v/2) = (h/w) tan(40.5 deg) -> 65.3 deg at 640x480.
  OpenGL: `gluPerspective(65.3, 4.0/3.0, 10.0, 12500.0)` (fovy = 2*atan(0.75*tan(0.45*pi/2))).
- World units: the camera near plane is 10 units; angles in game structs are integers with 1024 units per turn
  (`[0x49b014] = 1024.0`, mask `[0x49b018] = 1023`).
- Visible distance is not a far-plane thing but a chunk count: `Game.ini [OPTIONS] visiblechunks` (`0x4d9a8c`,
  clamped 10..50 in `0x421c98`). `0x4252b0` picks the first chunk to draw from the camera chunk: if the camera
  forward . road direction > 0.5736 (cos 55 deg) start = cam_chunk - 2; if < -0.5736 (looking back)
  start = cam_chunk + 2 - visiblechunks; otherwise start = cam_chunk - visiblechunks/2 (wrapping). Road
  (`0x4454f0`) and objects (`0x440990`) are drawn for `visiblechunks` chunks from there.
- Frame-rate governor in `0x424a70` handle_game (0x424c98, after the frame is shown): if g_game_event == 0 (racing),
  1 / timer_frame_dt < 10 and visiblechunks > (g_hw3d ? 25 : 15), visiblechunks is decreased by 1 (never increased
  again). The timer's dt is a weighted average of the last 4 frame times (1/2, 1/4, 1/8, 1/8; timer_update 0x441bd0),
  and timer_frame_dt 0x441cd0 returns 0.1f for 0 or more than 0.1 s, whose reciprocal (9.99999985) is below 10: so
  every racing frame with an averaged frame time of 0.1 s or more costs one chunk. The same global is the AI's window
  (traffic recycling, handling range), so a slow machine also shrinks those. Tweak mode prints `frameRate: %f` /
  `vchunks: %d`.
- Port: `ai_visible_chunks()` (src/ai.c, which reads the ini value) is the drawn chunk count of race_draw (road,
  objects) and `ai_frame_rate_governor` runs once per drawn frame with its real duration. Not reproduced: the
  original's pause dialog is a modal loop, so the first frame after a pause counts the whole pause and drops a chunk;
  the port keeps drawing frames while paused.

## 3. Render states

State numbers are the DX5 D3DRENDERSTATETYPE values (16 LASTPIXEL, 17 TEXTUREMAG, 18 TEXTUREMIN, 47 ZBIAS).
Only these states are ever set (all SetRenderState call sites enumerated). Everything else stays at the D3D
(DX5) defaults: CULLMODE = CCW (cull counter-clockwise -> front faces are clockwise in D3D's left-handed
space), ZFUNC = LESSEQUAL, ZWRITEENABLE = TRUE, DITHERENABLE = FALSE, ALPHATESTENABLE = FALSE,
TEXTUREADDRESS = WRAP, SPECULARENABLE = TRUE(?), SUBPIXEL etc. default.

Global state, set at init:

| State | Value | Where |
|---|---|---|
| 4 TEXTUREPERSPECTIVE | TRUE | `0x45fac0` |
| 7 ZENABLE | TRUE (Z-buffer always on; only turned off locally, see effects) | `0x45fac0` |
| 41 COLORKEYENABLE | TRUE (source colour key on all textures; key colour 0x7f00ff = RGB(255,0,127), PCX palette index 254, `0x464d30`; see FORMAT_IMAGES.md) | `0x45fac0` |
| 21 TEXTUREMAPBLEND | g_hw3d: 2 MODULATE; software: 7 COPY | `0x423d20` / `0x424440` |
| 17/18 TEXTUREMAG/MIN | `Game.ini [OPTIONS] FILTER=2` and g_hw3d: 2 LINEAR, else 1 NEAREST | `0x41b250` |
| 9 SHADEMODE | 2 GOURAUD (g_hw3d) | `0x4243f0` |
| 28/34 + LS 4..7 fog | off by default, see 5. | `0x429e30` |
| LS 1 MATERIAL | default material `0x49b008` | `0x44e990` |
| 8 FILLMODE | per mesh debug switch (POINT / WIREFRAME), restored to SOLID | `0x455700` |

Wrapping plus bilinear filtering: a vertex on a texture's edge takes up to half of the texel row or column on the
opposite edge. The meshes share texture pages, so that row can be another drawing: MPOLIZEI's front centre line
(windscreen, bonnet, bumper) lies at v = 0.0002 of CARS01, whose bottom rows 244..255 are the magenta (255,0,255)
background of other drawings - not the colour key, so it is drawn - and the bilinear sample there is 44% magenta.
The original does the same, but at 640x480 the tinted strip is under a pixel wide (a faint pink dotted line at
most); at today's resolutions (and with mipmaps) it is a clear pink line. Port (model.c `load_mesh`, gfx_gl.c
`range_bind`): a mesh whose UVs stay inside 0..1 (+-0.01) samples its texture clamped to the edges; meshes that tile
their texture (R_ASTRA's v reaches 214, the city buildings' 1..2) and the road keep wrapping.

Per effect (set before, reset after drawing): ALPHABLENDENABLE (27) + SRCBLEND (19) / DESTBLEND (20), ZENABLE,
TEXTUREHANDLE. D3DBLEND: 2 ONE, 5 SRCALPHA, 6 INVSRCALPHA.

| Function | Effect | ALPHABLEND | SRC / DEST | Other |
|---|---|---|---|---|
| `0x41d460` | car shadows | 1 -> 0 (alpha_blending only) | SRCALPHA / INVSRCALPHA | in the object pass, before each car |
| `0x41b720` | object lights (brake lights) | 1 -> 0 | SRCALPHA / INVSRCALPHA | |
| `0x41d360` | radar (HUD, 3D quad) | 1 -> 0 | SRCALPHA / INVSRCALPHA | TL fan |
| `0x41f420` | sparks | - | - | no blend (opaque), LVERTEX TRIANGLELIST |
| `0x41e3a0` | skid marks | 1 -> 0 | **INVSRCALPHA / SRCALPHA** (vertex alpha = transparency) | LVERTEX strips |
| `0x4203d0` | clouds | 1 -> 0 | SRCALPHA / **ONE** (additive) | LVERTEX fans |
| `0x41aee0` | finish flag | 1 -> 0 | SRCALPHA / INVSRCALPHA | ZENABLE 0 then 1, own filter |
| `0x417870` | 3D font (gfx_3dft = "3D font", not 3dfx) | | | ZENABLE 0 then 1 |
| `0x4326e0` | particles (rain) | - (0 from the clouds) | - | ZENABLE 0 then 1, LVERTEX LINELIST, texture 0 |

Textures: table `g_textures [0x4eaa28]`, 0x48 bytes per entry, +0 = D3DTEXTUREHANDLE; the last bound handle is
cached in `0x4ea834` (every draw function compares before calling SetRenderState(TEXTUREHANDLE)).

Vertex types (DrawPrimitive second argument): meshes use DrawIndexedPrimitive TRIANGLELIST with D3DVT_LVERTEX
(mesh type 2, pre-lit vertex colours) or D3DVT_VERTEX (mesh type 1), flags D3DDP_DONOTUPDATEEXTENTS (`0x455700`);
effects use LVERTEX; sky backdrop and HUD use TLVERTEX.

## 4. Lighting

There is effectively **no Direct3D lighting**: light.cpp has constructors for point/spot/directional/parallel-point
lights (`0x44e6f0`, `0x44e740`, `0x44e7b0`, `0x44e800`, via the add functions inside `0x44d930`), but nothing in the
game calls them, and LIGHTSTATE_AMBIENT is never set. All shading comes from pre-lit vertex colours (LVERTEX)
modulated with the texture (TEXTUREMAPBLEND MODULATE), Gouraud interpolated. Damage/brake-light/siren effects are
done by copying texture regions (`Game.ini texturecopy`) and the alpha-blended "object lights", not by lights.
Reimplementation: `color = texture * vertex_color`, no lighting. (? whether any D3DVT_VERTEX meshes exist in the
data - for those D3D would compute material emissive + ambient(0) = black/unlit; check with the mesh format.)

Gamma (`Game.ini [GFX OPTIONS] gamma`, `0x4d9b14`): not a gamma ramp; when set, every colour conversion to 16 bit
(textures, 2D images) does c' = c*c/256 per channel (`0x468220` pformat_set_gamma). It is switched off while the
sky backdrop image is loaded (`0x4234e0`), so the sky is not darkened.

## 5. Frame

`0x424a70` handle_game (one frame):
1. timer, `0x440850` object/AI/physics update, `0x4144b0` handle_game_cameras (input, camera update, view matrix,
   sky heading), `0x425a90`, `0x41dd00` skid mark update.
2. `0x425090` **render_game**:
   1. `Game.ini WAITVBL=1`: IDirectDraw::WaitForVerticalBlank.
   2. Software path only: `0x41e850` blit the scrolling 2D sky into the back buffer (this is the colour clear).
   3. `0x44cb70` viewport Clear with **D3DCLEAR_ZBUFFER only** - the colour buffer is never cleared in the
      accelerated path; the sky backdrop (step 7) covers every pixel the world does not.
   4. BeginScene.
   5. `0x4454f0` road chunks, then `0x440990` all objects (track objects, cars incl. player, each car right after
      its shadow, see 9. "Car shadows"), for `visiblechunks` chunks from `0x4252b0` (see 2.). Opaque, Z-buffered,
      colour-keyed.
   6. `0x41d360` 3D radar (HUD; only if texturecopy, overlay on, overlay_detail > 0).
   7. Alpha/transparent passes, in this order: `0x41b720` object lights, `0x41f420` sparks, **`0x4190d0` sky
      backdrop** (accelerated path), `0x41e3a0` skid marks, `0x4203d0` clouds, `0x431f60` particles,
      `0x41aee0` finish flag, `0x417870` 3D font.
   8. EndScene; tweak mode frame-rate text.
3. Game logic (`0x424ee0` keys, actions, damage...), `0x41bdd0` draw_2d_display (2D HUD on the back buffer, see the
   HUD document), `0x420760`/`0x464ef0` update_game_screen = flip (or blit of the system-memory buffer when
   `Raser.ini FOCUS=1`).

For OpenGL: clear depth (and colour, harmless), draw the opaque world, then the sky quad with depth test
LEQUAL at its far depth (or simply draw the sky first with depth writes off - same result because the sky sits
behind all drawn geometry), then the blended effects with depth writes on as in the original (D3D default) (?),
then the HUD.

## 6. Cameras

### Library camera (cam_main.cpp)
`c_camera` (0x160 bytes, ctor `0x446fa0`, array `g_cameras 0x4e66a0`, `0x446d80` cam_init_cameras(10)):
+0x08 position, +0x14 orientation matrix (row0 right, row1 up, row2 forward), +0x54 target, +0x60 up,
+0x6c dampening on, +0x70 position lock, +0x74 target lock, +0x78 follow (unused), +0x7c use object up
(1 = orientation row1, 0 = world (0,1,0)), +0x80 keep lock distance (default 1), +0x84 dampening factor,
+0x88/+0x94 previous position/target, +0xa0/+0xa4 position-lock object (-1 = absolute) and car-local offset,
+0xb0/+0xb4 target-lock object and offset.

`0x447ab0` c_camera::update (per frame, for the active camera):
```c
if (poslock) pos = (obj == -1) ? off : O[obj].pos + off * O[obj].ori;   // offset in car space: x right, y up, z forward
target = pos + ori.row2;
if (tgtlock) target = (tobj == -1) ? toff : O[tobj].pos + toff * O[tobj].ori;
if (damp) {
    pos = prev + factor * (pos - prev);          // written as /dt*dt, so a per-frame lerp (frame-rate dependent)
    prev = pos; prev_target = target;            // the target is not smoothed
    if (poslock && obj != -1 && keep_lock_distance) {   // 0x447f97: keep the distance behind the car rigid
        l = (pos - O[obj].pos) * transpose(O[obj].ori); l.z = off.z;
        pos = prev = O[obj].pos + l * O[obj].ori;
    }
}
up = use_object_up ? ori.row1 : (0,1,0);
f = norm(target - pos); r = norm(up x f); u = f x r; ori = (r, u, f);
```
So the chase camera lags only sideways/vertically (lateral swing in corners, vertical on bumps); the distance
behind the car (z) is rigid. `0x4475a0` enable_dampening snaps prev to the current position.

View matrix (`0x4144b0` handle_game_cameras, tail): find the camera's road chunk (`0x445a10`, stored in
`g_active_cam_chunk 0x4a70cc` for the chunk culling in 2.), `update()`, `0x45d910` d3du_view_matrix(pos, target,
up, roll 0) (DX5 SDK `ViewMatrix`) -> `0x4ea77c`, SetTransform(VIEW). Then the camera heading drives the
software sky (`0x41eac0`, see 7). The projection (FOV 81 deg, 10..12500) is global, not per camera.

### Player views (`0x4147f0` init_player_camera, `0x414b70` set_player_cam_mode)
Player camera = slot 0: position lock on the player car, target lock (0, 10, 0) on the car, damping factor
**0.245** (`0x3e7ae147`), world up. Offsets in car space (x right, y up, z forward):

| Mode | Key | Name | Position offset | Target | Damping | Up | Player car drawn |
|---|---|---|---|---|---|---|---|
| 1 | `1` | chase (default) | (0, 25, -60) | (0, 10, 0) | 0.245 | world | yes |
| 3 | `2` | cockpit | (0, 15.5, -1) | none: looks along car forward | off | car | no (car +0x2c = 0) |
| 5 | `3` (held) | look back | (0, 23, 40) | (0, 10, 0) | as before | world | yes |
| 0 | `4` | far chase | (0, 25, -100) | (0, 10, 0) | 0.245 | world | yes |
| 2 | - | high chase | (0, 38, -55) | (0, 10, 0) | 0.245 | world | yes |
| 10 | - | higher chase | (0, 45, -55) | (0, 10, 0) | 0.245 | world | yes |
| 4 | `5` tweak | offset | (25, 15, -50) | none | off | world | yes |
| 6 | `7` tweak | | (-30, 15, 60) | (0, 10, 0) | as before | world | yes |
| 7 | `8` tweak | | (25, 15, -50) | (0, 10, 0) | as before | world | yes |
| 8 | `9` tweak | | (-20, 9, -30) | (0, 10, 0) | as before | world | yes |
| 9 | `0` tweak | | (-25, 55, -30) | (0, 10, 0) | as before | world | yes |

Keys: Windows VK '0'..'9' on the main row via GetAsyncKeyState (`0x466910`), only with Shift up and only while
racing (`g_game_event 0x4d9a7c == 0`); not configurable, no cycle key. `3` is momentary (saves the mode in
`0x4863cc`, restores it on release). Tweak keys need `Raser.ini [HELPDESK] TWEAK_MODE=1` (`0x4d9b24`); tweak key
`M` cycles the camera between the AI racers and the player (`0x4149b0`). Modes 2 and 10 are unreachable.

### Event cameras
"Slot" is the game camera slot of `change_active_camera(slot)` `0x4142b0` (`g_cam_slot[slot]` 0x4a7078 -> camera
index; `init_game_cameras` 0x413ed0): 0 player (`init_player_camera`), 1 start, 3 topview, 4 tankstation, 5 finish
(0x4a708c, a plain `cam_add_camera`), 6 event camera (0x4a7090, "any camera", shared by the arrest, the game over and
the path camera), 7 temp, 8 ai cockpit. Selecting slot 5 also sets the camera's use-object-up flag (+0x7c) to 0 (world
up); slot 6 keeps the constructor's 1 (up from its own previous orientation: stays upright). The "camera mode 6" of
some notes is this slot 6. A fresh camera (ctor `0x446fa0`) has no dampening and no locks; the event code never turns
dampening on, so all event cameras are undamped.

| Slot | Camera | Behaviour |
|---|---|---|
| 1 | start (ev_stcam.cpp `0x416ce0`/`0x416e00`) | target lock (0,10,0) on the player car, position offset (-120, 100, 200); after 1000 ms flies linearly in 2.0 s (N = 2/dt frames) to (0, 20, -60); then slot 0, mode 1 |
| 5 | finish (`0x4154d0`) | still: position = road point (chunk +0x24) of chunk `g_road_chunk_order[c + 3]` (c = player chunk at the finish, no wrap) + (0, 40, 0), set once; target lock (0, 20, 0) on the car (car space); world up; until the race ends |
| 6 | cops caught (`0x4151f0`) | road point of chunk c+1, y + 20; target lock (0, 20, 0); previous camera restored after |
| 6 | game over (`0x416060` -> `game_over_camera` `0x415b20`, `0x4162e0`) | `set_player_cam_mode(1)` first, then the slot-6 camera copies slot 0's lock offsets: position lock (0, 25, -60), target lock (0, 10, 0), both on the player car (car space). Each frame `gameover_update` sets the position offset to (cos a * 60, 25, sin a * 60) with a = -1024 + k * ftol(4096 / ftol(8 / dt)) (4096 units): starts straight behind (continuing the chase view), turns via the right side and the front, **one turn in 8 s**, undamped, until the race ends |
| 6 | path camera (`0x432a10`, .pth path type 1) | position along a path spline, looks at the next path point or a target object |
| 3, 4, 7, 8 | topview, tankstation, temp, ai_cockpit | set up but never activated (dead code) |

## 7. Sky

### Accelerated path: the backdrop quad (gfx_bkg.cpp)
- Image: `0x4234e0` picks **one of back1.pcx..back7.pcx at random** (all 128x128 8-bit sky photos) (`rand() % 7`, table `0x48a790`), loaded as a
  texture without gamma and set as the viewport background material (`0x45ce90`; texture index `0x49b028`,
  material `0x49b02c`). `Raser.ini [MAPS]` scrl_lo/scrl_hi are only used by the software path.
- Geometry (`0x418ab0`, at `0x4a7c68`): 8 TLVERTEX = 2 triangle fans of 4 vertices covering the screen
  (rect table `0x488190`: 640x480 -> (0,0)-(639,479)):
  - fan 1 (`0x4a7c68`): top edge y0 to the horizon line y0 + H/3; tv 0 -> 1/3;
    colour top 0xffffff, horizon `horizon_rgb` (initial 0xc83737);
  - fan 2 (`0x4a7ce8`): horizon line to the bottom y1; tv 1/3 -> 1; colour horizon_rgb -> 0xffffff at the bottom.
  - tu: left = u0, right = u0 + 0.15 (the screen shows 15% of the image width, which wraps once per 360 deg).
- Depth (`0x418760` bkg_set_depth(d)): sz = 1 - 1/d (clamped 0..1), rhw = 1/d, with d = 320 at init and
  d = 260 + 10*(visiblechunks - 20) once visiblechunks is known (`0x4190d0`); i.e. the quad sits at the far end of
  the visible track and is Z-tested against the world, filling only empty pixels.
- Per frame (`0x4190d0` draw_bkg, `0x418f00` bkg_update_colors):
  - u0 = 1 - (int(heading) & 1023) / 1024, heading = the float at object +0x78 of the player car's object
    (an angle in 1024 units per turn) -> linear: one texture width per 360 deg (needs GL_REPEAT). It follows the
    car heading, not the camera (look-back shows the same sky (?)). No vertical movement (pitch is ignored).
  - A "time of day" factor runs along the track: dt = |player_chunk (`0x4d9e44`) - start_chunk (`0x4a7c58`)| * 2,
    t = (dt mod 2N) / N with N = number of chunks of the track (`[road 0x4e6670]+8`), t in [0,2).
    a = |sin(t*pi/2)|, b = |sin(t*pi)|, c = |sin(t*pi/4)|.
  - horizon_rgb = base + (255 - base) * b, base = (200 + 25c, 55 + 175c, 55) (constants `0x488150..158`).
  - Specular alpha (= D3D vertex fog factor, only visible when fog is on): top vertices 128 + 127a, other
    vertices 255a.
  - `0x418830` fog: colour = F + (255 - F)*a with F = (220, 125 - 50*|sin(s*pi/960)|, 100) (B = 55 before the fog
    init), s = race clock in seconds; FOGSTART = 435 + 750p, FOGEND = 975 + 7500p with p = 0.08 + 0.8a.
    Only applied when fog is enabled; it also sets the background material colour.
- Draw: TEXTUREHANDLE = backdrop texture, 2 x DrawPrimitive(TRIANGLEFAN, D3DVT_TLVERTEX, 4 vertices); current
  blend states (no alpha blending), MODULATE with the vertex colours.

### Software path (gfx_sky.cpp)
Only when `g_hw3d = 0` and `Game.ini [OPTIONS] BACKGROUND` (`0x4d9a80`) is 0.
- `0x41e740` sky_init_moving_sky loads the `Raser.ini [MAPS]` arg 6/7 image into a DirectDraw surface:
  SCRL_LO.pcx 480x200 (screen 320x200), SCRL_HI.pcx 960x320 (screen 640x480), i.e. 1.5x the screen width.
  (Scroll.pcx 800x200 is not referenced.)
- `0x41e850` sky_blit, per frame before BeginScene: a plain DirectDraw `Blt(DDBLT_WAIT)` into the back/buffer
  screen, destination (0, 0, screenW, H - H/3) = the top 2/3 of the screen (134 rows lores, 320 rows hires = the
  image height). Source y is always 0: **no vertical movement with pitch**. If the source window passes the right
  edge the blit is split in two (`0x41e9d0`, horizontal wrap).
- `0x41eac0` sky_set_scroll(c): `x = ftol(-(c + 1) * (W/2))` (W/2 an integer division), wrapped into [0, W) by one
  add or subtract, W = image width (`0x4cc028`); then `sky_build_rects` 0x41e9d0: one rectangle (x, 0)-(x + screenW,
  H - H/3) when it fits, else (x, 0)-(W, ..) to the left edge and (0, 0)-(screenW - (W - x), ..) after it. The source is
  the image's top H - H/3 rows (SCRL_LO: 134 of 200).
  Callers: handle_game_cameras (`0x4147d3`, every frame, after the objects) with `c = cos_table[ftol((atan2(dz, dx) -
  pi/2) * units / 2pi) & mask]` of the active camera's view (target - position): that is dz / |d|, the view's north
  component; and handle_player_car (`0x4261dd`) with the car's heading, which the camera's call overwrites in the same
  frame. So the offset is W*(1 - c)/2 with c = the north component: looking east and west shows the same part, and a
  turn of 180 deg sweeps the image once (an original quirk).
- Port (race.c `draw_sky_sw`): as above, into the 8-bit modes' target before the scene; the rows below the sky are
  cleared to black (the original leaves the last frame there; the world covers them).

## 8. Fog
- `Game.ini [OPTIONS] fog` (`0x4d9abc`, CD 0, "conflicts with alpha blending"). Only in the accelerated path,
  and only in half of the races: `0x418da0` init_bkg (from init_game) enables it if `fog != 0 && (rand() & 1)`.
  Unless fog == 2 it first switches TEXTUREMAPBLEND to 7 (COPY) and re-applies the FILTER setting (no change).
  COPY is, per the DX5 documentation, "equivalent to DECAL" on a HAL device: the texture's colour and alpha replace
  the vertex colour and alpha. The 16-bit textures have no alpha, so everything that relies on vertex colour or alpha
  changes: the sky loses its horizon gradient, the car shadow becomes an opaque black rectangle, the brake glows
  opaque grey discs, the smoke puffs fully additive, the finish flag an opaque chequer over the whole screen, and the
  skid marks (alpha 255 = invisible with their INVSRCALPHA / SRCALPHA blend) disappear - the conflict. fog = 2 keeps
  MODULATE. The COPY state stays for the whole race, also when trigger 17 switches the fog off.
- `0x429e30` fog_enable: FOGENABLE = 1, fog mode LINEAR (D3DLIGHTSTATE fog = vertex fog done by the D3D lighting
  module, not table fog), colour 0xb4b4c8 (R=180,G=180,B=200), start 3.5, end 1000; init_bkg then sets the base colour
  of `0x418830` (`0x48815c`) to (220, 125, 100). From then on `0x418830` recomputes colour/start/end every frame (called
  by draw_bkg, so for the next frame's world) from the track progress a and the race clock (see 7.):
  colour = ftol((255 - X0) a) + ftol(X0) per channel, X0 = (220, 125 - 50 |sin(s pi/960)|, 100), s = lap timer 0 in
  whole seconds; start = 435 + 750 p, end = 975 + 7500 p, p = 0.08 + 0.8 a: start 495..1095, end 1575..7575 world
  units (heavy at the start of a track, a = 0; light and white half way, a = 1).
- The sky backdrop (TL vertices) carries its own fog factor in the specular alpha (written by bkg_update_colors every
  frame, used only while FOGENABLE): top edge 128 + 127a, horizon and bottom 255a (255 = no fog); so at the start of
  the track the horizon is pure fog colour.
- Track triggers 16 and 17 (handler table `0x48c488`, `0x429ad0` / `0x429b20`, player only) - no track of the CD has
  them (FORMAT_ROAD.md): 16 stores a fade (colour (0, 0, 255), +5/+2/-5 per step, start 3.5, end 2000) that would be
  run by the game action 5 update `0x4157e0`, but that only runs while `0x4dc9a0` is set and nothing sets it; 17 turns
  the fog off for the rest of the race (FOGENABLE 0, colour 0xc8c8ff, start 3.5, end 1000).
- Port (src/fog.c): fog_init (from race_init) reads the setting and tosses the coin (`A2_TEST_FOG=1`: always on, for
  tests); fog_frame (from the sky drawing, every frame) computes the values and sets the device-wide states of gfx.h:
  `gfx_set_fog_on` (linear fog on the eye depth for every 3D draw until the next frame - D3D's lighting-module vertex
  fog for the pre-lit meshes (?)) and `gfx_set_decal` (COPY: the shader takes the texture alone); the 2D overlay is
  never fogged (it is DirectDraw in the original). fog_sky blends the fog colour over the sky with alpha 255 - factor
  per vertex. Triggers 16 / 17 are cases of rules.c's trigger switch.

## 9. Effects
Ini switches: `Game.ini [GFX OPTIONS]` clouds, skidmarks, fade_skmarks, particles, part_rain, object_lights,
alpha_blending (master switch; also forced off by `0x4234e0` when the device lacks source/destination blend caps,
saved in `0x4d9b04`/`0x4d9b08`). All effect textures are regions of **S_SFX.PCX** (128x128). Lifetimes in seconds
are converted to frame counts with the frame time at creation (`0x441cd0`, dt clamped to 0.1 s), so the effects
are frame based. Billboards copy the camera orientation matrix.

### Skid marks (gfx_skid.cpp)
- `0x41e130` init, `0x41dd00` skid_update(player car, slip) every frame (player car only), `0x41e3a0` draw.
- 20 records (`0x4acad0`, stride 0x1910), each two triangle strips (left/right rear wheel) of max 100 LVERTEX.
- A mark starts when slip >= 4 and the previous slip was < 4; while it lasts, 2 vertices are appended per strip
  **every frame** (no distance threshold) until the strip is full.
- Points (`0x41d880`): car-local x = +-7, y = +1.0 (height offset; no ZBIAS), z = -10*h (h per car type,
  1.01..1.46, table `0x484b3e` + 238*type), offset +-2 perpendicular to the car-local velocity (mark width 4),
  transformed by the car matrix.
- Colour 0x404040, alpha = clamp(200 - 16*(slip - 4), 0, 255); uv u 0.453125 / 0.5390625, v 0.15625.
- Lifetime 20 s. `fade_skmarks=1`: each frame alpha += max(1, (255 - alpha)/(life - age)), reaching 255 at the
  end; otherwise the mark simply disappears when its life expires.
- Draw: ALPHABLEND on, SRCBLEND = INVSRCALPHA, DESTBLEND = SRCALPHA (so **alpha 255 = invisible**), world =
  identity, DrawPrimitive(TRIANGLESTRIP, LVERTEX, n) per strip with n > 2; the blend states are skipped in the
  8-bit modes (`0x41e430`), where the strips are drawn opaque (COPY: the texture's colour; port: the same).
  OpenGL: `glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA)`.

### Tyre smoke "clouds" (gfx_wolk.cpp; "wolk" = cloud, but these are smoke puffs, not sky clouds)
- `0x4201a0` init (needs g_hw3d, SRCALPHA and ONE blend caps, alpha_blending). Emitter `0x425a90`: slip >= 4, at
  most every 100 ms, one `cloud_emit` `0x41fff0` per rear wheel (+-9 lateral, 3 up), velocity 0.6 * car velocity.
  cloud_emit (Game.ini clouds; nothing below 12 fps, 1/dt < 12) spawns **2 puffs** (`ebx < 2` loop), each in the
  first free slot, so an emission is **4 puffs**, two per wheel at the same point (spawn randomises their speed).
  70 puffs (`0x4d3a18`). (The port's fx_smoke spawned one per wheel until this was checked; now two, as fx_cloud.)
- Spawn `0x41fd80`: life 4 s, size 7x7, vel.xz = (0.75 + rnd*0.25) * v, vel.y = (rand%35 + 9)/life.
  Update `0x41fae0`: vel *= 0.9, pos += vel, size += 29/life (ends at ~36), centre alpha -= 0x50/life;
  animation frame ping-pongs 0,1,2,1,0 (3 frames stacked vertically in S_SFX, u 0..0.3954).
- Geometry: TRIANGLEFAN of 6 LVERTEX, centre ARGB 0x508484A4, 4 corners 0x00646464 (alpha 0), billboard.
- Draw `0x4203d0`: if fps < 10 all puffs are killed; sorted far to near; ALPHABLEND on, SRCALPHA / **ONE**
  (additive); per puff SetTransform(WORLD) + DrawPrimitive(TRIANGLEFAN, LVERTEX, 6).

### Sparks (gfx_sprk.cpp)
- `0x41f300` init; emitted on player collisions (`0x4281f0` -> `0x41f150`), count = min(speed/10 + 5, 25);
  100 sparks (`0x4cc088`).
- Spawn `0x41ed30`: position jitter +-2.5, life 2.5 s, horizontal velocity (rnd + 0.5) * dir * speed*3 / life,
  bob amplitude clamp((rand%50 + speed - 25)*0.02 + 1.5, 1.5, 2.3). Update `0x41eb60`: pos += vel,
  y += cos_table[(2048/life*age + 300) & 1023] * amp (a bounce, no real gravity); billboard.
- One white triangle (+-0.225) front and back (6 LVERTEX), S_SFX uv (0.807,0.185), (0.807,0), (0.992,0).
- Draw `0x41f420`: **no blending** (opaque, colour key); per spark SetTransform(WORLD) + TRIANGLELIST.

### Car shadows (gfx_shdw.cpp)
Port: src/shadow.c. Every car has one: the player, the CPU racers, traffic, ADAC (all through `add_car` 0x410bc0,
which writes object +0x24) and the parked police cars of the .omp files (`car_cops_add_static` -> `object_set_shadow`
0x4110e0); the draw loops skip objects with +0x24 = -1 (main_object_init 0x43fa40 does not write it, so other
objects keep the array's initial -1 (?)). The path vehicles (helicopter, plane, boat, tram: main_object_add without
add_car) have none, in the port neither.
```c
void init_car_shadow_object(void)              // 0x410df0, Car_main.cpp, from init_game
{
    g_shadow_instance = object3d_new((0,0,0), (0,0,0), S_SHADOW_ANIM, 0, 0);   // 0x4d9a90; inst +0x95 = 2
    mesh = its frame's mesh (s_shadow.d3d: 4 vertices at y = 0, faces (2,0,3) (1,0,2)), else "shadow mesh (%d) is not square";
    for (v = 0..3) v.color = 0x80000000, v.specular = 0;  // black, alpha 128
    e = 2 / g_textures[tex].width;             // s_shadow: 64 x 64 (.tga), a black rounded rectangle in the colour key
    v0.uv = (1-e, e); v1.uv = (e, e); v2.uv = (e, 1-e); v3.uv = (1-e, 1-e);
}
void shadow_draw_cb(int obj)                   // 0x41d460, registered by shadow_register 0x41d810
{
    car = objects[obj] instance; sh = objects[obj].+0x24;   // ("Object (%d) has a shadow mesh (%d) that's not square")
    B = the car mesh's bounding box (mesh +0x10.., built by 0x456110);
    v0.xz = (B.max.x, B.max.z); v1.xz = (B.min.x, B.max.z); v2.xz = (B.min.x, B.min.z); v3.xz = (B.max.x, B.min.z);
    h = chunk_height_at(g_road_chunk_order[obj +0x18], car.pos.x, car.pos.z);
    sh.pos = (car.x + 2, h + 0.5, car.z + 2); sh.rot = (0, car.yaw, 0);   // (unused: +0x95 = 2)
    sh.M = car.M;                              // the car's matrix with its pitch and roll ...
    sh.M.row3 = (car.M.row3.x + 2, h + 0.5, car.M.row3.z + 2);           // ... moved 2 along world x and z
    if (g_opt_alpha_blending) { ALPHABLENDENABLE 1; SRCBLEND SRCALPHA; DESTBLEND INVSRCALPHA; object3d_draw(sh); ALPHABLENDENABLE 0; }
    else object3d_draw(sh);                    // opaque black
}
```
The object draw loops (`0x4409e0`, `0x440b30`) call the callback for every visible object of the drawn chunk window
with +0x24 != -1, right before `obj_draw_with_damage` draws it, so the shadows are part of the opaque pass (Z test
and write on). What it looks like: the car's footprint as a half-transparent black rounded rectangle 0.5 above the
road, shifted 2 units along world +x and +z (a fixed "sun" direction, not turning with the car), so mostly hidden
under the body and visible as a dark edge on two sides and in the gap under the body; it tilts with the body's pitch
and roll. Port: drawn from race_draw just before each car's body with the drawn (interpolated) body matrix; the colour
key is the shader's key-only alpha test (GfxState alpha_test 2), so the vertex alpha does not cut the quad. Not
drawn in the cockpit view (the player car is not drawn), as in the original.

### Object lights / brake lights (gfx_lght.cpp)
Port: src/lights.c.
- `0x41b330` light_init (from init_game; needs object_lights, g_hw3d, SRCALPHA/INVSRCALPHA caps and alpha_blending,
  else "Object lights disabled" and object_lights = 0): 20 records (`0x4aa860`: used, on, object, instance), each with
  its own instance (+0x95 = 2).
- `0x41b5a0` light_create(obj, anim, frame, pos, rot, argb): the first free record (-1 when none or object lights
  off); the instance gets anim/frame, pos (+0x50) and rot (+0x74, pitch/yaw/roll) relative to the object, zero
  velocities, inst_build_matrix; `0x41b400` light_set_vertex_alpha recolours the **mesh** (shared by every light, so the
  last colour wins): alpha = A - ftol(A |v| / max |v|) (A at the origin, 0 at the farthest vertex), RGB = the light's,
  specular 0; at most 1000 vertices.
- `0x425500` create_brake_lights, called by init_player_car only - **the AI, traffic and police cars get no lights**:
  two lights for the player car from "OBJ_LIGHT01_ANIM" (s_licht.d3d: a 6-vertex pentagon fan of radius ~3 in the xy
  plane facing -z, uv in the white disc of s_sfx), positions per car type at `0x485c28` (left) / `0x485c34` (right),
  float[7][2][3]; rotation (pitch, 0, 0) with pitch per type { 0, 0, -500, 0, 0, 0, -180 } (4096 units: the Beetle's
  lights tilt 44 degrees, the Diablo's 16) and ARGB per type 0xFF960000 (0-1), 0xFFFF0000 (2-4), 0xA4FF0000 (5-6), both
  stack constants of the function. Indices in `0x48b458` / `0x48b45c`.
- handle_player_car 0x425f0b, before the driving code (so with the previous frame's value): on when
  g_plr_brake_light == 1 (or g_plr_gas_hold, never set); only a change (`0x4d9e10`) is passed on: with object lights
  light_set_on `0x41b560` for both, otherwise set_brake_light_texture `0x42b100` (the texture copy, section 11).
- `0x41b720` light_draw (render_game, after the objects and the radar, before the sparks): ALPHABLENDENABLE 1,
  SRCALPHA / INVSRCALPHA; per used and lit light: inst_build_matrix(light), M = light.M * object.M (mat_mul), draw;
  ALPHABLENDENABLE 0. Z test and write stay on. It does not check whether the object is drawn.
- Port: lights_init / lights_tick (start of the tick, before the cars' handlers) / lights_attach (the body matrix as
  drawn) / lights_draw (after the cars). Only drawn with this frame's body matrix: in the cockpit view the player car
  is not drawn and its lights (behind the camera) are skipped.

### Particles / rain (prt_main.cpp)
Port: src/particles.c (names in docs/names/spel_render.txt). A general particle module with one emitter, set up as
"rain": a spray of grey 1-pixel streaks around the camera. The CD's Game.ini switches it off (`particles=0`, comment
"Currently the module isn't used"); `part_rain=1` only matters when `particles` is not 0. Both are read by
`gm_ini_load_gfx_options` 0x421fd0 with GetPrivateProfileInt, default 0 (`g_opt_particles` 0x4d9af4, `g_opt_part_rain`
0x4d9af8). Nothing else reads them: the rain does not depend on the etappe, the track, the sky or the fog, and it is
the same in the 3D-card mode and the 8-bit (RAMP) modes (render_game calls prt_frame on both paths).

- `prt_init` 0x431f20 (init_game 0x42423b, before light_init and init_bkg; only with `particles`):
  `prt_alloc_pool` 0x431fb0 calls **srand(time(NULL))** (0x470640 / 0x470110 = the CRT's time / srand: every later
  rand() of the race - the fog coin of init_bkg, the traffic - is then seeded from the clock), mallocs 4000 particles
  of 0x54 bytes (0x52080) and links them into the free list `g_prt_free` 0x4e057c (via +4). Then per emitter of the
  array 0x4e0460..0x4e057c (stride 0x11c: one emitter) `prt_emitter_init` 0x432130 (`prt_emitter_ctor` 0x432000 and
  the list head +0x78 = 0), then `prt_rain_setup` 0x4328b0 on it.
- Emitter (0x11c bytes): +0 0, +4 name "Emitter" (0x49661c), +0x54..+0x5c 0, +0x60/+0x64 yaw base/range, +0x68/+0x6c
  pitch base/range (radians), +0x70/+0x74 speed base/range, +0x78 particle list, +0x7c max particles, +0x80 count,
  +0x84/+0x88 new per frame base/range, +0x8c/+0x90 life base/range (frames), +0x94/+0xa0 start RGB base/range,
  +0xac/+0xb8 end RGB base/range, +0xc4 acceleration (vec3, per frame), +0xd0 active, +0xd4 matrix (16 floats, the
  emitter's world matrix; the identity 0x4ea468 of math3d_init_tables at first), +0x114 draw callback, +0x118 update
  callback. The constructor's values (yaw 0 / 2 pi, pitch pi/2 / 0.698, speed 0.5 / 0.05, max 500, new 15 / 5, life
  80 / 15, white, acceleration (0, -0.007, 0), inactive, draw = prt_draw, no update) are all replaced by the rain's:

  | Field | Rain (`prt_rain_setup` 0x4328b0) |
  |---|---|
  | yaw / pitch | 0 + rnd * 2 pi / 0 + rnd * 0: horizontal, every direction |
  | speed | 2.0 + rnd * 0.05 units per frame |
  | max | 200 |
  | new per frame | 10 + ftol(rnd * 5): 5..15 |
  | life | 50 + ftol(rnd * 15): 35..65 frames |
  | start / end colour | 0.5 + rnd * 0.1 per channel (each drawn on its own) |
  | acceleration | (0, -0.1, 0) |
  | active | g_opt_part_rain |
  | update callback | `prt_rain_follow_cam` 0x432990 |

  rnd = (rand() - 0x3fff) * [0x47f920] (1/16383): -1 .. 1.00006, the MSVC CRT rand() (0..32767).
- `prt_frame` 0x431f60 (render_game, after cloud_draw, before flag_draw; returns at once without `particles`): per
  emitter with active (+0xd0) set: `prt_emitter_update` 0x432520, the update callback, the draw callback. Inactive:
  nothing, not even drawn. It runs once per rendered frame; while the pause dialog is up (handle_pause 0x4248d0 blocks)
  nothing is rendered, so the rain stops with the game. The game has no replay and no split screen.
- `prt_emitter_update` 0x432520: `prt_particle_update` 0x432410 on every particle of the list, then
  n = ftol(rnd * range +0x88) + base +0x84 times `prt_spawn` 0x432150.
- `prt_particle_update` 0x432410: life > 0: old position (+0x14) = position (+0x08), position += velocity (+0x20),
  velocity += the emitter's acceleration, old colour (+0x3c) = colour (+0x30), colour += step (+0x48), life (+0x2c)
  -1. Life 0: unlinked, back to the free list, count -1 (so a particle is drawn in life + 1 frames).
- `prt_spawn` 0x432150: nothing (and no rand()) when the free list is empty or count >= max. Else the head of the free
  list goes to the front of the emitter's list; position = old position = (0, 0, 0); yaw = rnd * yaw range + base,
  pitch = rnd * pitch range + base, velocity = (-sin yaw cos pitch, sin pitch, cos yaw cos pitch) * (rnd * speed
  range + base); start colour = 3 x (rnd * range + base), end colour likewise; life = ftol(rnd * life range) + base;
  colour = start, step = (end - start) / life per channel; count +1. 10 rand() per particle. The old colour is not
  set (a new particle is a zero-length line).
- `prt_rain_follow_cam` 0x432990: the emitter's matrix (`prt_emitter_set_matrix` 0x432590) = the identity with the
  translation = the position of the active camera (g_cameras 0x4e66a0 [g_cam_slot[g_active_cam_slot]] +8) + (0, 20, 0).
  The particles live in the emitter's space, so the spray is not left behind: it moves with the camera every frame
  (translation only, the world's axes).
- `prt_draw` 0x4326e0 (+0x114; 0x3e800 bytes of stack = 8000 LVERTEX): the default material and the texture state get
  **texture 0** (g_textures[0], the cache 0x4ea834 = 0); SetTransform(WORLD, emitter +0xd4); `prt_build_lines`
  0x4325b0 (fatal "No emitter!" / "Incorrect index current vertex!" past 8000) writes two D3DLVERTEX per particle:
  x, y, z of the position with colour 0x00RRGGBB = ftol(c * 255) of the colour, then the old position with the old
  colour; specular 0, **tu / tv not written** (whatever is on the stack). ZENABLE 0 (only when g_zenable 0x4ea83c is
  on), DrawPrimitive(LINELIST, LVERTEX, D3DDP_WAIT) ("DramPrimitive error"), ZENABLE 1. No blending
  (ALPHABLENDENABLE is 0 again after the clouds; alpha 0 is not used), no depth test or write: streaks drawn over
  everything, even below the road and behind walls.
- What it looks like: 200 streaks (the cap is reached after about 20 frames and kept, a few new ones as old ones die)
  start 20 above the camera and fly outwards at 2 units per frame while falling (y = 20 - 0.05 t (t + 1) after t
  frames): about 40 out at the camera's height, 70..130 out and 40..195 below it when they die. Each frame a line of
  one frame's movement, so short and slanted at first, nearly vertical and about 5 long later; grey 102..153.
- Port (src/particles.c): prt_init from race_init (keys missing: the CD's values 0 and 1), prt_tick from race_tick
  after the effects (one game tick = one original frame), prt_draw from race_draw after fx_draw, before the flag,
  around the camera as drawn (interpolated); the particles move per tick. rand() & 0x7fff as the CRT's rand. PORT:
  no srand(time) (the port seeds nothing); a line is a quad facing the camera, as wide as one of the original's
  pixels (1/480 of the 640x480 mode's picture height, at least one window pixel; the 8-bit modes: one pixel of their
  target); untextured, with the vertex colour also under TEXTUREMAPBLEND COPY (fog = 1, the 8-bit modes), where the
  original's colour came from texture 0 at undefined UVs (MODULATE: grey times that texel; COPY: the texel) (?);
  drawn with the device's fog like everything else (the original's LVERTEX have specular alpha 0) (?).

### Finish flag (gfx_flag.cpp)
Port: src/flag.c (names in docs/names/spel_render.txt). A waving chequered flag, a half transparent grid in front of
the camera that covers the whole picture, behind the results panel. Started by `finish_update` 0x4155d0 (GAMEFLOW.md
"Finish"): 1500 ms after the finish, if g_hw_accel and not yet done (`g_finish_flag_started` 0x4a70ec),
`flag_start(1)`, in the same call that draws the results panel. Nothing ever calls flag_start(0) and `flag_init`
(from init_game) is the only other writer of the active flag, so once started it stays until the race program ends
(the continue key -> state 4). The panel is drawn by the game logic after EndScene (2D on the back buffer), so the
flag (a 3D pass in render_game, after the particles, before the 3D font) is always under it; the first frame of
the panel has no flag yet.

State: strips `0x4a7dd0` (10 x 34 D3DLVERTEX, 0x440 bytes each), `g_flag_period` 0x4aa850, `g_flag_frame`
0x4aa854, `g_flag_active` 0x4aa858, texture index 0x4a7d88, world matrix 0x4a7d90.
```c
void flag_init(void)                                      // 0x41aea0, from init_game
{
    flag_build_mesh();
    period = ftol(2.0 / timer_frame_dt());                // frames in 2 s
    frame = 0; active = 0;
    tex = load_texture("S_SFX.PCX");                      // the .tga in the 16-bit mode (128 x 128)
}
void flag_build_mesh(void)                                // 0x41ad40: 11 x 17 vertices as 10 strips
{
    for (s = 0; s < 10; s++) for (c = 0; c < 17; c++)     // strip s: rows s and s+1, A = 2c, B = 2c+1
        for (r = s; r <= s + 1; r++) V[s][2c + r-s] = {
            x = 12.5*c - 100, y = 15*r - 75, z = 100,     // 200 x 150 units, 100 ahead (camera space)
            color = 0xffffffff, specular = 0,
            tu = 0.445313 + c * 0.06249997 * 0.0625,      // 114/256 .. 130/256
            tv = 0.046875 - 0.046875 * r * 0.1 };         // 12/256 (bottom row) .. 0 (top row)
}
void flag_start(int on)                                   // 0x41abc0
{
    period = ftol(2.0 / timer_frame_dt());
    if (frame >= period) frame = 0;
    active = on;
}
void flag_animate(void)                                   // 0x41ac00
{
    step = 0x1000 / period;                               // int; 4096 units per turn (run-time sin table)
    for each strip s, column c, row r in {s, s+1}:
        a = (step * frame + 0x40 * r + 0x80 * c) & 4095;
        w = sin_table[a] * 10;
        g = clamp(256 - ftol((w + 10) * 6.4), 0x80, 0xff); // near (w = -10) white .. far (w = +10) half grey
        V.z = w + 100;                                    // 90 .. 110
        V.color = 0x64000000 | g << 16 | g << 8 | g;      // alpha 100/255
}
void flag_draw(void)                                      // 0x41aee0, from render_game
{
    if (!g_opt_finish_flag || !active) return;           // Game.ini [GFX OPTIONS] finish_flag (CD: 1)
    default material texture = tex; SetRenderState(TEXTUREHANDLE, tex);
    if (!g_8bit) ALPHABLENDENABLE = 1, SRCBLEND = SRCALPHA, DESTBLEND = INVSRCALPHA;
    if (FILTER == 2 && g_hw_accel) TEXTUREMAG = TEXTUREMIN = NEAREST;   // the flag is always point sampled
    ZENABLE = 0;
    cam = g_cameras[g_cam_slot[g_active_cam_slot]];
    world = cam.orientation (+0x14, rows right/up/forward); world.row3 = cam.position (+0x08);
    flag_animate();
    if (++frame >= period) frame = 0;
    SetTransform(WORLD, world);
    for (s = 0; s < 10; s++) DrawPrimitive(TRIANGLESTRIP, D3DVT_LVERTEX, V[s], 34, D3DDP_WAIT);
    ALPHABLENDENABLE = 0 (not 8-bit); filter back to LINEAR (FILTER == 2 and hw); ZENABLE = 1;
}
```
What it looks like: the texels are an 8 x 6 chequer of white and very dark red (0x3c0000) at (57, 0) of the 128 x 128
S_SFX (the keyed pixels next to it are only reached at the flag's edges, which are off screen), point sampled, so 8 x 6
big squares. With the 81 deg horizontal FOV the visible half width at depth z is 0.854 z and the half height 0.640 z
(4:3): even at z = 110 that is 94 x 70 against the flag's 100 x 75, so the flag always covers the whole screen. No depth
test or write, so the world and the cars show through it at 39% opacity, multiplied by a grey 128..255 that is
brightest where the cloth is nearest. The wave: one turn per 2 s (step * period ~ 4096; the small rest makes a jump of
a few units at the wrap), 0x80 per column = half a wavelength over the 16 columns, 0x40 per row = 10/64 turn over the
height; the phase grows with time, so the crests run from right to left (and a little downwards). The original counts
drawn frames, with the period measured from the frame time at flag_start.

Port (src/flag.c): the frame counts game ticks (period = 2 s * RACE_HZ, from rules_tick) and the drawn phase is
interpolated between ticks; the texture is a separate NEAREST copy of S_SFX (keyed, gamma as the other textures);
drawn from rules_draw_3d after the effects (fx_draw) and before the 3D letters, with the interpolated camera; in a window
wider than 4:3 the grid is widened by aspect * 3/4 so it still covers the picture. Not reproduced: the 8-bit path
without blending (this port always draws in true colour), and the one frame without flag (here it starts in the tick
that first shows the panel).

### 3D font (gfx_3dft.cpp): the countdown letters
gfx_3dft.cpp is the **3D font** (not 3dfx): "FONT3D_ANIM" objects per character, letter spacing 12.5
(`0x417140`, `0x417720`), drawn with ZENABLE off (`0x417870`).
Port: src/font3d.c (names in docs/names/spel_font3d.txt). The only user is the race-start countdown
(`countdown_update` 0x416bd0, GAMEFLOW.md 4): every 1000 ms `font3d_create_text(rec.text, a, b, rec.serie)` with
the records at `0x4869d8` `{char text[10]; int sample; int effect_serie;}` = "3"/42/1, "2"/42/1, "1"/42/1,
"GO?"/43/2. `countdown_init` 0x416b00 sets the first step 1000 ms ahead and computes, once, from the player instance's
freshly built matrix M (`inst_build_matrix`, rows = the model's axes): a = M.row3 + M.row2 * 400 + (0, 30, 0) (row 2
is the car's z axis, including its pitch), b = (M.row2.x, 0, M.row2.z). The countdown action ends (race state 0)
in the same frame that creates "GO?"; the letters run on by themselves.

Each letter is a record (`t_3d_effect_object`, 0x44 bytes, T_ARRAY of 30 at `0x4a7c00`) plus a 3D instance of
FONT3D_ANIM (frame = the character). The record:

| Off | Field |
|---|---|
| +0x00 | Vec3 centre: a (fx 4 moves it) |
| +0x0c | Vec3 the letter's start position |
| +0x1c | float angle around the centre (fx 4, 4096 units) |
| +0x24 | int height (fx 0) / distance from the centre (fx 4) |
| +0x28 | int phase (fx 0) |
| +0x2c | int letter number (spaces not counted) |
| +0x30 | int instance |
| +0x34 | int frames of the running effect |
| +0x38 / +0x3c | int effect serie / step in it |
| +0x40 | int frames done (fx 0) |

```c
void font3d_create_text(char *text, Vec3 a, Vec3 b, int serie)        // 0x417140
{
    // 'Illegal value effect serie' unless 0 <= serie < 3; series[serie].count must be 1..9
    srand(time(0));
    Vec3 d = (b.z - 0, 0, 0 - b.x) / |...|;           // right of b: the letters go left to right, seen from behind
    int n = strlen(text);
    Vec3 p = a - d * 12.5 * (n / 2);                  // 12.5 = 0x486b64; int n/2
    if (!(n & 1)) p += d * 12.5 * 0.5;                // centred on a
    if (last_used_record_index + n > 30) return;      // (index of the highest used record, -1 if none)
    for (i = 0, k = 0; i < n; i++, p += d * 12.5) {
        int ch = index of text[i] in the 48 bytes at 0x486b68;  // -1: a space, only the advance
        if (ch < 0) continue;
        r = font3d_array_get_new();                   // 0x418200: first free from the last new index on, wrapping
        rec[r] = { .centre = a, .pos = p, .index = k++, .serie = serie, .step = 0 };
        fx = series[serie].fx[0];
        inst = object3d_new(p, (0,0,0), FONT3D_ANIM, ch, fx_tab[fx].update ? : object3d_cb_none);
        inst.vel = 0; inst.rotvel = 0;
        inst.yaw = (atan2(b.z, b.x) - PI/2) * 4096 / (2 PI);   // the letter's z axis along b (pitch, roll 0)
        inst.record = r; inst.visible = 1; inst_build_matrix(inst);
        if (fx_tab[fx].init) fx_tab[fx].init(inst);
    }
}
```

`object3d_new` puts type 2 and +0x95 = 1 (rebuild the matrix when drawn) in the instance. The effects table at
`0x486ab8` is `{init, update}` x 5 and the series at `0x486ae0` are `{int count; int fx[10];}` x 3:
serie 0 = {0, 1}, serie 1 = {2, 3} ("3", "2", "1"), serie 2 = {4, 3} ("GO?"). `dt` below is `timer_frame_dt` (the
last frame's time, at most 0.1 s), so the effects last a time, not a number of frames; `ftol` truncates.

```c
void font3d_draw(void)                                                   // 0x417870, from render_game
{
    ZENABLE = 0;                                      // always on top of the world (no depth test or write)
    for (each used record r) {
        inst = rec[r].inst;
        inst.update(inst);                            // the effects run here, once per drawn frame
        if (instance still used) object3d_draw(inst); // 0x459460: matrix from +0x50 / +0x74..+0x7c, the mesh
    }
    ZENABLE = 1;
}
void font3d_fx_next(inst)                                                // 0x4179e0
{
    if (++rec.step >= series[rec.serie].count) { used[r] = 0; object3d_free(inst); return; }
    fx = series[rec.serie].fx[rec.step];
    if (fx_tab[fx].init) fx_tab[fx].init(inst);
    inst.update = fx_tab[fx].update;
}
void object3d_move(inst) { inst.pos += inst.vel; inst.rot += inst.rotvel; }   // 0x458fd0 (+0x50 += +0x5c, +0x74 += +0x68)

// fx 0 "hop" (0x417a90 / 0x417b20): the word hops in a wave
init:   rec.phase = rec.index * 75; if (rec.phase > 0x7ff) rec.phase -= 0x7ff;
        rec.height = 80; rec.frames = ftol(3.0 / dt); rec.done = 0;
update: rec.phase += 4096 / rec.frames; if (rec.phase >= 0x7ff) rec.phase = 0;      // half sines: hops
        if (rec.height > 25) rec.height -= max(60 / rec.frames, 1);                 // int divisions
        inst.pos.y = SIN[rec.phase] * rec.height + rec.pos.y;
        if (++rec.done >= rec.frames) font3d_fx_next(inst);
// fx 1 "burst" (0x417c30 / 0x417d50): the letters fly apart
init:   inst.vel = ((inst.pos.x - rec.centre.x) * 0.2, 15.0, (inst.pos.z - rec.centre.z) * 0.2);
        inst.rotvel = (rand() % 200, rand() % 200, rand() % 200); rec.frames = 25;     // frames, not seconds
update: object3d_move(inst); if (--rec.frames == 0) font3d_fx_next(inst);          // (fx 2 uses it too)
// fx 2 "fly" (0x417da0): to the player with one somersault
init:   Q = player instance pos; rec.frames = ftol(0.8 / dt);
        inst.vel = ((inst.pos.x - Q.x) * -0.9 / rec.frames, 0, (inst.pos.z - Q.z) * -0.9 / rec.frames);
        inst.rotvel.x = 4096 / rec.frames;                                         // pitch: one turn (int division)
// fx 3 "hold" (0x417ec0 / 0x417f10): stand still, then the end of both countdown series
init:   rec.frames = ftol(0.2 / dt);
update: if (--rec.frames == 0) font3d_fx_next(inst);
// fx 4 "spin" (0x417f50 / 0x418050): the word turns once around its centre, which slides to the player
init:   rec.frames = ftol(1.0 / dt); inst.rotvel = (0, -4096 / rec.frames, 0);
        dx = rec.pos.x - rec.centre.x; dz = rec.pos.z - rec.centre.z; rec.radius = ftol(sqrt(dx*dx + dz*dz));
        if (dx != 0 || dz != 0) rec.angle = (atan2(dz, dx) - PI/2) * 4096 / (2 PI);
update: rec.angle += ftol(inst.rotvel.y);
        a = ftol(rec.angle) + 0x400;                                               // + a quarter turn
        inst.pos.x = COS[a & 4095] * rec.radius + rec.centre.x;
        inst.pos.z = SIN[a & 4095] * rec.radius + rec.centre.z;
        Q = player instance pos;
        rec.centre.x += (Q.x - rec.centre.x) / rec.frames;                         // reaches Q on the last frame
        rec.centre.z += (Q.z - rec.centre.z) / rec.frames;
        object3d_move(inst);                                                       // the letter's own yaw turns along
        if (--rec.frames == 0) font3d_fx_next(inst);
```

What it looks like (letters 8.9 x 13.6 x 5 units, s_letter.pcx; no scaling, no fading):
- **"3", "2", "1"** (serie 1): the digit appears 400 units ahead of the car, 30 up, its face towards the camera behind
  the car, and in 0.8 s flies 90% of the way to the car (ending 40 ahead) while it somersaults once forwards around
  its x axis (4096/frames per frame: at 30 fps 170 x 24 = 4080, so it stops 16 units = 1.4 degrees short of upright);
  it stands for 0.2 s and disappears: exactly the 1000 ms to the next digit.
- **"GO?"** (serie 2): the three letters turn as one rigid word once around the middle letter in 1 s (orbit angle and
  yaw both -4096/frames per frame, so the word shows its back half way), while the centre slides from 400 ahead to
  the car's own position (y stays at a.y = car + 30); it stands 0.2 s over the car and disappears.
- The charset `0x486b68` ("0-9A-Z", 4 umlauts, `="-*+.!?`) is one off against FONT3D_ANIM (49 frames: frame 36 is a
  second LTR_T, and LTR_VRAG "?" is the 49th), so from the umlauts on every character gets the mesh of the one before:
  '?' shows LTR_UITR **"!"**, which is why the record says "GO?" for a "GO!" on screen.
- Matrix (`inst_build_matrix` 0x459360, D3D row vectors): M = Rz(roll) * Rx(pitch) * Ry(yaw), then the position,
  with Rx [5]=c [6]=-s [9]=s [10]=c, Ry [0]=c [2]=s [8]=-s [10]=c, Rz [0]=c [1]=-s [4]=s [5]=c (`0x45e060`,
  `0x45e0d0`, `0x45e140`; `mat_mul` 0x45e450 is out = b * a). A zero angle is skipped; the others are truncated to
  whole units. Ry turns the model's +z to (-sin, 0, cos), the same as the cars (`car_fwd` in src/rules.c), so the
  letter's yaw of create_text equals the car's yaw: the letters keep a fixed orientation (no billboard).
- `object3d_animate_all` 0x458690 (frame++ of type-2 instances and the +0x91 callback) has no direct caller; the
  letters' callback is only run by font3d_draw, so they do not cycle frames.
- Port: the effects run once per game tick (RACE_HZ) instead of per drawn frame, with dt = 1/RACE_HZ (frames =
  sec * RACE_HZ, +0.001 against 23.999), and are drawn interpolated between the last two ticks.

## 10. Car wheels (car_whl.cpp)

The racer bodies `RACER_1..7_ANIM` (car types 0..6: r_trabbi, r_astra, r_beetle, r_bmw, r_porche, r_clk, r_diablo)
have no wheels: their meshes start at y = 1..3. The traffic, police and ADAC meshes (types 7..15) start at y = 0 with
the wheels modelled in, and get no wheel objects. So only the player and the three CPU racers have wheels, as four
**sub-objects** of the car object: `WIEL_ANIM` (anim 142, `wiel.d3d` + cars01.pcx: a 10-sided prism, x +-1.39,
radius 3.66, both caps textured alike, so the left wheels need no mirroring; all four are created with rotation 0).

Sub-objects (obj_main.cpp): an object holds up to 4 instance indices at `+0x4c[4]`, the count at `+0x5c`.

| Addr | Name | |
|---|---|---|
| 0x4136c0 | `car_add_wheels(type, obj)` | from `init_player_car` 0x42572b and `init_racer_cars` 0x41139b, after add_car |
| 0x486198 | `g_wheel_pos` | float[7][4][3]: per car type 0..6 the centres of FL, FR, RL, RR (x, y, z) |
| 0x441630 | `obj_add_sub_object(obj, anim, frame, pos, rot)` | `object3d_new(pos, rot, anim, frame, object3d_cb_none)`, inst `+0x95 = 2` (no auto matrix), inst `+0x0c = obj`; 'No more sub-objects available' at 4 |
| 0x43fbe0 / 0x43fbb0 | `obj_set_child_instance` / `obj_get_child_instance(n)` | `obj +0x4c[n]` |
| 0x441700 | `obj_free_sub_objects(obj)` | (helicopter release) |
| 0x413900 | `car_update_wheels(obj)` | player: from handle_player_car (end of the normal path, and the autopilot branch) |
| 0x413be0 | `car_update_wheels_ai(obj)` | racers: from HANDLE_RACER_AI 0x40ac90 (always with ai_model 0; at the end with ai_model 1) |
| 0x440270 | `obj_draw_with_damage` | draws the body, then each sub-object |

`g_wheel_pos` (front at +z, left at -x; y = 3.7 for all):

| type | front x, z | rear x, z |
|---|---|---|
| 0 Trabant | +-7.0, 13.3 | +-7.0, -10.0 |
| 1 Astra | +-7.4, 13.8 | +-7.5, -14.5 |
| 2 Beetle | +-7.15, 11.2 | +-7.15, -11.72 |
| 3 BMW | +-7.0, 12.6 | +-6.6, -12.5 |
| 4 Porsche | +-7.75, 13.0 | +-7.5, -12.5 |
| 5 CLK | +-7.83, 13.5 | +-7.83, -13.0 |
| 6 Diablo | +-8.0, 14.0 | +-8.3, -14.8 |

```c
void car_add_wheels(int type, int obj)                                   // 0x4136c0
{
    if (type < 0 || type > 6) fatal("Illegal car type (%d)");           // car_whl.cpp line 50
    for (i = 0; i < 4; i++)                                              // FL, FR, RL, RR
        obj_add_sub_object(obj, get_anim_name_index("WIEL_ANIM"), 0, g_wheel_pos[type][i], (0, 0, 0));
}
// init_player_car: car_add_wheels(g_copland ? 0 : g_plr_car, g_plr_obj);
// [quirk] in cop mode the body is MPOLIZEI_ANIM (0x85 poked into type 6's anim index +0x0e, which has its own
// wheels) and the Trabant's wheel set is added as well.

void car_update_wheels(int o)          // 0x413900; car_update_wheels_ai 0x413be0 differs only in the front yaw
{
    float spin = dt * (c_angle_units * 10 / (3.14159f * 7)) * c->vz;    // vz m/s; wheel diameter 0.7 m
    float lim  = c_angle_units * 0.497213f;                              // < half a turn per frame
    if (spin > lim) spin = lim;                                          // forwards only
    for (n = 0..3) wheel[n]->pitch -= spin;                              // rolls forwards
    // player: c_angle_units/12 * -0.01 * g_plr_steer: steer +-100 -> -+30 degrees (to the right = negative yaw)
    // racers: clamp(c->yawrate(+0x7c, rad/s) * 400, -500, 500)  (raw units: up to 44 degrees)
    wheel[0]->yaw = wheel[1]->yaw = front_yaw;
    float d = chunk_height_at(chunk, inst->x, inst->z) - inst->y;  clamp d to [-1.05, 1.05];
    for (n = 0..3) wheel[n]->y = d + 3.5f;                               // on the road while the body bobs
}

// obj_draw_with_damage 0x440270, after object3d_draw(body):
for (n = 0; n < obj->nsub; n++) {
    inst_build_matrix(wheel[n]);                     // Rz(roll) Rx(pitch) Ry(yaw), then the position (row vectors)
    wheel[n]->M = mat_mul(wheel[n]->M, body->M);     // child * parent: the wheels lean and pitch with the body
    object3d_draw(wheel[n]);
}
```

Notes:
- The player's wheels are only updated once the race runs (handle_player_car returns earlier during the countdown),
  so until GO they stand at the table's y = 3.7 with no turn; afterwards y = 3.5 + d.
- **[quirk]** With the old steering model (stuur_model 1) g_plr_steer is 0..60 with 30 = straight, but the formula is
  the same, so the front wheels point 30 * 341 * 0.01 = 102 units (9 degrees) to the right when going straight.
- At 30 fps the spin limit is reached at ~118 km/h (half a turn per frame); above that the wheels stroboscopically
  stand still or turn backwards.
- Port (src/wheels.c): `wheels_init` reads `g_wheel_pos` from spel.dat; `wheels_tick` (per game tick, after the
  update callbacks) keeps the pitch of the last two ticks, the front yaw and y per object (cars with car_type 0..6);
  the player's wheels turn once `race_started` (also during the autopilot); `wheels_draw` draws
  `body * T(x, y, z) * Ry(-yaw) * Rx(-pitch)` (the original's Rx(p) / Ry(y) are `m4_rot_x(-p)` / `m4_rot_y(-y)` in
  vmath.h) right after the body in race_draw, the spin interpolated between the ticks. The port's copland player
  drives the RACER_7 body, so it uses the type-6 wheel set.

## 11. Texture copies (txt_main.cpp, txt_reml.cpp, txt_vbrd.cpp, tr_vbord.cpp)

Game.ini `[OPTIONS] texturecopy` ("brake lights, sirenes, traffic signs, radar") -> `g_radar_enabled` 0x4d9aa4
(0x421d41). `check_texture_copy` 0x424032 then makes a 16x16 surface and test-blits it into the radar texture
(LORADAR/HIRADAR by resolution, `sub_469a20`); when that fails the flag is cleared. The flag gates the radar
(HUD.md 4.8) and every texture copy.

`txt_blit_anim(index, frame)` 0x42ae90 blits frame `frame` of an RC anim into a 3D texture's DirectDraw surface and
re-uploads it (`texture_reload` 0x45bae0). The copy table 0x48c968 has 16 records of 0x58 bytes
`{s32 anim_8bit, anim_16bit, x, y; char texture[64]; s32 track_type; s32 texture_index}`; `sub_42b060` (init_game)
fills `texture_index` (+0x54) for the records whose track type is the etappe's (Raser.ini [MAPS] arg 0) or 2, the
others stay 0. The blit: src (0,0)-(w-1,h-1) -> dst (x,y)-(x+w-1,y+h-1), exclusive rects (`blt_surf_2_surf`
0x4697c0), so the sprite's last row and column are not copied; no colour key. In the 8-bit software modes x/y get
the texture's slot offset in the accumulator texture (texture +0x34/+0x38). A frame outside the anim or a rect
outside the texture is a fatal error.

| index | anim 8/16 | texture, x,y | track | frames | used by |
|---|---|---|---|---|---|
| 0..6 | 26 / 27 | RACER02 246,13 / RACER01 109,24 / RACER02 35,163 / RACER01 102,164 / RACER02 95,39 / RACER01 242,42 / RACER01 143,132 | both | 14: 2*car lit, 2*car+1 dark | brake lights of player car 0..6 |
| 7 | 24 / 25 | a_TRFFC1 190,220 | highway | 5: dark, cross, 50, 70, 100 | speed-limit matrix boards (A_VIDSP1/2) |
| 8, 9, 10 | 28,30,32 / 29,31,33 | a_TRFFC1 9,122 | highway | 4 (text panels) | direction sign, etappe 1 / 3 / 5 |
| 11, 12, 13 | 34,36,38 / 35,37,39 | a_TRFFC1 9,7 | highway | 4 | the second direction sign, etappe 1 / 3 / 5 |
| 14 | 40 / 41 | CARS01 99,62 | both | 2: dark, lit | the blue half of the police light bar |
| 15 | 42 / 42 | RACER02 197,173 | both | 3 | ADAC beacon (`adac_beacon_update` 0x40c4a0; its flag 0x4a5cd0 is never set) |

The sprites of the 16-bit anims are painted in e02_pl16 like the HUD; the 8-bit ones in the target texture's
palette. The CD's textures carry the blank state: the direction panels are empty blue, the matrix boards dark.

Callers:
- Brake lights (`handle_player_car` 0x425f0b): when `g_plr_brake_light` (or the dead `g_plr_gas_hold`) is 1 and
  the state 0x4d9e10 is off, set it and switch on; when neither is 1 and it is on, switch off. With g_hw_accel and
  Game.ini object_lights the glow meshes of gfx_lght.cpp are switched (`light_set_on`), otherwise
  `set_brake_light_texture(on)` 0x42b100: copy `car` (table on the stack, identity) with frame 2*car (on) or
  2*car + 1.
- Sirens: `cop_siren` 0x40c640 blits copy 14 frame 0 when the last siren goes off (0x40c768);
  `cop_siren_light_update` 0x40c860 (from the FSM loop, cop_new and pth_police_update) blits the current frame every
  200 ms (GetTickCount) and toggles it (0x4a5d3c / 0x4a5d40). One texture, so all police cars blink together.
- Speed limits (triggers 4/5/6 on highway tracks only, 8 on any): `set_speed_sign_texture(zone)` 0x42b190, zone
  0..3 for 50/70/100/120, through the jump table 0x42b200 to frames 2, 3, 4, 1 (zone 4 -> frame 0): limit 120
  shows the cross.
- Direction signs (tr_vbord.cpp, triggers 22..39 of the handler table 0x48c488, 0x42a640..0x42adb0): trigger 22 + f
  blits frame f of copy 8/9/10, trigger 31 + f frame f of copy 11/12/13, by etappe 1/3/5 (other etappes: fatal
  "Trigger is only used for highway tracks!"). The tracks use f = 0..3: etappe 1 chunks 50, 161/162, 277/278,
  645/646; etappe 3 121, 204/205, 268/269, 487/488; etappe 5 210, 599/600, 669/671, 739/740.

Port (src/txcopy.c, hooks in race.c, rules.c `trigger`, ai.c `cop_siren*`): the table is read from spel.dat; each
target texture keeps a copy of the pixels tex_get uploaded (texture.c `tex_pixels`), a copy writes the sprite into
it and replaces the GL texture (`gfx_texture_update` rebuilds the mipmaps). The textures are put back to the CD's
state at every race start (the original loads them anew per race). The copy rectangles are in the CD image's
coordinates (`tex_orig_size`) and are scaled when a texture pack's image has another size. Repeating the frame a
copy already shows is skipped (the copies do not overlap). The brake lights are the object lights (src/lights.c) when
Game.ini object_lights and alpha_blending are on (the CD's settings), else this texture copy, as in the original
(`lights_on`). `A2_TEST_BRAKE=1` keeps them lit (tests). Copy 15, the ADAC beacon, is in the table but never runs:
adac_beacon_update `0x40c4a0` (blink every 150 ms while g_adac_beacon_on `0x4a5cd0`) has no caller, and nothing
writes the flag either, so the port leaves it out.

## 12. Port extras: multisampling and ambient occlusion (graphics.ini)

Not in the original; both off by default (`graphics.ini [graphics] msaa=0 ao=0`, `A2_MSAA` / `A2_AO` for tests), only
in the 640x480x16 mode (the 8-bit modes keep their look). Port: src/gfx_gl.c (the scene target), src/screen.c (the
file, `scr_frame_begin` / `scr_frame_end`), race.c (`gfx_scene_ao` after the cars).
- With one of them on, the 3D picture is drawn into an own target of the window's size instead of the window:
  multisampled colour and depth renderbuffers with MSAA, else a colour texture with a 24-bit depth buffer. The overlay
  is drawn into it too; at the end the target is resolved (MSAA) into its colour texture, which is drawn 1:1 into the
  window, and the touch controls come after that. Needs OpenGL 3 / OpenGL ES 3 (or ARB_framebuffer_object):
  multisampled renderbuffers and glBlitFramebuffer; an OpenGL ES 2.0 context leaves both off.
- Ambient occlusion: after the opaque world, the objects and the cars (with their shadows), before the object lights
  and the blended effects. The target's depth is blitted into a depth texture (one sample per pixel under MSAA); a
  full-screen pass computes ground-truth ambient occlusion (Jimenez et al. 2016, in Intel's XeGTAO form; the shader
  of WoodyRE's gtao.c): the view-space position and normal from the depth, 3 slice directions (rotated by a 4x4 Bayer
  tile) with 6 steps each way, the highest horizon within 1.8 m (18 units) with a falloff, the cosine-weighted
  visible arc. A depth-aware 4x4 box blur of it, to the power 1.5, is multiplied into the picture.
- A lost context (Android) forgets the targets and the shaders; they are made again at the next frame.
