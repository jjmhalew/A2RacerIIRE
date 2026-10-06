# A2 Racer II - the in-race 2D overlay (HUD)

All addresses are in `spel.dat`. "lo" = the 320x200 layout, "hi" = the 640x480 layout. `[?]` marks uncertain items.

## 1. Display modes and which layout/font/sprite set is used

`gm_init` `0x423d20` switches on Raser.ini `[PROGRAM] RESOLUTION` (`[0x4895b0]`, jump table `0x4243d4`):

| RESOLUTION | mode | `[0x4d9a6c]` hi-res | `[0x4d9a70]` 8-bit | `[0x4d9aac]` hw/16-bit |
|---|---|---|---|---|
| 0 | 320x200x8 | 0 | 1 | 0 |
| 1 | 640x480x8 | 1 | 1 | 0 |
| 2, 3 | 640x480x16 | 1 | 0 | 1 |
| 5 | (undocumented) | 0 | 0 | 0 |

There are only two HUD variants, selected by the **8-bit flag `[0x4d9a70]`, not by resolution**:

| | 8-bit (modes 0, 1) | 16-bit (mode 2/3) |
|---|---|---|
| layout row (`0x488510`, see 3) | row 0 (320x200 coordinates) | row 1 (640x480 coordinates) |
| font row (`0x486d08`, `[0x4a7c50]` = `[0x4d9a70]==0`) | `lo_*` fonts | `hi_*` / `olfnt*` fonts |
| sprite anims (rc.img) | list `0x496118` | list `0x496150` |
| HUD layout x offset | +320 when 640x480x8 (`0x41bfc0`, `0x41c0f0`: `x += 0x140` if hi-res && 8-bit) | 0 |

So 640x480x8 draws the 320x200 layout shifted 320 px right (no y change). It looks like a leftover; a reimplementation
can treat mode 1 as "lo layout, x+320" or drop it [?]. The radar, wait screens and sky use `[0x4d9a6c]` (resolution)
instead: radar `0x41cf20`/`0x41c5a0`/`0x41cf90`, wait screen `0x4220e0` (`[wait_lo]`/`[wait_hi]`).

## 2. Primitives

| Function | What |
|---|---|
| `0x41c0f0` hud_draw_item(focus, item, frame, anchor, dx, dy) | sprite `anim[layout[item].anim].frame[frame]` at `layout[item].xy + (dx,dy)`; `sub_468db0` flags: anchor=0 -> 3 (clip, x/y = top-left), anchor=1 -> 1 (clip, x/y = hotspot; HUD hotspots are ~w/2,h/2) |
| `0x41bfc0` hud_draw_text(focus, item, fontrec, center, fmt, ...) | `sprintf` then `ascii_printf(focus, x, y)` `0x46a270`. center=1: `x -= text_width/2` (`0x46a900`: sum of glyph w + (n-1)*xspace), `y -= height('A')/2` |
| `0x46a180` ascii_draw_string | glyph top-left at (x,y), advance = glyph w + font xspace (+0x88); `\n` -> x=0, y += h + yspace (+0x8c); `\t` advances like a glyph |
| `0x469fb0` / `0x469f70` | select font record / set its x/y extra spacing |
| `0x41bf70` | anim/frame -> sprite id with range assert |

Font record = `0x486d08 + (res*17 + font)*0x94`. Font numbers used by the HUD (spacing set in `0x418330`, as xspace,yspace):

| font | 8-bit file (spacing) | 16-bit file (spacing) | used for |
|---|---|---|---|
| 0 | lo_fnt3 (-2,0) | olfnt03 (-8,0) | speed digits `%03d` |
| 1 | lomid | himid | not used by the HUD |
| 2 | logeel (-4,0) | higeel (-4,0) | not used by the HUD |
| 3 | lo_geld (-3,0) | hi_geld (-4,0) | not used by the HUD (menu money?) |
| 4 | lo_vlag | hi_vlag | not used by the 2D HUD [?] |
| 5 | lo_bts (-2,0) | hi_bts (-4,0) | not used by the HUD |
| 6 | lo_fnt1 (0,0) | olfnt07 (-2,0) | pause/quit dialog body lines |
| 7-9 | marc10, slider13, slider15 | same | not used by the HUD |
| 10 | lo_fnt1 (0,0) | olfnt01 (-2,0) | labels, CHECKPOINT, messages, dialog title |
| 11 | lo_fnt2 (-1,0) | olfnt02 (-4,0) | race time, record, position, lap, tweak numbers |
| 12 | lo_fnt4 (0,0) | olfnt04 (-3,0) | city lap times (value column) |
| 13 | lo_fnt1 (0,0) | olfnt06 (-2,0) | city lap-info labels (TIJD/RECORD/LAATSTE) |
| 14 | lo_fnt8 (-5,0) | olfnt08 (-14,0) | GEFINISHT, game-over sweep texts |
| 15 | lo_fnt9 (-1,0) | olfnt09 (-2,0) | split time, result-panel numbers |
| 16 | lo_fnt1 (0,0) | lo_fnt1 (0,0) | damage `%2d@` (16-bit only; `@` is presumably a % glyph [?]) |

(spacing pairs come from `0x469f70(x, y)`; `0x4182f0(rec, a8, a16)` picks a8 in 8-bit and a16 in 16-bit mode.
lo-res also calls `sub_468930(0)` = `[0x4a0268]=0`, a glyph-blit flag [?].)

## 3. Layout table `0x488510` and the sprite anims

`g_hud_layout[2][84]` of `{s32 anim, s32 x, s32 y}` (anim -1 = text item); row 0 = 8-bit/lo, row 1 = 16-bit/hi.
Row 1 entries 34..83 are zero in the file and filled by the CRT static initializer `0x41b860` (it uses screen 640x480
from `[0x49e72c]/[0x49e730]`). Only the items listed in section 4 are referenced; items 0-2, 4-6, 8-13, 16-28, 30-33
are unused leftovers (probably from A2 Racer 1).

Anims come from `rc.img/rc.ind` (Menuanim.cpp `0x431900`..). `g_anim_table` `0x4961e8` = 45 records `{count, ?=1,
frame*}`, frames are `{u32 delay?=2, u32 sprite_id}`. **Sprite id N is rc.ind entry N-1**: the loader `0x46c1c0`
seeks to `id*4-2` in rc.ind (2-byte count header, then u32 offsets), so id N = `out/RC/(N-1).bin`. The file
numbers below are 0-based (`out/RC/NNNN.bin`, `out/png/RC/NNNN.png`). Sprites: `{u16 w,h; s16 hotx,hoty; w*h bytes}`.

| anim (8-bit / 16-bit) | RC files (0-based) | content | used by item |
|---|---|---|---|
| 1 / 0 | 428 (86x15) / 412 (170x26) | CHECKPOINT box | 73 |
| 2 / 3 | 423,420,421,422 / 317,316,315,318 | position badges 1,2,3,4 (frame = position 0..3) | 64 |
| 5 / 4 | 424 (272x149) / 319 (543x306) | highway result panel (RACE TIJD, BUDGET, PRIJZENGELD, RADAR BONUS, NIEUW BUDGET) | 72 |
| 7 / 6 | 419 (272x172) / 314 (543x380) | city result panel (RONDE 1, RONDE 2, TOTAAL, ...) | 62 |
| 8 / 9 | 425 (78x102) / 311 (153x211) | city info panel frame (brackets) | 51 |
| 10 / 11 | 426 (80x59) / 312 (148x134) | speed/turbo/damage panel frame incl. "km h" | 52 |
| 13 / 12 | 427 (42x63) / 313 (67x127) | highway info panel frame | 57 |
| 15 / 14 | 308 (173x65) / 307 (345x139) | chequered-flag box for PAUSE / quit | 34 |
| 16 / 17 | 162 (37x37) / 161 (73x73) | yellow U-turn ("wrong way") sign | 29 |
| 18 / 19 | 430 (60x15) / 429 (124x33) | colour ramp = loading progress bar | 14 |
| 20 / 21 | 11 frames 49x3 / 91x8 | turbo bar (frame 0 = empty .. 10 = full) | 54 |
| 22 / 23 | 10 frames 49x3 / 11 frames 91x8 | damage bar | 3 |
| 43 / 44 | 115,304,306 (5x5) / 114,303,305 (8x8) | radar dots: 0 racer, 1 player, 2 cop | 15 |

Anim lists: 8-bit HUD `0x496118` = {5,1,13,8,10,2,7,43,15,16,18,22,20}, 16-bit HUD `0x496150` =
{0,44,3,4,6,9,11,12,14,17,19,23,21}, 8-bit texture anims `0x496188` = {42,40,28,30,32,34,36,38,24,26}. Anims 24-42
are not HUD: they are texture-copy animations blitted into 3D textures by `txt_main` `0x42ae90` (speed-limit signs
24/25, 26/27 small light pieces, direction signs 28-39 (4 frames each), 40/41 and 42 [?]); `txt_reml` `0x42b100` =
brake lights, `txt_vbrd` `0x42b190` = speed-zone signs (needs Game.ini `texturecopy`). So "txt_" means texture, not text.
Loading: `0x41bd50` (8-bit `0x431970` / 16-bit `0x431a60`), freeing `0x41bda0`; texture anims `0x42b060`.

## 4. Overlay elements

`D` = minimum `overlay_detail` (`[0x4d9b00]`). C = centered text, TL = top-left, HS = hotspot. "t N" = language.ini
text N (NEDERLANDS shown), loaded by `0x431d60` into 56 x 256-byte strings at `0x4dcc5c`; `0x431d30` checks the index.
Hi coordinates = row 1; 640x480x8 uses lo + (320,0).

### 4.1 Highway info panel `0x419b10` (track type 0, D>=3)
| item | element | source | lo | hi | value |
|---|---|---|---|---|---|
| 57 | panel frame | anim 13/12, TL | 239,6 | 485,15 | |
| 38 | "RACE TIJD" | t 33, font 10 | 271,1 | 546,4 | |
| 39 | race time `%02d:%02d:%d` | font 11 | 258,11 | 502,24 | timer 3 (`0x4214d0(3)`, ms): min, sec, tenths |
| 58 | "RACE RECORD" | t 52, font 10 | 254,25 | 522,56 | |
| 59 | record time | font 11 | 258,35 | 514,76 | `[0x4d9acc]`; `--:--:-` when 0x0fffffff |
| 60 | "POSITIE" | t 38, font 10 | 284,49 | 564,107 | |
| 61 | `%d/4` | font 11 | 288,59 | 567,127 | `[0x4d9e60]+1` (4 cars: player + 3 racers) |

### 4.2 City info panel `0x419e00` (track type 1, D>=3)
| item | element | source | lo | hi | value |
|---|---|---|---|---|---|
| 51 | panel frame | anim 8/9, TL | 233,6 | 474,15 | |
| 38, 39 | "RACE TIJD" + race time | as 4.1 | | | timer 3 |
| 40 | "RONDE INFO" | t 34, font 10 | 263,23 | 532,54 | |
| 41 / 42 | "TIJD" / lap time | t 35 font 13 / font 12 | 238,33 / 278,33 | 484,75 / 553,75 | timer 0 |
| 43 / 44 | "RECORD" / lap record | t 36 font 13 / font 12 | 238,42 / 278,42 | 484,92 / 553,92 | `[0x4d9ac8]`, `--:--:-` if 0x0fffffff |
| 45 / 46 | "LAATSTE" / last lap | t 37 font 13 / font 12 | 238,51 / 278,51 | 484,109 / 553,109 | `[0x4d9ad0]`, `--:--:-` while laps done `[0x4d9ad4]`==0 |
| 47 / 48 | "POSITIE" / `%d/4` | t 38 font 10 / font 11 | 284,68 / 288,79 | 567,139 / 571,159 | `[0x4d9e60]+1` |
| 49 / 50 | "RONDE" / `%d/%d` | t 39 font 10 / font 11 | 290,91 / 288,102 | 580,189 / 571,209 | `[0x4d9ad4]+1` / min(`[0x4d9ad8]` NRLAPS, 9) |

Track type = `[0x4d8e9c + etappe*0x88]` (Raser.ini `[MAPS]` arg 0: 0 highway, 1 city), etappe `[0x4d9a74]`.

### 4.3 Speed panel `0x41a410` (D>=2)
| item | element | source | lo | hi | value |
|---|---|---|---|---|---|
| 52 | panel frame | anim 10/11, TL | 235,139 | 484,340 | |
| 53 | "TURBO" | t 53, font 10 | 289,152 | 582,368 | |
| 54 | turbo bar | anim 20/21 (11 frames), TL | 266,163 | 540,388 | frame = int(turbo*0.990099)*n/100, clamped 0..n-1; turbo `[0x4d9e3c]` 0..100 (`0x428050`) |
| 55 | "SNELHEID" | t 54, font 10 | 278,171 | 559,409 | |
| 56 | speed `%03d` | font 0 | 254,180 | 505,428 | abs(`[0x4d9e34]`) = car speed * 3.6 (km/h), set at `0x425ec3` |
| 7 | "SCHADE" | t 55, font 10 | 283,133 | 571,329 | |
| 3 | damage bar `0x41aa90` | anim 22/23 (10/11 frames), TL | 266,144 | 541,349 | frame = int(dmg / (100/(n-1))), clamped; dmg `[0x4d9e30]` 0..100 (`0x4281b0`). dmg>=80: blinks, hidden when (GetTickCount/200)&1 |
| 83 | damage `%2d@` | font 16 | (unused) | 580,349 | 16-bit mode only (same blink) |

### 4.4 Checkpoint / split time (D>=3 for highway, any D for city)
| item | element | function | lo | hi | value / logic |
|---|---|---|---|---|---|
| 73 | box | `0x41a5f0`, anim 1/0, TL | 117,6 | 235,15 | highway only |
| 74 | "CHECKPOINT" (hard-coded, not translated) | font 10, C | 160,6 | 320,12 | |
| 75 | time left `%02d:%02d:%d` | font 11, C | 160,12 | 320,24 | timer 11 (counts down; start = track timer, +extra time per time check) |
| 76 | "EXTRA TIJD" (hard-coded) | font 10, C | 160,40 | 320,80 | 2000 ms after `[0x4dcaa0]` set (time check `0x42a530`), visible when (t/100)&1 |
| 77 | split `+%02d.%d` / `-%02d.%d` | `0x41a780`, font 15, C | 160,10 | 320,20 | city only, no D check: `[0x4dca98]` = time minus previous time at this check (ms -> s%60, tenths), 2000 ms, (t/100)&1 blink |

### 4.5 Always (any D)
| item | element | function | lo | hi | logic |
|---|---|---|---|---|---|
| 29 | U-turn sign | `0x41a8a0`, anim 16/17, HS | 160,50 | 320,120 | when driving against the track direction (`0x410a10` returns 0: dot(next chunk - chunk, car forward) < 0), visible when (t/300)&1 |
| 76 | message | `0x41a8a0`, font 10, C | 160,40 | 320,80 | `[0x4882c0]` = text id (-1 none) is latched into `[0x4882c4]`; shown 2000 ms, visible when (t/100)&1 |

Messages posted into `[0x4882c0]`: t 31 NIEUW RONDE RECORD (`0x429860`), t 45 LAATSTE RONDE (`0x429860`), t 44 EXTRA TIJD
(`0x42a530`, also cheat), t 46 INVINCIBLE (cheat), t 47 COP MODE (`0x4168c0`), t 48 QUICK SERVICE (`0x42a4d0`, repairs),
t 49/50 EERSTE/LAATSTE WAARSCHUWING (cops, `0x4151f0`), t 51 RADAR BONUS (`0x428d30`, `[0x4dc964]++`; probably
hitting a speed camera "radar" [?]).

### 4.6 Pause / quit dialog `0x419300` (drawn while `[0x4d9ab4]` != 0)
| item | element | lo | hi | pause (`P`, mode 1) | quit (`Esc`, mode 2) |
|---|---|---|---|---|---|
| 34 | flag box anim 15/14, HS | 160,100 | 320,240 | | |
| 35 | title, font 10, C | 160,67 | 320,170 | t 24 PAUZE | t 27 EINDE |
| 36 | line 1, font 6, C | 160,86 | 320,220 | t 25 | t 28 |
| 37 | line 2, font 6, C | 160,100 | 320,246 | t 26 | t 29 (J/N) |

After the frame is flipped `0x4248d0` blocks: timers 8,9,10,0,3,11 and sound paused; pause waits for `P` release then
any key; quit: `J` or `Y` -> `0x424d20(4)` (race ends), `Esc`/`N` -> resume.

### 4.7 Result panels (after finishing) `0x419ad0`
Drawn by the finish event callback `0x4155d0` (state 3, game action 4) from 1500 ms after the finish until Esc/Enter/Space
(then it waits for their release); with hardware acceleration it also starts the 3D finish flag (`0x41abc0(1)`). Values: money `0x428140`, prize `0x4280c0`, radar bonus
`[0x4dc964]*2000`, new budget = sum. Font 15 for all numbers, t 17 GEFINISHT in font 14 (item 63: lo 29,32 / hi 68,78),
position badge item 64 (anim 2/3, frame = position, HS, lo 246,35 / hi 494,83).

| item | city `0x4194e0` | lo | hi | | item | highway `0x419870` | lo | hi |
|---|---|---|---|---|---|---|---|---|
| 62 | panel anim 7/6, TL | 23,20 | 49,46 | | 72 | panel anim 5/4, TL | 23,20 | 49,46 |
| 65 | lap 1 `[0x4dc970]` | 116,62 | 256,159 | | 82 | race time `[0x4d9e5c]` | 144,64 | 294,157 |
| 66 | lap 2 `[0x4dc974]` | 116,74 | 256,186 | | 78 | budget ` %d` | 184,93 | 388,207 |
| 67 | total `[0x4d9e5c]` | 116,97 | 256,237 | | 79 | prize `+%d` | 184,107 | 388,235 |
| 68 | budget ` %d` | 184,116 | 388,281 | | 80 | radar bonus `+%d` | 184,121 | 388,263 |
| 69 | prize `+%d` | 184,130 | 388,309 | | 81 | new budget ` %d` | 184,145 | 388,316 |
| 70 | radar bonus `+%d` | 184,144 | 388,337 | | | | | |
| 71 | new budget ` %d` | 184,168 | 388,390 | | | | | |

### 4.8 Radar (gfx_radr, D>=1)
Drawn inside the 3D scene by `0x41d360` (`render_game` `0x425090`) when no event runs, `[0x4d9aa4]` (texture copy
works, tested in `0x423d20` with LORADAR/HIRADAR), `disable_overlay`==0 and `overlay_detail`>0.
- Map: at init `0x41cda0` creates an 8-bit surface of road-bbox/60 pixels (`0x41cc90`) and draws the road into it
  (`0x41c9a0`); world (x,z) of a chunk -> map ((x-minx)/60, (maxz-z)/60) (`0x41c790`).
- Each frame `0x41d040`: copies a 64x64 (lo) / 128x128 (hi) window centred on the player's chunk (`[0x4d9e44]`) into the
  radar texture LORADAR.PCX / HIRADAR.PCX at (2,2) (`0x41cf90`, rect table `0x489040` = lo {15,146,79,210}, hi
  {30,361,158,489}), then blits dots (anim 43/44): racers `[0x485b2c]`,`[0x485b84]`,`[0x485bdc]` frame 0, cops (list
  `0x4a5ce0`, 20 slots) frame 2, player frame 1; then uploads the texture (`0x45bae0`).
- Screen quad `0x41c5a0`: top-left = rect.xy, size = 0.7 * 64 / 128 -> lo (15,146) 44.8x44.8, hi (30,361) 89.6x89.6,
  white, uv 0..1. `0x41d280` rotates the 4 vertices around the quad centre (`[0x4ac998]`,`[0x4ac9a0]`) by the camera
  heading every frame (map stays north-up in the texture, the quad turns). Drawn as a TL-vertex triangle strip.

### 4.9 Other overlay-like effects (not in draw_dashboard)
| What | Where | Notes |
|---|---|---|
| Loading progress bar | `0x41c1f0` init, `0x41c410` step, `0x41c4c0` draw | while loading: redraws the wait screen (`0x41bde0`: `[wait_lo]` pcx in 8-bit, `[wait_hi]` name with .tga in 16-bit) and blits the left pct% of item 14 (lo 246,177 / hi 489,425, width min 2 px). pct per step from Raser.ini `[progress] percent0..10`; with `calculate=1` the step times are measured and written back (`0x41c2b0`) |
| Start countdown "3","2","1","GO!" | gfx_3dft `0x417140` (spawn, from `0x4168c0`), `0x417870` (draw) | 3D letter objects (FONT3D_ANIM, charset at `0x486b68`, after the letter spacing 12.5f at `0x486b64`), records at `0x4869d8` {text[10], sound 0x2a/0x2b, effect serie 1/2} |
| Game-over texts | `0x416060` -> `0x415c50` (prepare) / `0x415e80` (sweep) | text rendered to an off-screen surface, its scanlines slide in alternately from the left (odd) and the right (even), position = sin of a quarter wave, centred on x; t 40 TOTAL LOSS / t 41 TIJD IS OM / t 42 GEARRESTEERD at y = H/4 (in 1.5 s), then t 43 RACE OVER at y = H/2 (in 3.0 s), then the first line slides back out; font 14. Pseudocode: [GAMEFLOW.md](GAMEFLOW.md) "Game over" |
| Finish flag | gfx_flag `0x41aea0` init, `0x41abc0` start, `0x41aee0` draw | 3D waving chequered flag (10 strips of 17x2 lit vertices, texture S_SFX.PCX, shading from sine table), world object; Game.ini `finish_flag` |
| Speed-camera flash | `0x4156d0`/`0x415710`/`0x415750` (game action registered by `0x42a2e0` when passing a radar trap) | 8-bit: all palette entries (235,235,235) for 2 frames then restore; 16-bit: screen filled with palette colour 15 once (per coordinator, see FORMAT_IMAGES) |
| Background | gfx_bkg `0x4190d0` | 16-bit only scrolling backdrop quads (u from car heading); not overlay |
| Tweak mode | `[0x4d9b24]` (Raser.ini TWEAK_MODE [?]) | F12: `frameRate: %f` at (10,80), `vchunks: %d` at (10,100) (`0x425090`); `L` held: current chunk `%d` at (0,100) font 11 (`0x41a8a0`) |

## 5. Frame order (`handle_game` `0x424a70`)
1. `render_game` `0x425090`: 3D scene, then (inside the scene) radar `0x41d360`, object lights, sparks, background,
   skid marks, clouds, particles, finish flag, 3D font.
2. `check_game_keys` `0x424ee0`, `handle_game_actions` `0x4168c0` (event callbacks: result panels, game-over texts).
3. `draw_2d_display` `0x41bdd0` -> `draw_dashboard` `0x41a8a0` unless Game.ini `disable_overlay`. Order: U-turn sign;
   highway/city info panel; speed panel (+damage bar); checkpoint (highway) or split time (city); message; pause/quit
   dialog; tweak chunk number. Everything except the dialog/tweak is skipped while an event runs (`[0x4d9aa0]`, set by
   `0x424d20` for finish/game over/arrest).
4. Flip; then the blocking pause/quit loop `0x4248d0` if `[0x4d9ab4]`.

## 6. Keys (`0x424ee0`, Win32 virtual keys via GetAsyncKeyState `0x466910`, not DirectInput)
| Key | Action |
|---|---|
| `D` (0x44) | overlay_detail = (detail+1) mod 4, waits for release. 0: only messages/U-turn/split/dialogs; 1: + radar; 2: + speed panel; 3: + info panel and checkpoint box (Game.ini default 3) |
| `P` (0x50) | pause (`[0x4d9ab4]`=1) unless game state 3 or 6 |
| `Esc` | quit dialog (`[0x4d9ab4]`=2) |
| `T` (0x54) | sound 0x29 (horn [?]) |
| Ctrl+Space+T | highway only: message t 44, timer 11 += 180000 ms (cheat) |
| Ctrl+Space+I | message t 46 INVINCIBLE, `[0x4d9eb8]`=1 (cheat) |
| `M` (tweak) | cycle camera target over racers/player |
| F12 (tweak) | frame rate / vchunks text |

Game.ini `[GFX OPTIONS]`: `disable_overlay` (no 2D overlay and no radar; `D` has no visible effect), `overlay_detail`
0..3, `finish_flag`. Game.ini `texturecopy` and the LORADAR/HIRADAR copy test gate the radar.

## Open questions
- Exact meaning of mode 640x480x8 (lo layout +320 px) - bug or intended.
- `@` glyph in lo_fnt1 for the 16-bit damage percentage; `[0x4a0268]` lo-res glyph flag.
- What exactly triggers RADAR BONUS (`0x428d30`).
- Language texts 0-16, 18-23, 30 (GEFLITST, BOETE, GETANKT, POLITIE, ...) are not referenced by spel.dat's HUD code.
