# Road / track geometry: etappeN.rmp, .pmp, .cmp, .dmp

The road module of the 3D library `win3ddp` (`rd_main.cpp`, `rd_chunk.cpp`, `rd_drmap.cpp`, `rd_rail.cpp`,
`0x441d00-0x4466a0`, `0x44bab0-0x44c750` in spel.dat). Parser + self-test + OBJ export: `tools/roadparse.py`. Function and
global names: [names/spel_road.txt](names/spel_road.txt). All addresses below are spel.dat virtual addresses.

Status: all four formats are validated byte-exact to EOF on all 7 tracks (`python tools/roadparse.py test`). Items
marked **(?)** are not proven from the code.

## 1. Concepts

A track ("etappe") is a sequence of **chunks**: slices of road about 128 units long (the distance between consecutive
chunk positions is 128.0-128.4 on every track). Chunk *i* runs from its position `pos[i]` to `pos[i+1]`. Each chunk owns:

* up to 16 **road** quads, 24 **land** quads (verge/grass), 12 **extra** quads (second grass texture) and 12 **rail**
  quads (guard rails), each a textured quad of 4 vertices and 2 triangles (`.rmp`);
* its position, two **trigger** ids, the indices of up to 12 **guard-rail collision segments** (`.cmp`) and up to 12
  **waypoints** on each side of the road (`.dmp`) (`.pmp`).

City tracks 0, 2, 4, 6 are closed loops (last chunk is about 180 units from chunk 0); highway tracks 1, 3, 5 are open
(the chunk sequence ends far from chunk 0). Coordinates are Direct3D left-handed, y up; the road lies roughly at y = 0
(range -116..+752).

| track | chunks | road | land | extra | rail quads | .cmp rails | .dmp waypoints | max lanes left/right | loop |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 778 | 4757 | 6958 | 1738 | 3206 | 2664 | 3310 | 3/5 | closed |
| 1 | 818 | 5480 | 10550 | 569 | 7050 | 3315 | 5347 | 4/6 | open |
| 2 | 726 | 4610 | 7021 | 1370 | 1707 | 1986 | 3308 | 4/4 | closed |
| 3 | 774 | 4596 | 10938 | 204 | 6248 | 3124 | 4611 | 3/5 | open |
| 4 | 840 | 5639 | 7512 | 2022 | 2234 | 2864 | 3807 | 3/5 | closed |
| 5 | 810 | 5861 | 12236 | 472 | 6553 | 3364 | 4911 | 3/6 | open |
| 6 | 778 | 4757 | 6958 | 1738 | 3206 | 2664 | 3167 | 3/5 | closed |

Tracks 0 and 6 have the same counts but different file contents (except `.pth`).

## 2. Loading order

`rceng_load_etappe(name)` 0x441ea0 (asserts `g_rceng_initialised` 0x4e65d4) -> `road_load_track_files` 0x444bf0 with
`this = g_road` (0x4e5d28):

1. `name.cmp` -> 0x444ce0 -> `rails_load_cmp` 0x44c2f0 (rd_rail.cpp)
2. `name.dmp` -> 0x444cd0 -> `drmap_load` 0x443c40 (rd_drmap.cpp)
3. `name.pmp` + `name.rmp` -> `road_load_rmp_pmp` 0x444cf0 (rd_main.cpp, rd_chunk.cpp)
4. `name.omp` -> 0x4451d0 (objects; not covered here)

Between the steps `0x41c410` is called (progress bar). The road textures must be set before step 3
(`'Textures not set. Cannot load road.'` 0x444d0b, checks `g_road+0x4a/+0x4e/+0x52 != -1`). All files are read with
`fread(ptr, 4, 1, f)` one 32-bit value at a time (0x470b70), little endian.

## 3. File formats

### 3.1 `.rmp` - road mesh (read by 0x444cf0 / `chunk_load_rmp` 0x4431b0 / `plane_load` 0x44bdb0)

```
u32  total_alt      ; NOT used by the loader (read into a local at 0x444d8c and never read again).
                    ; Always equals the number of planes whose triangle list is not (0,1,2,0,2,3), i.e.
                    ; all planes minus the rail planes with that winding (verified on all 7 tracks).
u32  nchunks        ; must equal the .pmp chunk count (else 'Data-chunk count does not match draw-chunk count', 0x444dc4)
nchunks x CHUNK
CHUNK:
  u32 nroad  (<= 16, 'Too many road-planes')   ; nroad  x PLANE
  u32 nland  (<= 24, 'Too many land-planes')   ; nland  x PLANE
  u32 nextra (<= 12, 'Too many extra-planes')  ; nextra x PLANE
  u32 nrail  (<= 12, 'Too many rail-planes')   ; nrail  x PLANE
PLANE (104 bytes):
  +0x00  4 x { f32 x, y, z, tu, tv }           ; 4 corners
  +0x50  6 x i32 tri                           ; 2 triangles, corner numbers 0..3 (stored as i16 at plane+0x80)
```

Observed maxima per chunk: road 15, land 22, extra 12, rail 12. `tu`/`tv` are always inside [0, 1] (every quad maps a
part of its texture; the road quads map half the road texture in u: 0.004..0.496 or 0.5..0.996). Triangle patterns:
(0,2,1,0,3,2) and (3,2,1,3,1,0) dominate; (0,1,2,0,2,3) occurs only in the rail group.

Data quirk: the last chunk of the open tracks 1 and 3 has 6 road quads with corners 1 and 2 at exactly (0,0,0) (the
editor connected them to a non-existent next chunk). The original loads and draws them unchanged; the OBJ export skips
those triangles (`skip_origin`). Waypoint/rail y values that should be 0 are sometimes tiny denormals (e.g. 8e-40).

### 3.2 `.pmp` - chunk data (`chunk_load_pmp` 0x442b00)

```
u32 nchunks
nchunks x {
  +0   f32 x, y, z          -> chunk+0x24  chunk position (start of the chunk, on the road centre line)
  +12  i32 trigger_a        -> chunk+0xec  trigger handler id (0 = none)
  +16  i32 trigger_b        -> chunk+0xf0
       u32 nrails (<= 12)   -> chunk+0xe0, then nrails x i32 -> chunk+0xf4[12]   .cmp indices
       u32 nleft  (<= 12)   -> chunk+0xe4, then nleft  x i32 -> chunk+0x124[12]  .dmp indices, side 0
       u32 nright (<= 12)   -> chunk+0xe8, then nright x i32 -> chunk+0x154[12]  .dmp indices, side 1
}                                      ('chunk waypoint left overflow' asserts both lists <= 12, 0x442bce/0x442c3b)
```

Verified: the rail lists of all chunks concatenated are exactly 0..nrails-1 in order; the waypoint lists together are a
permutation of 0..nwaypoints-1 (each waypoint belongs to exactly one chunk/side/lane). Lane counts: 0..4 left,
0..6 right. Note the rail list count is read before the waypoint counts although it is stored at +0xe0 (0x442b6d).

### 3.3 `.cmp` - guard-rail collision segments (`rails_load_cmp` 0x44c2f0, `rail_set` 0x44c440)

```
u32 n
n x { f32 px, py, pz,    -> rail+0x00  start point
      f32 dx, dy, dz }   -> rail+0x0c  vector from start to end (length ~128 = one chunk)
```

Typical: 2-6 per chunk, alternating left/right edge of the road. Derived at load (0x44c440):
`rail+0x5c len = sqrt(dx^2 + dz^2)` (horizontal length), `rail+0x18 yaw = (atan2(dz, dx) - pi/2) * 1024 / (2 pi)`
(float, 1024ths of a turn; the `fsubp` operand order is read as Intel `st1 - st0` **(?)**).

### 3.4 `.dmp` - driver map / waypoints (`drmap_load` 0x443c40, `drmap_new_waypoint` 0x443b40, ctor 0x4443a0)

```
u32 n
n x {                    (36 bytes, stored 1:1 at waypoint+0x00..+0x23)
  +0x00 i32 flags        ; observed 0,1,2,4,128,129,130,132 (bit 3 = 8 is tested by AI code 0x40c07b, never set) (?)
  +0x04 i32 flags2       ; observed 0,2,9,10                                                                     (?)
  +0x08 f32 x, y, z      ; waypoint position (lane centre at the chunk start)
  +0x14 i32 next         ; next waypoint in driving direction, same lane (-1 = none)
  +0x18 i32 next_inner   ; next waypoint, lane - 1 (towards the centre line)
  +0x1c i32 next_outer   ; next waypoint, lane + 1
  +0x20 i32 prev         ; previous waypoint, same lane
}
```

Link semantics are statistical (all 7 tracks): for side 1 (right) `next/next_inner/next_outer` are in chunk c+1 and
`prev` in chunk c-1; for side 0 (left) the other way round. Exceptions (13-36 per track) are where lanes merge or split.
Lane 0 is the lane nearest the road centre line; side 0 waypoints lie on the -right side of the chunk axis, side 1 on
the +right side. So traffic on side 1 drives towards higher chunk numbers (the player's direction, keep right), side 0
is oncoming traffic. Waypoint semantics beyond this are AI territory (ai_*.cpp).

## 4. In-memory structures

All structs are byte-packed (fields at odd offsets, e.g. `g_road+0x4a`). Arrays are `T_ARRAY<T>` (dplib array.hpp):
`{T* data; int size; int next_free; int initialised; char* used; char name[25]; char type[25]}`, elements allocated with
`get_new()` (first free slot from `next_free`, so on a fresh load element *i* is the *i*-th one read).

### 4.1 Road object `g_road` (0x4e5d28), packed

| off | type | meaning | written by |
|---|---|---|---|
| +0x00 | i32 | pools allocated flag (freed by 0x444b20 when 1) | |
| +0x04 | T_ARRAY<int> | `m_chunks`: data +0x04 (0x4e5d2c), size +0x08 (0x4e5d30): chunk index for road position i (identity) | 0x444e2e |
| +0x4a | i32 | texture id, road group (`eNN_weg`) | 0x441e4c |
| +0x4e | i32 | texture id, land group (`eNN_gras`) | 0x441e45 |
| +0x52 | i32 | texture id, rail group (`eNN_vang`) | 0x441e3f |
| +0x56 | i32 | texture id, extra group (`eNN_gra2`) | 0x441e53 |
| +0x5a/+0x5e/+0x62/+0x66 | ptr, ptr, i32, i32 | rail group: vertices (n*12*4 x 32 B), u16 indices, nverts, nindices | 0x444650 |
| +0x6a/+0x6e/+0x72/+0x76 | | road group (capacity n*16 quads) | |
| +0x7a/+0x7e/+0x82/+0x86 | | land group (n*24 quads) | |
| +0x8a/+0x8e/+0x92/+0x96 | | extra group (n*12 quads) | |
| +0x9a | i32 | 1 = hardware path (raw UVs, SetTexture per group), 0 = software atlas | 0x441e90 <- `g_hw_accel` 0x4d9aac |

Each group is one big vertex buffer + one u16 index buffer for the whole track (largest: etappe5 land, 48944 vertices,
so indices must be unsigned 16-bit).

### 4.2 Chunk (0x184 bytes, `g_chunks` T_ARRAY data at 0x4e65e0, count 0x4e65e4)

| off | type | meaning |
|---|---|---|
| +0x00 / +0x04 | i32 | road group index range [start, end) into `g_road+0x6e` (multiples of 3) |
| +0x08 / +0x0c | i32 | land group range |
| +0x10 / +0x14 | i32 | rail group range |
| +0x18 / +0x1c | i32 | extra group range |
| +0x20 | i32 | loaded (1) |
| +0x24 | vec3 | position (.pmp) |
| +0x30 | vec3 | right = normalize(cross((0,1,0), fwd)) = (fz, 0, -fx)/len |
| +0x3c | vec3 | up = normalize(cross(fwd, right)) |
| +0x48 | vec3 | fwd = normalize(pos[i+1] - pos[i]) |
| +0x54 | i32 | pitch = trunc(atan2(fy, hypot(fx, fz)) * 1024/2pi) |
| +0x58 | i32 | yaw = trunc((atan2(fz, fx) - pi/2) * 1024/2pi)  (= -heading, heading measured from +z towards +x) |
| +0x5c | mat4 | matrix from 0x442cb0/0x442f20 (identity with the basis vectors partly overwritten; looks buggy, no reader found) (?) |
| +0x9c | mat4 | rotation from heading atan2(fx,fz) and pitch (0x442fe0, via 0x45dfa0/0x45df40/0x45e450); copied to objects by 0x442390 |
| +0xdc | f32 | plane d = dot(up, pos) |
| +0xe0 / +0xe4 / +0xe8 | i32 | nrails, nleft, nright |
| +0xec / +0xf0 | i32 | trigger_a, trigger_b |
| +0xf4 | i32[12] | rail indices |
| +0x124 | i32[12] | left waypoints (side 0), lane order |
| +0x154 | i32[12] | right waypoints (side 1), lane order |

Frames (0x444f67..0x445000): for i = 0..n-2: `calc_frame(pos[i+1])`, `calc_matrix`, `calc_angles`. The last chunk is
computed against chunk 0 (also on open tracks), then its +0x5c matrix, +0xdc, right, up and fwd are overwritten with
those of chunk n-2 (0x4450ad..0x445192); its +0x54/+0x58/+0x9c keep the values computed towards chunk 0.

### 4.3 Plane (temporary, 0x8c bytes, pool `g_planes` 0x4ea2f8, 152 entries, freed after each chunk 0x443362)

`4 x D3DLVERTEX {f32 x,y,z; u32 reserved; u32 color = 0x00FFFFFF; u32 specular = 0; f32 tu, tv}` (ctor 0x44bd80)
+ `i16 tri[6]` at +0x80.

### 4.4 Waypoint (0x50 bytes, `g_driver_map` 0x4e6628, count 0x4e662c)

+0x00..+0x23 = file record; +0x24 i32 object count; +0x28 i32[10] objects on this waypoint (obstacle list, -1 = free;
`waypoint_add_object` 0x4442a0, `waypoint_remove_object` 0x4442d0).

### 4.5 Rail (0x60 bytes, `g_rails` 0x4ea340, count 0x4ea344)

+0x00 vec3 p, +0x0c vec3 d, +0x18 f32 yaw (1024 units), +0x1c.. 4x4 matrix (identity at ctor 0x44c410; 0x44c500
fills a rotation from d, no caller found), +0x5c f32 horizontal length.

## 5. Geometry build (`chunk_build_geometry` 0x4433d0)

Called per chunk with the four plane lists. For the groups in the order road, land, rail, extra:
`chunk.start = pool.nindices`; for each plane: append its 4 vertices to the group's vertex pool (32-byte copy, after the
UV transform below) remembering their global numbers; append `tri[0..2]` and `tri[3..5]` mapped through those numbers;
`chunk.end = pool.nindices`. Vertex helpers: road 0x4449c0, land 0x444940, rail 0x444a30, extra 0x444aa0; index
helpers 0x4447a0 / 0x444800 / 0x444870 / 0x4448d0. No vertex sharing between quads.

**Texture per group** (Raser.ini `[MAPS] N=type,etappeN,weg,gras,gra2,vang,...`, parsed by `gm_load_track_info`
0x4225d0 into `g_maps` 0x4d8ea0 as `"%s.pcx"`; ids from `get_texture_index` 0x42b220; set by 0x424131 ->
`rceng_init_engine` 0x441de0):

| .rmp list (file order) | pool | texture | ini arg |
|---|---|---|---|
| 1 road | +0x6a | +0x4a | 2 `eNN_weg` |
| 2 land | +0x7a | +0x4e | 3 `eNN_gras` |
| 3 extra | +0x8a | +0x56 | 4 `eNN_gra2` |
| 4 rail | +0x5a | +0x52 | 5 `eNN_vang` |

There is no per-quad texture id: the group decides the texture. Track 6 uses the e00 textures.

**UVs.** Hardware path (`g_road+0x9a = 1`, display modes 2/3): the file `tu, tv` are used unchanged. Software path
(`+0x9a = 0`): `tu' = |tu| * g_atlas_u_scale(0x4eab9c) + tex[id].u0(+0x34)`, `tv' = |tv| * g_atlas_v_scale(0x4eaba0) +
tex[id].v0(+0x38)` with `tex = *(0x4eaa28)`, 72-byte entries: all textures live in one atlas page (writer of the
scales not found **(?)**). A reimplementation should use the hardware path: one texture per group, file UVs, wrap or
clamp irrelevant because all UVs are inside [0,1].

## 6. Rendering

Per frame (game loop 0x425090, after BeginScene):

```
start = road_visible_start(2)            ; 0x4252b0
road_draw(start, g_visible_chunks)       ; 0x4454f0, g_visible_chunks 0x4d9a8c = Game.ini [OPTIONS] visiblechunks (30)
objects_draw(...)                        ; 0x440990 (same window logic, objects agent)
```

`road_visible_start(behind)` 0x4252b0: `c = g_cam_chunk` (0x4a70cc, the chunk of the object the camera follows, set
in 0x4142b0); `v = normalize(camera target - camera eye)` (camera +0x54..+0x5c minus +0x08..+0x10);
`d = dot(v, chunk[c].fwd)`:

* `d > 0.5736` (cos 55 deg, looking forward): `start = c - behind`
* `d < -0.5736` (looking back): `start = c + behind - visible`
* otherwise: `start = c - visible/2`
* if `start < 0`: `start += n - 1` (sic, n - 1).

`calc_start_and_end_chunk(start, count)` 0x445cf0: `s = clamp(start, 0, n-1)`, `e = max(start + count, 0)`; if `e >= n`
the window wraps: draw `[s, n-1]` and `[0, e - n]`, else `[s, e]` (inclusive, so `count + 1` chunks).

`road_draw_range(first, last)` 0x445550: world matrix = identity (0x4455bc, SetTransform WORLD); then four
`IDirect3DDevice2::DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_LVERTEX, pool.verts, pool.nverts,
pool.index + chunk[first].start, chunk[last].end - chunk[first].start, flags)`:

1. road  (0x445695, flags 8 = D3DDP_DONOTUPDATEEXTENTS), texture +0x4a
2. land  (0x44579f, flags 8), texture +0x4e
3. rail  (0x4458d3, flags 8), texture +0x52
4. extra (0x4459dc, flags 1 = D3DDP_WAIT), texture +0x56

In the hardware path each call is preceded by SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, tex[id].handle) (only when it
changes, cache 0x4ea834) and an update of the material/texture record (0x4ea980[0x49b008]). In the software path no
texture change happens (the atlas is bound by the frame setup 0x4250cd). Vertex colour is always 0x00FFFFFF and
specular 0 (D3DLVERTEX = pre-lit, no D3D lighting); fog is the device's (FOG errors are reported from 0x4455e0).
The vertex-colour lighting bake 0x445ea0 / 0x445fb0 / 0x4465a0 (point light with falloff, chunks 0..20 only) is dead
code: no references.

Facing: the road and land triangles face up for clockwise order seen from above in left-handed coordinates (D3D front
faces with the default D3DCULL_CCW); the rail group contains both windings. Whether culling is enabled while the road
is drawn is decided by the render setup, not in rd_main.cpp **(?)**.

## 7. Per-chunk lookups

**Which chunk is a point on** - `road_find_chunk(start, x, y, z)` 0x445a10 (y unused). With
`b(c) = (x - pos[c].x) * right[c].z - (z - pos[c].z) * right[c].x` (= -dot(P - pos[c], fwd[c]) in xz; `b > 0` means P is
before the start line of chunk c):

```
c = start
if b(c) > 0:  for c = start .. 0 step -1:  if b(c) < 0: return c          ; walk back
              -> fall through to the full scan
else:         for c = start .. n-1:        if b(c) > 0: return c-1 (n-1 if c-1 == -1)
              -> fall through
full scan:    for c = 0 .. n-1:            if b(c) > 0: return c-1 (n-1 if c-1 == -1)
              return n                                                       ; (bug: out of range)
```
(An extra backward scan from n-1 at 0x445bb3 is only reached when the scan index is exactly 0, which cannot happen
for n > 0.) `rceng_update_object_chunk` 0x442020 keeps `obj+0x1c = previous chunk`, `obj+0x18 = road_find_chunk(obj+0x18,
obj position)`; other callers 0x40ee80, 0x4142b0 (camera), 0x432db0, 0x434df0.

**Triggers** - `rceng_check_chunk_triggers(obj)` 0x442590: if the chunk changed from p to c, `k = max(p, c)` except at
the wrap (n-1 <-> 0) where k = 0; then `g_trigger_handlers[chunk[k].trigger_a](obj)` and `[trigger_b]` if non-null.
`g_trigger_handlers` 0x4e5928 has 256 slots; 0x42a2b0 fills 0..48 from the table at 0x48c488. Id 14 is the finish line
(`trigger_find_start_position` 0x429a20 searches for it); ids used: 1, 2, 4, 5, 6, 9, 13-15, 18-25, 31-34, 40-43, 45 and
some > 48 (86, 94, 107, 130, 150, 196 on chunks 0/1/372/378/725 of the city tracks) which have no handler. Meaning of
the ids belongs to the game logic.

**Waypoints / lanes** - `rceng_get_waypoint_index(chunk, lane, side)` 0x442150: side 0 -> `wp_left`, 1 -> `wp_right`,
`lane = min(lane, count-1)`; `rceng_get_valid_lane` 0x442730 clamps; `rceng_get_waypoint_position` 0x4420f0;
`drmap_find_closest_waypoint(obj, side)` 0x443da0 searches only the object's chunk (side -1 = both lists).

**Guard-rail collision** - `bbox_collect_static(obj)` 0x44a010 (only when `obj+0x38 == 0`): for the chunks
`wrap(c-1), c, wrap(c+1)` and every rail k of that chunk, `bbox_add_rail(chunk, k)` 0x449c10 adds a box (0x4c bytes at
0x4e6790, max 200) with local corners (-10,0,-1), (0,0,-1), (0,0,len+1), (-10,0,len+1), position = rail p, angle =
rail yaw rounded away from zero (ceil 0x471890 if yaw > 0, else floor 0x471790; 0x449d63) - i.e. a 10-unit-thick wall on the local -x side of the
segment from p to p+d, extended 1 unit at both ends. The collision itself is bbox.cpp (physics agent). The rail render
quads (.rmp rail group) are not used for collision.

**Helpers** - `rceng_wrap_chunk` 0x4426e0, `rceng_chunk_distance` 0x442700 (circular), `rceng_get_chunk_position`
0x442090, `rceng_set_object_to_chunk_angle` 0x442230 (object yaw = (atan2 of chunk fwd - pi/2) * 1024/2pi, +512 = reversed for direction 0),
`rceng_set_object_to_chunk_orientation` 0x442390.

## 8. C sketch

```c
typedef struct { float x, y, z; uint32_t reserved, color, specular; float tu, tv; } LVertex;   /* 32 bytes */
enum { G_ROAD, G_LAND, G_RAIL, G_EXTRA };              /* draw order; .rmp file order is ROAD, LAND, EXTRA, RAIL */
typedef struct { LVertex *v; uint16_t *idx; int nv, ni; int tex; } RoadPool;
typedef struct {
    int range[4][2];                                   /* [group][start,end) into RoadPool.idx */
    float pos[3], right[3], up[3], fwd[3], plane_d;
    int pitch, yaw;                                    /* 1024 per turn */
    int trigger[2];
    int nrails, rails[12], nleft, left[12], nright, right_wp[12];
} Chunk;
typedef struct { int flags, flags2; float pos[3]; int next, next_inner, next_outer, prev; } Waypoint;
typedef struct { float p[3], d[3]; float yaw, len; } Rail;
```

## 9. Open questions

* Meaning of waypoint `flags` / `flags2` bits and of the trigger ids (game/AI side).
* The software-path atlas scale globals 0x4eab9c/0x4eaba0: writer not found (irrelevant for a hardware renderer).
* Chunk +0x5c matrix appears unused; the exact axis conventions of the +0x9c matrix were not traced into 0x45dfa0.
* Cull mode / z-bias for the rail group while drawing the road (render setup).
* `road_find_chunk` returns n when the point is past the start line of every chunk (only possible far off an open
  track); callers do not guard against it.
