# Track objects (.omp), paths (.pth), chunk file (.pmp) and CARS.DAT

Everything that stands on or moves along a track besides the road itself, plus the car table. Code: `rd_main.cpp`
(`.omp` loader 0x4451d0), `obj_main.cpp` (main objects 0x43f570-0x441780), `gm_init.cpp`/`gm_*` (object types
0x428bc0-0x4297d0), `pth_main.cpp` / `pth_heli.cpp` (0x432a10-0x435150), `plr_car.cpp` (CARS.DAT 0x4256a0).
Tool: `tools/objparse.py` (self-test over all 7 tracks, dumps, whole-track OBJ). Meshes/textures:
[FORMAT_D3D.md](FORMAT_D3D.md). Road and the `.pmp` chunk records in full: [FORMAT_ROAD.md](FORMAT_ROAD.md).
Names: [names/spel_objects.txt](names/spel_objects.txt).

Status: `.omp`, `.pth`, `.pmp` and CARS.DAT are validated byte-exact to EOF on all files (`python tools/objparse.py
test`). Claims are from the code at the given addresses unless marked **(?)**.

## 1. From a name to pixels

```
.omp record --anim name--> get_anim_name_index 0x42b310 (_stricmp over g_anim_names 0x490430, 312 names)
   anim index --> g_anim_table 0x492050 [9-byte {nframes, int *frames, flag}]
   frame index --> g_frame_table 0x48e5e8 [16-byte {mesh handle, texture handle, mesh index, texture index}]
   mesh index  --> g_mesh_table 0x48d0e0    (357 names, e.g. "mpolizei.d3d")
   texture idx --> g_texture_table 0x48e1a0 (91 names, "cars01.pcx" -> cars01.tga in 16-bit mode)
```

All names in the `.omp` files are of the form `<MESH>_ANIM` in mixed case (`A_viadct_ANIM`, `MPOLIZEI_ANIM`); the
lookup is case-insensitive. Which anims are loaded for a track is fixed in the exe (load lists, FORMAT_D3D.md 3);
every name used by the 7 `.omp` files resolves and is in its track's lists. An object shows one frame of its anim
(`object3d +8`); "animated" objects are those whose handler changes the frame (traffic lights, arrows) or that move
(paths, physics). `python tools/d3dparse.py anims` lists all 312 anims with their frames.

## 2. `.omp` - static objects (`road_load_omp` 0x4451d0, called last by `road_load_track_files` 0x444bf0)

```
u32 count
count x 68 bytes:
  +0   i32  chunk        road chunk the object belongs to (-> main_object+0x18); 0 <= chunk < nchunks
  +4   i32  type         object type 0..10 (handlers below); 21 and 43 also occur (no handler -> static)
  +8   f32  x, y, z      world position (same space as the road, 1 unit ~ 10 cm, y up)
  +20  f32  rx, ry, rz   rotation in degrees; in the data only ry (yaw) is used (rx = -1 on 2 objects)
  +32  char anim[32]     NUL-terminated anim name; bytes after the NUL are stale editor buffer content
  +64  i32  variant      0, 1 or 2 (-> main_object+0x14)
```

Every field is read with its own `fread(p, 4, 1)` (0x470b70), the name with `fread(buf, 1, 0x20)` (0x44533f).
Then (0x445385..0x4453b9) the rotation is converted to the engine's 1024-steps-per-turn angles:
`rx' = rx * 1024/360`, `ry' = -ry * 1024/360`, `rz' = rz * 1024/360` (`[0x49b014] = 1024.0`, `* 0.00277778`).

Object creation: `main_object_add(pos, rot', anim, frame 0, g_static_handlers[type].obj, g_static_handlers[type].coll,
0, variant)` 0x43f770 -> `main_object_init` 0x43fa40 -> `object3d_new(pos, rot, anim, frame)` 0x458340; the chunk is
stored at `main_object+0x18` (0x44542a). Then, by type (jump table 0x4454d8, types 5..10):

| type | handler (`init_track_object_types` 0x429310, table 0x48c2a8) | extra at load | objects (all 7 tracks) |
|---|---|---|---|
| 0 | none | - | scenery: buildings, trees, lamp posts, signs, bridges (4584) |
| 1 | 0x429380: `object_hit` 0x428bc0(obj, 1) | - | a few solid objects (`E00_OUDK`, `C_KEET`, `E02_GROT`) |
| 2 | 0x4293f0: by `+0x10`: 0 -> `object_flyup_check_collision` 0x428ea0 (state 0x10), 1 -> 0x429020 (state 0x11), then 0x428bc0(obj, 0) | - | pylons `A_PILON1`/`C_PILON1`, `A_CNSTR1` |
| 3 | 0x4297d0 (same pattern) | - | road-works barriers/fences `*_CNSTR0/2`, `C_HEK1/2` |
| 4 | 0x429480: frame = traffic-light state `g_traffic_light_state` 0x48c484 (timers 5000/500 ms via 0x421680) | - | `C_STOPLG`, `C_STPLR2` |
| 5 | 0x429380 (as 1) | physics: mass 0 (immovable), restitution 0.1, 4.0, 1.0 | columns `E0x_COL*`, `E0x_MID*`, `N_WALS`, pumps, `A_TANK2`, parked police |
| 6 | 0x4295d0 | - | not used |
| 7 | 0x428d30: speed camera (`flitspaal`, sets 0x4882c0 = 0x33) | - | `A_FLTSRP`, `C_FLTSRP` |
| 8 | 0x4296b0 | physics: mass 13000, restitution 0.2, 4.0, 1.0 | blinking arrows `A_PIJL1`, `A_LPIJL1`, `C_PIJL1` |
| 9 | none | `car_cops_add_static` 0x40c920 (AI police car on the waypoint graph) | `MPOLIZEI` |
| 10 | 0x4293a0 (damps velocity +0x6c..+0x74 and +0x7c by 0.9) | physics: mass 1300, restitution 0.2, 4.0, 1.0 | `MPOLIZEI` |

Physics init = `obj_init_physics_idx` 0x43f8a0 -> `obj_init_physics` 0x4403b0 (mass < epsilon -> inverse mass/inertia 0; restitution
must be in [0,1], `'Invalid coef. of restitution'`), followed by `physics_list_add` 0x4426c0. Type and handler
semantics beyond this are the gameplay/physics agents' area. The handler table `g_static_handlers` 0x4e5dd0
(257 x {object, collision}) is cleared by `rceng_init_engine` 0x441de0 and filled for types 0..10, so types 21/43
behave as type 0. Variant: 1 on 47/47/44 road-works objects of the highway tracks and a few others, 2 on 2 objects;
it only initialises `main_object+0x14`, which the handlers later overwrite with state codes - its effect is not traced
**(?)**.

**Placement transform** (verified against the road direction of the `.pmp` chunk line: on etappe 0 the 17 type-9
police cars are parallel to the road with a mean |sin(angle difference)| of 0.004 for this sign, 0.82 for the
other): a mesh vertex `v` is placed at

```
world.x = v.x * cos(ry) + v.z * sin(ry) + x      (ry = file value in degrees; mesh front +z ends up along
world.y = v.y + y                                  (sin ry, 0, cos ry))
world.z = -v.x * sin(ry) + v.z * cos(ry) + z
```

(rx/rz order unknown; only 2 objects have rx = -1 degree.)

**Data quirks.** `etappe2.omp` and `etappe6.omp` have a header count one larger than the records in the file (981 for
980, 1556 for 1555). The loader does not check fread results, so for the last iteration every read fails and the
locals keep the previous record: the last object is created twice, and because the rotation is converted in place
the duplicate gets `rx,rz * 2.844`, `ry * -2.844` again (a second, differently rotated copy) **(?: harmless)**. The
name field of 26 (etappe 2) / 29 (etappe 6) records contains leftover characters after the NUL (`"...ANIM\0M"`).

## 3. `.pth` - scripted paths (`pth_load` 0x433710, from `init_3d_paths` 0x433a00)

File `etappe%d.pth` (wsprintf 0x433721, opened `"r+b"`; missing file -> 0 paths).

```
i32 version          must be -1 ('Incorrect version path file', 0x43376e)
i32 npaths           -> g_npaths 0x4e47c0 (max 20: g_paths 0x4e0710, 0x33c bytes each)
npaths x {
  i32 start_chunk    path+0x00  the path starts when the player enters this chunk (0 = never)
  i32 end_chunk      path+0x04  the path object is removed when the player reaches this chunk (0 = runs to the end)
  i32 type           path+0x08  0..7, see below ('Unknown path type', 0x4341c6)
  i32 lap            path+0x0c  0 = every lap, n = only in lap n (compared with g_lap 0x4d9ad4 + 1)
  i32 time_ms        path+0x10  time to travel the whole path ('Time not set for path', 0x4342c2)
  i32 npoints        path+0x14  must be < 50 ('Not enough memory allocated for path points', 0x433915)
  npoints x 16 bytes path+0x18: { u32 unused (always 0); f32 x, y, z }
}                    (path+0x338: started-in-this-lap counter, reset when the lap changes)
```

All values are read with `fread(p, 4, 1)`, the points with one `fread(path+0x18, 16, npoints)` (0x4339cd).

Run time: `pth_check_triggers` 0x434460 (every frame): for each path with `start_chunk == g_player_chunk`
(0x4d9e44), matching lap and counter 0, call the type's init function; init functions create the object
(`main_object_add`) and take one of 10 active-path slots (`pth_activate` 0x434220, `g_active_paths` 0x4e0580, 0x28
bytes each; frames to run = `time_ms * 0.001 / frame time`). `pth_update_all` 0x434380 calls the update functions;
`pth_get_position` 0x433b10 evaluates the path: segment `i = t / (time / (n-1))`, per axis a cubic Hermite
(0x434030) between `p[i]` and `p[i+1]` with tangents `(p[i+1] - p[i-1]) * k` and `(p[i+2] - p[i]) * k` (k is an
argument; indices clamped, or wrapped for looping callers).

Path types (table `g_path_type_funcs` 0x4966f0, 8 x {init, update, release}):

| type | init / update / release | object | used in |
|---|---|---|---|
| 0 | - / 0x434140 / - | nothing | - |
| 1 | 0x432a10 / 0x432a80 / 0x432a70 | **(?)** | - |
| 2 | 0x432bd0 / 0x432db0 / 0x432d80 | police car `MPOLIZEI_ANIM` (with physics, cop AI 0x40c640) | - |
| 3 | 0x434800 / 0x4348f0 / 0x4348d0 | canal tour boat `E00_ROND_ANIM` | etappe 0, 6 |
| 4 | 0x434a70 / 0x434b60 / 0x434640 | tram `C_TREIN_ANIM` | etappe 2, 4 |
| 5 | 0x433170 / 0x4333b0 / 0x433380 | helicopter `C_HELI_ANIM` + `C_LBLADE_ANIM`/`C_SBLADE_ANIM` rotor objects (city tracks only, pth_heli.cpp) | etappe 2 |
| 6 | 0x434cf0 / 0x434df0 / 0x434640 | traffic car, random (`rand() % 5`) from `N_BUS`, `N_VRTAUT`, `N_BESTEL`, `N_GOLF`, `N_ASTRA` (0x4969f8) | etappe 1, 2, 3 |
| 7 | 0x434550 / 0x434670 / 0x434640 | airplane `A_VLIEGT_ANIM` (y ~ 400 = 40 m) | etappe 1 |

The init functions set the object's looping sample (object +0x14) and the update functions play it every frame with
`sndfx_play_object_sample` 0x428bc0 (positional, docs/SOUND.md): tram 0x2d `trein`, helicopter 0x2e `heli`, traffic
car 0x20, airplane 0x2c `vliegt`; the boat has none. Their object and collision handlers are NULL and they get no
physics, so path objects do not collide.

Contents: etappe 0 and 6 (identical files) 4 boats; 1: 1 plane + 6 cars; 2: 2 helicopters, 3 trams, 2 cars;
3: 1 car; 4: 2 trams; 5: none (`python tools/objparse.py pth N`).

## 4. `.pmp` - chunk records (`chunk_load_pmp` 0x442b00; documented by the road agent)

`u32 nchunks` (= `.rmp` header word 2, checked at 0x444dc4), then per chunk `f32 x, y, z` (chunk position on the
road centre line), `i32 trigger_a, trigger_b`, and three counted i32 lists (`n <= 12` each): `.cmp` rail indices,
left and right `.dmp` waypoint indices. See FORMAT_ROAD.md 3.2. Consecutive chunk positions are exactly 128 apart on
all tracks (asserted by `objparse.py test`); `.omp` chunk numbers and `.pth` start/end chunks index this list.

## 5. CARS.DAT - car table (`init_player_car` 0x4256a0, plr_car.cpp)

```
fread(g_cars_dat  0x4a5ee0, 0x7c, 10)   10 cars x 31 f32
fread(g_cars_body 0x4a5df0, 0x18, 10)   10 cars x  6 f32          = 1480 bytes, nothing else
```

`'(init_player_car) ... Could not open file CARS.DAT'` if missing. The race also contains a debug **"Tweak Menu"**
(Dutch labels at 0x485d34..0x486178, editor code 0x4119xx..0x413600) that edits these values for the current car
`g_plr_car` 0x4d9e2c and writes the file back (`fopen("CARS.DAT", "wb")`, two fwrites, 0x4135b3). Field names below are its
labels and the offsets it edits (car index `c`: float index `c*31 + i`):

| float | tweak-menu label | car 0 | meaning (physics agent) |
|---|---|---|---|
| 0..5 | Acceleratie constanten, gear 1..6, gearbox 0 | 0.25 0.25 0.2 0.15 0.15 0 | |
| 6..11 | same, gearbox 1 | 0.35 0.3 0.25 0.25 0.25 0.2 | |
| 12..17 | Top snelheden/versnelling, gearbox 0 | 5.56 11.1 19.4 27.8 41.9 0 | m/s (20, 40, 70, 100, 151 km/h) |
| 18..23 | same, gearbox 1 | 5.56 11.1 19.4 27.8 41.7 45.9 | 6th gear only with the upgrade |
| 24 | Punt vooruitschakelen (upshift point) | 0.7 | |
| 25 | Punt terugschakelen (downshift point) | 0.4 | |
| 26 | - (never edited) | 0 | 0 for all cars |
| 27 | Remkracht (brake force) | -0.3 | |
| 28 | Afremmen op motor (engine braking) | 3 | |
| 29 | Rolhoek versterking (roll amplification) | 1.6 | |
| 30 | Draaihoek (steering angle) | 9 | |

The gearbox index is `g_upg_gearbox` 0x4d9ed0 = Game.ini `[PLAYER] versnellingsbak` (0x421df7): 0 = standard
5-speed (6th entry 0), 1 = 6-speed upgrade. Second table, 6 floats per car: roll damping, roll factor, pitch
damping, pitch factor, height damping, height factor (`Rolhoek/Pitchhoek/Hoogte demping/factor`; car 0: 3, 20, 3,
20, 3, 20). Cars 0..6 are filled, 7..9 are all zero. `python tools/objparse.py cars` prints all of it.

## 6. Run-time structures (for reference)

- `g_objects` (`[0x4e48e0]`, T_ARRAY "main_objects" 0x43f570, 0xa0 bytes): +0 physics on, +8 object3d index,
  +0x10 sub-state (0 from `.omp`), +0x14 state (initialised with the `.omp` variant), +0x18 chunk, +0x1c previous
  chunk, +0x44 object handler, +0x48 collision handler, +0x60.. physics (inverse mass/inertia, restitution 0x68,
  velocity 0x6c).
- `g_instances` (`[0x4ea9e0]`, 0x88 bytes, 2500 created by `init_3d_library`): +0 kind (2 = anim/frame based, 1 =
  direct mesh at +0x8d), +4 anim, +8 frame, +0x50 position, +0x74/+0x78/+0x7c rotation (1024ths), +0x84 visible.
- `g_paths` 0x4e0710 (20 x 0x33c), `g_npaths` 0x4e47c0, `g_active_paths` 0x4e0580 (10 x 0x28).

## 7. Open questions

- Meaning of the `.omp` variant (1/2) and of path type 1; whether type-2 objects ever get `+0x10 = 1`.
- Rotation order for rx/rz (only two objects use them, by 1 degree).
- The duplicated last object of etappe 2/6: verify in the running game that it appears (rotated) twice.
