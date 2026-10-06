# Images, palettes and fonts

All 2D graphics of A2 Racer II: the `.pcx` and `.tga` images and the `.fnt` fonts in RCS (race), the `.pcx` screens and `.img` fonts in Mrcs (menu), and the two unnamed sprite banks RC and MENURC. `tools/imgconv.py` converts all of them to PNG in `out/png/<bank>/`. `--test` parses every file without writing anything (1231 files, all pass). Addresses are spel.dat unless marked *menu*. Names are in [names/spel_render.txt](names/spel_render.txt) and [names/menu_render.txt](names/menu_render.txt).

## Archives in short
The archive lookup is `file_open` `0x46d8e0`. It compares names case-insensitively and the first match wins. RCS has 5 duplicated names (`DEFAULT.d3d`, `hiradar.pcx`, `e02_pal.pcx`, `e02_w_hi.pcx`, `e02_w_lo.pcx`/`E02_W_LO.PCX`); each pair is byte-identical. `.pcx`/`.tga` file names in code and ini files are used with any case.

| Bank | Used by | Contents |
|---|---|---|
| RCS | spel.dat | 147 PCX + 106 TGA images, 31 `.fnt` fonts (plus meshes and sounds) |
| Mrcs | menu.dat | 8 PCX (7 menu screens 640x480 + black `PAL.PCX`), 7 `.img` fonts (plus sounds) |
| RC (`rc.img/rc.ind`) | spel.dat, `Menuanim.cpp` `anims_init` `0x431900` | 453 sprites: HUD (speedo, needles, gauges, bars), pause/quit panels, result panels, position numbers, and the sign/speed-limit/brake-light images used for texture copies |
| MENURC (`menurc.img/.ind`) | menu.dat, *menu* `init_anims` `0x409e10` | 482 sprites: menu buttons, title bars, controller icons, upgrade icons ("GEKOCHT"), track maps, car pictures, sliders |

## PCX (`rcpcx.cpp` `0x46ae60..0x46bd80`)
Standard ZSoft PCX in one variant only: manufacturer 10, version 5, RLE encoding 1, 8 bits per pixel, 1 plane. A 256-colour palette `0x0C` + 768 bytes RGB sits at the end of the file. `bytes_per_line` can be one larger than the width (odd widths, e.g. `palet.pcx` 73x65). RLE runs (`0xC0|n`, value) may run across scan lines. The 16-colour header palette is junk; ignore it.

| Loader | Purpose |
|---|---|
| `rcpcx_load_palette` `0x46ae60` | only the trailing palette -> current palette `[0x4eace0]`, optionally SetEntries |
| `rcpcx_read_palette_keyed` `0x46af40` | palette of the image being loaded -> current palette. Entry **254** (`[0x4a0be0]`) is replaced by the colour key colour; any other entry with that exact colour is nudged (+1 red) so it does not become transparent |
| `rcpcx_draw_to_surface` `0x46b0b0` | decode directly onto an existing surface (wait screens `gfx_draw_wait_screen` `0x41bde0`); in 8-bit modes it then sets the palette |
| `rcpcx_load_surface` `0x46b500` | new offscreen surface (software sky `SCRL_LO/HI`, `sky_init_moving_sky` `0x41e740`) |
| `rcpcx_load_texture_surface` `0x46b930` | texture surface (`texture_load_file` `0x45ae00`) |

Each pixel goes through `pformat_convert` `0x468380`: an 8-bit surface gets a copy, a 16/24-bit surface gets the RGB of `g_cur_palette[index]` (optionally gamma corrected, see below). **Every PCX is therefore shown with its own palette**, except in 8-bit screen modes, where the hardware palette is the track palette (next section).

### Palettes
Each track's textures share one palette (md5 groups: e00 18 files, e01 7, e02 20, e03 7, e04 19+, e05 7). Indices **0..160, 254 and 255 are identical in all track palettes and in the common images** (`algemeen`, cars, people, shadows, `SCRL_LO/HI`, `RACER0x`). Only 161..253 differ per track. So in 8-bit mode one track palette can show the common art plus that track's textures:

| Index | Colour | Meaning |
|---|---|---|
| 254 | (255,0,127) | colour key of textures and of the 2D image surfaces (pixels with this index are transparent) |
| 255 | (255,0,255) | transparent index of `.fnt` fonts and RC/MENURC sprites (`g_sprite_key_index` `0x49e8b0`) |

Selection, `load_game_palette` `0x420900`, from Raser.ini `[PALET] ETAPPEn=<8-bit>,<16-bit>`. `skelet2d_read_ini` `0x4209b0` copies the two names into the buffers `Pallow.pcx` `0x489601` and `Palhigh.pcx` `0x489611`.
- RESOLUTION 0/1/2/5: `e0N_pal.pcx`, a 16x16 PCX used only as a palette carrier. Track 6 reuses e00.
- RESOLUTION 3/4 (640x480x16): `e02_pl16.pcx` for every track. This is a 640x480 all-magenta image used only as a palette carrier. Its palette is a different, generic one: grey ramp 0..15, skin tones around 160..175, sky blues around 240..253, black at 254, magenta at 255. The 16-bit HUD sprites (RC 0..401), most `.fnt` fonts, `palet.pcx`, `hiradar/loradar.pcx`, `init.pcx` and `A_sfx.pcx` are painted in this palette.

The palette is loaded and then set with `peFlags` 0x40 (PC_NOCOLLAPSE) through `pal_set_flags_and_entries` `0x469e90` and `pal_set_entries` `0x469d10`.

**Speed-camera flash** (`ev_flash_trigger` `0x42a2e0`, sound 11, registered as a game action):
- 8-bit: `ev_flash_init` `0x4156d0` saves the palette. `ev_flash_update` `0x415750` sets all 256 entries to (235,235,235) for 2 frames. `ev_flash_exit` `0x415710` restores the palette.
- 16-bit: the screen is filled once with palette colour 15 (white).

### Colour key and gamma
- The colour key is set by `set_colorkey_rgb` `0x464d30` (`[0x4eac14]`). `init_3d_library` `0x4234e0` sets it to `0x7f00ff` (0xBBGGRR, i.e. **RGB 255,0,127**) in the accelerated mode. That is the colour of PCX index 254 and the transparent colour painted in the TGA files.
- `pformat_convert` `0x468380` and `pformat_rgb24_to_pixel` `0x468230` never produce the key by accident: a non-key colour that converts to the key gets red ±1.
- Game.ini `[GFX OPTIONS] gamma=1` (comment: "0=light, 1=dark"; `g_opt_gamma` `0x4d9b14`) calls `pformat_set_gamma(1)` `0x468220`. Every 8->16/24-bit and 24->16-bit conversion then maps each channel `c -> c*c/256` (implemented as `(c*c + ((c*c)>>31 & 0xff)) >> 8`). It is switched off around loading the random sky backdrop `back1..7.pcx` (`0x423565`). It does not apply in 8-bit modes, because the hardware palette is not changed. `imgconv.py --gamma` reproduces it.

### What the PCX files are
| Files | Size | Use |
|---|---|---|
| `e0N_*` (weg, gras, gra2, vang, bomn, buildings...) | 128² / 256² | track textures in the 8-bit modes ([MAPS] names road/grass/grass2/barrier) |
| `algemeen`, `cars01-03`, `RACER01/02`, `a_trffc1`, `C_*`, `A_*`, `S_*` | 32²..256² | common textures: cars, traffic, people, shadows, effects (`A_sfx`, `S_sfx`), letters (`S_letter`) |
| `e0N_pal.pcx` | 16x16 | 8-bit track palettes |
| `e02_pl16.pcx` | 640x480 | 16-bit palette (image is solid magenta) |
| `SCRL_LO.pcx` 480x200, `SCRL_HI.pcx` 960x320, `Scroll.pcx` 800x200 | | software-mode scrolling sky (`Scroll.pcx` unused); see RENDER.md |
| `back1..7.pcx` | 128² | accelerated-mode sky backdrop, one picked at random per race (`0x423574`, table `0x48a790`) |
| `e0N_w_lo.pcx` 320x200, `e0N_w_hi.pcx` 640x480 | | "wait" (loading) screens with the route map, Raser.ini `[wait_lo]`/`[wait_hi]`; 8-bit modes only (the 16-bit mode uses the `.tga` of the `[wait_hi]` name), so `e0N_w_hi.pcx` only shows in 640x480x8 |
| `loradar.pcx` 64², `hiradar.pcx` 128² | | all index 254 (empty): radar texture that is drawn into at run time (texture copy) |
| `init.pcx` | 2048² | 8-bit/RAMP path: the "main accumulator texture", texture 0. `texture_set_partition(8.0)` `0x459d50` splits it into 8x8 slots of 256². It holds a stray screenshot; its content is irrelevant (`texture_init_accumulator` `0x459db0`) |
| `palet.pcx`, `normcar1.pcx`, `_BACKUP_.PCX`, `E04_recl.pcx` | | not referenced by name in spel.dat or the track files (`_BACKUP_` is an old wait screen) |
| Mrcs `hoofd`, `garage`, `showroom`, `opties`, `bedien`, `hiscore`, `Newgame` | 640x480 | menu screens, all with the same palette (used for MENURC and the `.img` fonts) |
| Mrcs `PAL.PCX` | 640x480 | black image with a different palette (unused?) |

## TGA (`tga.cpp`, `tga_load_surface` `0x4636d0`)
Used instead of the PCX files in the accelerated 640x480x16 mode: `texture_set_source_type(1)` `0x459cf0` in `init_3d_library`. `texture_load_file` `0x45ae00` strips the extension of the requested name (copied to max 14 chars) and appends `.pcx` or `.tga`.
- Header: 18 bytes. Colour-map type must be 0 ("TGA cannot be colormapped") and image type 2 (uncompressed true colour). 24 bpp (all files) or 32 bpp (only `E04_vang.TGA`). The loader seeks to the pixel data, so the image ID field is skipped.
- Rows are bottom-up unless descriptor bit 5 is set (none set). Pixels are B,G,R(,A). The alpha byte is read and **discarded**; transparency comes only from the colour key RGB (255,0,127).
- Many files end with a 26-byte TGA 2.0 footer or an extension area (up to 7 KB); this is ignored.
- `c_texture_load` `0x45af20`: when `resizetexture=1` (`[0x4eaba4]`) a non-square texture is stretched to a square of side `2^((log2 w + log2 h)/2)`.
- Wait screens: `gfx_draw_wait_screen` `0x41bde0` draws the `[wait_lo]` PCX in 8-bit modes (`rcpcx_draw_to_surface`) and, in 16-bit mode, loads the `[wait_hi]` name with the extension replaced by `.tga` (`e0N_w_hi.TGA`, 640x480) through `tga_load_surface` and blits it. `e02_h_hi.TGA` is unused.

## .fnt fonts (race; `ascii.cpp` `ascii_load_font` `0x46a2d0`)
```
struct { s16 w, h; u32 offset; } glyph[256];   // char code -> glyph; w<=0 or h<=0 (-1,-1) = no glyph
s16 hdr[4];                                     // copied to font+0x70..0x7c, never used by the drawing code
u8  pixels[];                                   // glyph c: w*h bytes at glyph[c].offset (absolute), row-major
```
- The pixels are palette indices. 255 = transparent (DirectDraw source colour key from `[0x49e8b0]`). Each glyph becomes its own surface.
- How the pixels are converted depends on `pformat_set_mode` `0x468210`:
  - mode 0: 8-bit copy
  - mode 1: index -> 16-bit through the current palette
  - mode 2: index -> 24-bit
  - mode 4: reads 3 bytes/pixel (RGB fonts, not used by any file)
- Drawing (`ascii_draw_string` `0x46a180`): x advances by glyph width + the font's x spacing (`ascii_set_spacing` `0x469f70`); `'\n'` starts a new line, `'\t'` advances by the width of glyph 9.
- The 17 font slots × 2 sets are the table `0x486d08` (records of 0x94 bytes, name first), loaded by `gfx_asci_init_fonts` `0x418330`. The set index is `[0x4d9a70]==0`, so set 0 is used in the 8-bit modes and set 1 in the 16-bit mode. Which slot draws what is in [HUD.md](HUD.md).
- Most fonts, including several of set 0 (`Logeel`, `lo_vlag`, `MARC10`), are painted in **e02_pl16** indices. `imgconv.py` writes every font twice: `<name>.png` with e02_pl16 and `<name>_pal8.png` with e00_pal. A `<name>.txt` lists the metrics and the slots the font occupies.
- Unused files: `HiKMTel/LoKMTel` (big km/h digits), `hi_req/lo_req`.

| Set 0 (8-bit) slot 0..16 | lo_fnt3, lomid, logeel, lo_geld, lo_vlag, lo_bts, lo_fnt1, marc10, slider13, slider15, lo_fnt1, lo_fnt2, lo_fnt4, lo_fnt1, lo_fnt8, lo_fnt9, lo_fnt1 |
|---|---|
| Set 1 (16-bit) slot 0..16 | olfnt03, himid, higeel, hi_geld, hi_vlag, hi_bts, olfnt07, marc10, slider13, slider15, olfnt01, olfnt02, olfnt04, olfnt06, olfnt08, olfnt09, lo_fnt1 |

## .img fonts (menu; *menu* `ascii_load_img_font` `0x40eec0`)
Fixed-cell fonts:
```
u8  present[256];        // 1 = character exists
s16 w, h;                // cell size (font+0x80/+0x84)
s16 c, key;              // font+0x70 / +0x74: c = 200/253/63/112 (unknown, maybe text colour index), key = 255
u8  cell[256][h][w];     // all 256 cells are stored, also for absent characters (file size = 264 + 256*w*h)
```
Pixels are indices into the menu palette (that of `hoofd.pcx`); 255 = transparent.

| File | Cell | Notes |
|---|---|---|
| STDFONT, cyber | 8x17 | |
| ascii12 | 8x12 | |
| CARVED, SMALLBT | 10x17 | |
| BUTFNT, midfont | 10x26 | BUTFNT: button captions, two colour variants of the capitals |

## RC / MENURC sprites (`sprite.cpp`)
Every entry of the unnamed banks is one sprite:
```
u16 w, h;
s16 hotx, hoty;          // hot spot (can be negative, e.g. needle frames)
u8  pixels[w*h];         // palette indices, 255 = transparent
```
The `.ind` file is `u16 count` + `count × u32 offset`. `sprite_load_bank` `0x46c1c0` (img, ind, id list ending in -1) seeks the index to `id*4-2`, so **sprite id N is bank entry N-1**. Loaded sprites live in the 32-byte array `[0x4eff90]`: +0 surface, +8 w, +0xc h, +0x10 hotx, +0x14 hoty. `sprite_load_all` `0x46c6b0` / *menu* `0x414af0` read whole banks.

RC palette: entries 0..401 are the 16-bit set (e02_pl16) and 402..452 the 8-bit set (track palette). MENURC uses the menu palette. What each RC sprite is and where it is drawn: [HUD.md](HUD.md).

## Pixel formats (`pformat.cpp`, `txformat.cpp`)
- `pformat_dump` `0x468850` prints the surface format. The R/G/B shifts and positions are at `0x4eaca4..0x4eacb8`.
- `txformat_enum_texture_formats` `0x4619c0` lists the device texture formats. In the 16-bit mode, textures are converted to the screen's 16-bit format (565 or 555) at load time.
- For a reimplementation, convert everything to RGBA8 at load time: index 254 / RGB(255,0,127) gives alpha 0 for images and textures, index 255 gives alpha 0 for fonts and sprites. Apply `c*c/256` when emulating gamma=1.

## Open questions
- The 4 header shorts of `.fnt` files: (252,255,253,254), (0,0,0,0), (240,255,296,0) and others. They are never read after loading; probably leftovers from the font editor.
- The meaning of the `.img` header value `c`.
- Which palette the 8-bit (RESOLUTION 0/1) mode really shows for the fonts painted in e02_pl16 indices (Logeel, lo_vlag). With e0N_pal they would look wrong; maybe these slots are unused in practice (HUD.md lists them as unused).
- `PAL.PCX`, `palet.pcx`, `normcar1.pcx`, `Scroll.pcx`: not referenced by name in spel.dat; menu.dat use not checked.
