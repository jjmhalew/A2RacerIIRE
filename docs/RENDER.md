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
the old software renderer path (2D sky blit, MODULATEALPHA, nearest filtering). A reimplementation should simply
implement the `g_hw3d = 1` path.

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
| +0x2b4 | 0x4ea854 | RS 16/17 TEXTUREMAG/MIN | 0 |
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
- Frame-rate governor in `0x424a70` handle_game: if fps < 10 and visiblechunks > (g_hw3d ? 25 : 15), visiblechunks
  is decreased by 1 (never increased again). Tweak mode prints `frameRate: %f` / `vchunks: %d`.

## 3. Render states

Only these states are ever set (all SetRenderState call sites enumerated). Everything else stays at the D3D
(DX5) defaults: CULLMODE = CCW (cull counter-clockwise -> front faces are clockwise in D3D's left-handed
space), ZFUNC = LESSEQUAL, ZWRITEENABLE = TRUE, DITHERENABLE = FALSE, ALPHATESTENABLE = FALSE,
SPECULARENABLE = TRUE(?), SUBPIXEL etc. default.

Global state, set at init:

| State | Value | Where |
|---|---|---|
| 4 TEXTUREPERSPECTIVE | TRUE | `0x45fac0` |
| 7 ZENABLE | TRUE (Z-buffer always on; only turned off locally, see effects) | `0x45fac0` |
| 41 COLORKEYENABLE | TRUE (source colour key on all textures; key colour 0x7f00ff -> magenta, `0x464d30`) | `0x45fac0` |
| 21 TEXTUREMAPBLEND | g_hw3d: 2 MODULATE; software: 7 MODULATEALPHA | `0x423d20` / `0x424440` |
| 16/17 TEXTUREMAG/MIN | `Game.ini [OPTIONS] FILTER=2` and g_hw3d: 2 LINEAR, else 1 NEAREST | `0x41b250` |
| 9 SHADEMODE | 2 GOURAUD (g_hw3d) | `0x4243f0` |
| 28/34 + LS 4..7 fog | off by default, see 5. | `0x429e30` |
| LS 1 MATERIAL | default material `0x49b008` | `0x44e990` |
| 8 FILLMODE | per mesh debug switch (POINT / WIREFRAME), restored to SOLID | `0x455700` |

Per effect (set before, reset after drawing): ALPHABLENDENABLE (27) + SRCBLEND (19) / DESTBLEND (20), ZENABLE,
TEXTUREHANDLE. D3DBLEND: 2 ONE, 5 SRCALPHA, 6 INVSRCALPHA.

| Function | Effect | ALPHABLEND | SRC / DEST | Other |
|---|---|---|---|---|
| `0x41b720` | object lights (brake lights) | 1 -> 0 | SRCALPHA / INVSRCALPHA | |
| `0x41d360` | radar (HUD, 3D quad) | 1 -> 0 | SRCALPHA / INVSRCALPHA | TL fan |
| `0x41e3a0` | skid marks | 1 -> 0 | **INVSRCALPHA / SRCALPHA** (vertex alpha = transparency) | LVERTEX strips |
| `0x4203d0` | clouds | 1 -> 0 | SRCALPHA / **ONE** (additive) | LVERTEX fans |
| `0x41aee0` | finish flag | 1 -> 0 | SRCALPHA / INVSRCALPHA | ZENABLE 0 then 1, own filter |
| `0x417870` | 3D font (gfx_3dft = "3D font", not 3dfx) | | | ZENABLE 0 then 1 |
| `0x4325b0` | particles (rain) | | | ZENABLE toggled, LINELIST |

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
   5. `0x4454f0` road chunks, then `0x440990` all objects (track objects, cars incl. player, shadows), for
      `visiblechunks` chunks from `0x4252b0` (see 2.). Opaque, Z-buffered, colour-keyed.
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
| Slot | Camera | Behaviour |
|---|---|---|
| 1 | start (ev_stcam.cpp `0x416ce0`/`0x416e00`) | target lock (0,10,0) on the player car, position offset (-120, 100, 200); after 1000 ms flies linearly in 2.0 s (N = 2/dt frames) to (0, 20, -60); then slot 0, mode 1 |
| 5 | finish (`0x4154d0`) | fixed position = road point of chunk c+3, y + 40; target lock (0, 20, 0) on the car; world up |
| 6 | cops caught (`0x4151f0`) | road point of chunk c+1, y + 20; target lock (0, 20, 0); previous camera restored after |
| 6 | game over (`0x416060`/`0x4162e0`) | orbit at radius 60 (from mode 1), height 25, about one turn per 2 s, undamped |
| 6 | path camera (`0x432a10`, .pth path type 1) | position along a path spline, looks at the next path point or a target object |
| 3, 4, 7, 8 | topview, tankstation, temp, ai_cockpit | set up but never activated (dead code) |

## 7. Sky

### Accelerated path: the backdrop quad (gfx_bkg.cpp)
- Image: `0x4234e0` picks **one of back1.pcx..back7.pcx at random** (`rand() % 7`, table `0x48a790`), loaded as a
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
    (an angle in 1024 units per turn) -> the sky scrolls with the car heading (not the camera, so look-back shows
    the same sky (?)). No vertical movement (pitch is ignored).
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
Used when `g_hw3d = 0` (8-bit modes). `0x41e740` init_moving_sky loads the `Raser.ini [MAPS]` arg 6/7 image
(`scrl_lo` at 320x200, `scrl_hi` at 640x480); `0x41e850` blits it into the back buffer each frame before the 3D
scene (this replaces the colour clear). `0x41eac0` gfx_sky_set_heading(v), called by the camera code with
v = cos_tab[(heading - pi/2) * 1024/(2 pi) & 1023] (i.e. about sin(heading)): scroll offset
`[0x4cc01c] = -(v + 1) * w/2`, wrapped into [0, w) with w = `[0x4cc028]` (image width (?)), then `0x41e9d0`
recomputes the blit. So the 2D sky scrolls horizontally with the sine of the camera heading (not linearly) and
does not move vertically. (? exact blit rectangle and image width; the gfx_sky details were not fully traced)

## 8. Fog
- `Game.ini [OPTIONS] fog` (`0x4d9abc`, default 0, "conflicts with alpha blending"). Only in the accelerated path,
  and only in half of the races: `0x418da0` init_bkg enables it if `fog != 0 && (rand() & 1)`.
  If fog == 1 (not 2) it also switches TEXTUREMAPBLEND to MODULATEALPHA and re-applies the FILTER setting.
- `0x429e30` fog_enable: FOGENABLE = 1, fog mode LINEAR (D3DLIGHTSTATE fog = vertex fog done by the D3D lighting
  module, not table fog), colour 0xb4b4c8 (R=180,G=180,B=200), start 3.5, end 1000; the base colour of `0x418830`
  becomes (220, 125, 100). After that `0x418830` recomputes colour/start/end every frame from the track progress
  (see 7.): start 495..1095, end 1575..7575 world units.
- Fog colour fade trigger (track trigger class with vtable `0x48c4c0`, `0x429ad0` start / `0x429b20` end, update
  event `0x4157e0`): fades the fog colour from (0,0,255) by (+5, +2, -5) per step, start 3.5 end 2000; the end
  function turns fog off and sets colour 0xc8c8ff, start 3.5, end 1000. (? where these triggers are placed)
- OpenGL: linear fog on eye-space depth with the values above; D3D vertex fog on TL vertices uses the specular
  alpha as fog factor (sky quad).

## 9. Effects (alpha-blended passes)
Only the D3D side was traced here (call sites of SetRenderState/DrawPrimitive); counts, sizes and lifetimes of the
individual effects are not in this document yet (?). Ini switches: `Game.ini [GFX OPTIONS]` clouds, skidmarks,
fade_skmarks, particles (module off by default), part_rain, object_lights, alpha_blending (master switch, also
forced off by `0x4234e0` when the device reports no source/destination blend caps).

| Module | Init | Update | Draw | Primitive | Blend |
|---|---|---|---|---|---|
| object lights (gfx_lght.cpp, brake lights) | `0x41b330` | | `0x41b720` | mesh-based light objects | SRCALPHA / INVSRCALPHA |
| sparks (gfx_sprk.cpp) | `0x41f300` | | `0x41f420` | TRIANGLELIST, LVERTEX | (no blend state set; colour-keyed texture) (?) |
| skid marks (gfx_skid.cpp) | `0x41e130` | `0x41dd00` (per frame, before render) | `0x41e3a0` | TRIANGLESTRIP, LVERTEX | INVSRCALPHA / SRCALPHA (vertex alpha 255 = invisible), fade per `fade_skmarks` |
| clouds (gfx_wolk.cpp) | `0x4201a0` | | `0x4203d0` | TRIANGLEFAN, LVERTEX | SRCALPHA / ONE (additive) |
| particles / rain (prt_main.cpp) | `0x431f20` | | `0x431f60` -> `0x4325b0` | LINELIST, LVERTEX | Z test toggled |
| finish flag (gfx_flag.cpp) | `0x41aea0` | | `0x41aee0` | TRIANGLESTRIP, LVERTEX | SRCALPHA / INVSRCALPHA, ZENABLE off, own world matrix |
| 3D font (gfx_3dft.cpp) | `0x417720` | | `0x417870` | | ZENABLE off while drawing |

Each effect sets its world matrix with SetTransform(WORLD) (`0x4ea7bc`), binds its texture through the
TEXTUREHANDLE cache and resets ALPHABLENDENABLE to 0 afterwards.
