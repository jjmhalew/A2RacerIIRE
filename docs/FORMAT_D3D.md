# Meshes (.d3d), textures and how they are paired

The 3D objects of the race (`spel.dat`): cars, wheels, traffic signs, buildings, trees, the 3D font. They come from
the `RCS` archive (unpacked to `out/RCS` by `tools/imgpack.py`). Code: `mesh.cpp` (0x452130-0x456c80), `text.cpp` =
textures (0x459b80-0x45be50), `object3d.cpp` (0x456ed0-0x459b70), the anim tables of `gm_init.cpp` (0x42b310-0x431540).
Tool: `tools/d3dparse.py` (self-test, OBJ/MTL/PNG export, table dumps). Names: [names/spel_objects.txt](names/spel_objects.txt).
Placement of the objects on the tracks: [FORMAT_OBJECTS.md](FORMAT_OBJECTS.md).

Status: the format is validated byte-exact to EOF on all 448 `.d3d` entries of RCS.IMG (`python tools/d3dparse.py
test`); every claim below is from the code at the given address unless marked **(?)**.

## 1. File format

```
u32 nverts
u32 nfaces
nverts x 33 bytes:  f32 x, y, z         position
                    f32 nx, ny, nz      normal       (dropped by the loader, see below)
                    f32 u, v            texture coordinates
                    u8  marker          1 or 109     (never read: fread of 33 bytes, only 8 floats copied)
nfaces x 9 bytes:   u16 a, b, c         vertex indices (all < nverts)
                    u16 word            (never read)
                    u8  marker          1 or 109     (never read)
```

Loader `mesh_load_d3d` 0x455b50 (`this` = mesh, arg = file name):

- `file_open` 0x46d8e0 (plain file or case-insensitive lookup in the resource archive), `fread(&nverts,4,1)`,
  `fread(&nfaces,4,1)`, `mesh_alloc(nverts, 3*nfaces)` 0x454370.
- Per vertex `fread(buf, 0x21, 1)` (0x455c34), then by mesh type `[mesh+0]`:
  - type 1 (D3DVERTEX, array `[mesh+0x78]`, 32 bytes): x, y, z, nx, ny, nz, u, v copied.
  - type 2 (D3DLVERTEX, array `[mesh+0x80]`, 32 bytes): x, y, z -> +0/+4/+8, **color = 0x00ffffff** (+0x10),
    **specular = 0** (+0x14), u, v -> +0x18/+0x1c. The normal is discarded.
- Per face `fread(buf, 9, 1)` (0x455e10); only the three u16 indices are stored (`[mesh+0x98]`, 6 bytes per face).
- `mesh_post_load` 0x455ea0 builds the per-type helpers (0x455f20 / 0x456110).
- Errors: `"Unable to load object, can't open file %s"`, `"error reading <%s>"` (ferror check after every record).

**Every mesh of the game is loaded as type 2** (all 27 loader functions push 2 before `mesh_new_load` 0x452660, e.g.
0x42b47f), i.e. unlit, white pre-lit vertex colour, colour = texture. The normals in the files are never used (they
are ~98% unit length, the rest length 0, 2 or other). Mesh array `c_mesh` (`[0x4ea898]`, 0xbc = 188 bytes per mesh,
`mesh_array_init(1186)` 0x4522a0 called from `init_3d_library` 0x4234e0); `+0xa8` = file name.

### The ignored bytes

The marker byte is constant per file and identical in the vertex and the face records; the face word is constant per
file too (`d3dparse.py test` asserts this):

| marker | files | face word |
|---|---|---|
| 1 | 371 | always 0x0700 |
| 109 (`'m'`) | 77 (cars `n_*`, `r_*`, `mpolizei`, `adac`, `C_HELI`, blades, `C_KEET`, `A_RECL0x`, ...) | an arbitrary value with the low nibble 0 (0xd600, 0xcd70, ...) |

They look like uninitialised fields of the converter that wrote the files (the 109 group is probably a different
converter run) **(?)**. A reimplementation can ignore them, as the original does.

### Odd files

- `E04_koor.D3D` (4826 bytes) starts with 24 zero bytes, followed by a valid mesh (118 vertices, 100 faces, exactly
  to EOF). The original would read nverts = nfaces = 0 there; it does not matter, the file is not in the mesh table.
- `DEFAULT.d3d` is stored twice in RCS.IMG (entries 22 and 111, identical), so `out/RCS` holds 447 distinct `.d3d`.
- 90 `.d3d` files are not referenced by the mesh table (e.g. `a_blinks`, `a_mens1..11`, `vpolizei`, `vracht`,
  `e04_viad_try1`, `e01_sc~1`) - leftovers on the CD.

## 2. Coordinates and units

- Direct3D left-handed, **+y up**, x right, +z forward. A car mesh is centred on x/z with the wheels at y = 0 and
  its **front at +z** (`n_golf`: x -9.5..9.5, y 0..15, z -20.7..20.7).
- **1 unit = about 10 cm**: Golf 41 x 19 x 15 units (real 4.1 x 1.7 x 1.4 m); road chunks are exactly 128 units
  (12.8 m) apart and a track is ~100 000 units (~10 km).
- Winding: no CULLMODE render state is ever set (see [RENDER.md](RENDER.md)), so D3D's default `D3DCULL_CCW`
  applies: a face (a, b, c) is visible when it is **clockwise on screen**. Checked with a software render of
  `n_golf`/`mpolizei` with that culling (closed, correct-looking cars).
- UV: D3D convention, (0,0) = top-left of the texture, wrap addressing (u reaches 40 and v 214 in some meshes, i.e.
  tiling; negative values occur).

## 3. How a mesh gets its texture

A `.d3d` has no material or texture reference. The pairing lives in four static tables in spel.dat `.data`
(all read by `tools/d3dparse.py Spel`):

| table | address | entries | entry |
|---|---|---|---|
| mesh names | `g_mesh_table` 0x48d0e0 | 357 + NULL | 12 bytes `{char *name; int handle = -1; int refcount}` (names at 0x4943bc..0x495860) |
| texture names | `g_texture_table` 0x48e1a0 | 91 + NULL | same layout (names at 0x493e70..0x4943b0, all `*.pcx`) |
| frames | `g_frame_table` 0x48e5e8 | 386 | 16 bytes `{int mesh_handle = -1; int tex_handle = -1; int mesh; int texture}` |
| anims | `g_anim_table` 0x492050 | 312 | 9 bytes packed `{int nframes; int *frames; u8 flag = 1}`; frame lists at 0x48fe08..0x492050 |
| anim names | `g_anim_names` 0x490430 | 312 | `char *` (e.g. `"N_GOLF_ANIM"`), parallel to the anim table |

- A **frame** = one mesh + one texture. An **anim** = a list of frames (the `_ANIM` names used by the track files and
  the code). Most anims have one frame; multi-frame anims are switched by game logic: `C_STOPLG_ANIM` /
  `C_STPLR2_ANIM` = traffic light green/orange/red meshes, `A_PIJL1`/`A_LPIJL1`/`C_PIJL1` = blinking arrow (2 frames),
  `FONT3D_ANIM` = 49 letters `LTR_*`, `RADAR_TEXTURES_ANIM` (lo/hi radar). The `E0x_ALGEMENE_TEXTURES_ANIM` anims
  (grass, grass 2, road, rail textures of a track), `RADAR_TEXTURES_ANIM` and `S_SFX_ANIM` are texture-only: their
  frames use `r_porche.d3d` (mesh 49) as a dummy mesh. `OBJ_LIGHT01_ANIM` points its frame list at the zeroed
  .bss int 0x4dcaac = frame 0 (`s_licht.d3d` + `s_sfx.pcx`, re-coloured at run time by `0x41b400`).
- At start-up the tables are registered as static T_ARRAYs: `frames_static_init(0x48e5e8, 386)` 0x45d650 ->
  `g_frames` (`[0x4ea420]`, name "frame3d_ptr"), `anims3d_static_init(0x492050, 312)` 0x44d320 -> `g_anims3d`
  (`[0x4ea4f8]`, "anims3d_ptr"/"anim3d_t") (done once, flag 0x4dcab0, by the first loader function).
- `get_anim_name_index(name)` 0x42b310: linear `_stricmp` (0x47d4f0) over the 312 names; `"anim <%s> not found"`
  returns 0.
- **Every mesh is used with exactly one texture** (357 meshes, no mesh appears with two textures), so for export a
  mesh -> texture map is well defined (`Spel.mesh_textures()`); the anim/frame indirection only matters for
  multi-frame objects.
- Many meshes share one texture page (an atlas): e.g. `cars02` holds Golf, Astra and bus; the mesh UVs address the
  whole 256 x 256 page directly (`n_golf` uses u, v 0.5..1.0). There is no per-mesh UV offset in 16-bit mode.

### Loading (`load_anim_config` 0x4236d0, called from `init_game` 0x423d20)

The game loads only what the current track needs. Per mode (`g_hw3d` 0x4d9aac: 1 = 640x480x16 hardware, 0 = 8-bit
software) and track it calls loader functions; each walks a -1-terminated list of anim indices, loads every frame's
mesh (`mesh_new_load(2, name)`) and texture once (refcounts in the name tables, handles written into the frame) and
then a -1-terminated list of extra texture indices (all empty in the data). `release_anim_config` 0x4219a0 calls the
matching free functions (decrement refcount, `mesh_free` 0x4537b0 / `texture_free` 0x45a950 at 1).

| mode | common | highway (1,3,5) | city (0,2,4,6) | per track |
|---|---|---|---|---|
| 16-bit | 0x42b360 (list 0x490910) | 0x42be40 (0x490aa0) | 0x42c920 (0x490ed0) | 0x42d400 e0/e6, 0x42dee0 e1, 0x42e9c0 e2, 0x42f4a0 e3, 0x42ff80 e4, 0x430a60 e5 |
| 8-bit packed | 0x42b700 (0x4909d0) | 0x42c1e0 | 0x42ccc0 | 0x42d7a0, 0x42e280, 0x42ed60, 0x42f840, 0x430320, 0x430e00 |
| 8-bit | 0x42baa0 (0x490a70) | 0x42c580 | 0x42d060 | 0x42db40, 0x42e620, 0x42f100, 0x42fbe0, 0x4306c0, 0x4311a0 |

Etappe 6 uses the etappe-0 lists (jump table 0x4238e8). Full lists: `python tools/d3dparse.py configs`; the
"packed" lists load their textures with `texture_load_packed` 0x45a810, the others with `texture_load` 0x45a460.
`d3dparse.py test` / `objparse.py test` check that every anim named in a track's `.omp` is in that track's 16-bit lists.

## 4. Textures

`texture_load_file` 0x45ae00 copies the table name (max 14 chars), cuts it at the first `.` and appends the
extension of the **texture source** `g_tex_source` 0x4eaba8 (`texture_set_source_type` 0x459cf0):

- 0 -> `.pcx` (8-bit paletted, `rcpcx` loader 0x46b930) - the 8-bit software modes;
- 1 -> `.tga` (24-bit, `tga.cpp` 0x4636d0) - set by `init_3d_library` (0x42359f) when `g_hw3d`.

All 91 table textures exist as both `.pcx` and `.tga` in RCS (sizes can differ, e.g. `s_shadow` 32x32 PCX vs 64x64
TGA; UVs are normalised so this does not matter). Textures are converted to the surface format on load; on devices
that need square textures (`texture_set_resize` 0x459d30, `g_tex_square` 0x4eaba4) a non-square image is stretched
to a square power of two (0x45afb7).

**Transparency = colour key.** Each texture surface gets a source colour key (`SetColorKey(DDCKEY_SRCBLT)`,
0x45b235/0x45b26a) whose value is `texture_colorkey_from_index` 0x45be50 = `pformat_convert(g_tex_colorkey_index, 8)`, i.e.
palette entry **0xfe = 254** (0x49b024, changeable via 0x45be80) converted to the surface pixel format. In every
texture PCX entry 254 is **RGB (255, 0, 127)**, and the TGAs use exactly that colour for their transparent pixels
(`a_trffc1`: 6147 px, `C_people`: 9231 px). The device enables `D3DRENDERSTATE_COLORKEYENABLE` (state 41, 0x45fc66).
So: transparent where the 16-bit converted texel equals the converted (255,0,127) (in 5:6:5: r >= 248, g <= 3,
120 <= b <= 127); the exporter writes that as alpha 0. No alpha channel or blending is used for meshes.

### 8-bit software mode: one shared texture page

In the 8-bit (RAMP) path all "packed" textures are blitted into **texture 0**, the "main accumulator texture"
`init.pcx` (`texture_init_accumulator` 0x459db0; must be texture index 0):

- `texture_set_partition(8.0)` 0x459d50 (0x423519, only when not `g_hw3d`; default 16) -> `textures_init` 0x45a020
  sets the cell size `g_atlas_u_scale`/`g_atlas_v_scale` (0x4eab9c/0x4eaba0, written at 0x45a04f/0x45a055) = 1/8 and
  the cursor `g_tex_page_u/v` (0x4eab94/0x4eab98) = 0. `init.pcx` is 2048 x 2048, so a cell is 256 x 256 texels.
- `texture_create_packed` 0x45b370 (the 8-bit twin of `c_texture_load` 0x45af20) stretches the image into the next cell (8 x 8 grid, left to right, top to bottom;
  `"out of main texture space for software texture"` after 64) and stores the cell rectangle in the texture
  (`+0x34 u0, +0x38 v0, +0x3c u1, +0x40 v1`, flag `+0x44 = 1`). Full textures get 0,0,1,1 (0x45a591..0x45a757).
- When such a texture is drawn on a mesh (0x4538c0 region, mesh `+0xb8` remembers the texture), the mesh UVs are
  rewritten in place by `mesh_uv_to_texture_cell` 0x455080: `u' = u0 + |u| * cell_w`, `v' = v0 + |v| * cell_h`
  (undo: 0x4553b0). So in 8-bit mode tiling and negative UVs do not work for packed textures. A reimplementation
  targeting true colour can ignore all of this and use the TGA path.

## 5. Export (`tools/d3dparse.py`)

`python tools/d3dparse.py export <name>|all [--pcx]` writes `out/d3d/<mesh>.obj` + `.mtl` and
`out/d3d/tex/<texture>.png` (RGBA, colour key -> alpha 0; `--pcx` uses the 8-bit images, index 254 -> alpha 0).
OBJ is right-handed: vertices are written as (x, y, -z) and faces in file order (mirroring turns D3D's clockwise
front faces into OBJ's counter-clockwise ones); `vt = (u, 1 - v)`; normals likewise mirrored. Meshes not in the
tables get an empty material. `python tools/objparse.py scene N` builds a whole track (see FORMAT_OBJECTS.md).

## 6. Open questions

- What wrote the marker byte / face word (converter version?) - irrelevant for the game.
- Exact 16-bit pixel format used for the colour-key comparison on the original hardware (5:5:5 vs 5:6:5 changes
  which near-magenta texels are also transparent).
- Draw-time texture binding and UV restore details of 0x4538c0..0x453d05 (render agent's area).
