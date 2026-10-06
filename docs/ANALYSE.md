# A2 Racer II - overview of the original

*A2 Racer II* (Davilex Games, NL, November 1998; version 1.01 of 03-11-1998 on the CD). The Dutch sequel to *A2 Racer*
(sold in Germany as *Autobahn Raser*): races over Dutch highways and through the cities Amsterdam, Den Haag and
Rotterdam, with traffic, police, speed cameras ("flitspalen"), refuelling and a garage between the races.

## The CD
| Path | What |
|---|---|
| `setup.exe`, `service\setup.exe` | InstallShield installer, settings program |
| `data\a2_II.exe` | Launcher (28 KB): starts the menu and the race in turn |
| `data\menu.dat` | The menus (PE exe, 253 KB, 1998-11-02): main menu, new player, garage, buy car, options, controls, high scores, save slots, Smacker films (`smackw32.dll`) |
| `data\spel.dat` | The race (PE exe, 669 KB, 1998-11-05): DirectDraw + Direct3D (immediate mode, also RAMP/RGB software), DirectInput, DirectSound |
| `data\qrace.exe`, `data\instlng.exe` | Quick-race starter, language installer |
| `data\Game.ini`, `Raser.ini`, `garage.ini`, `language.ini`, `menu.ini` | Commented settings; the menu and the race exchange the player's state (car, money, damage, upgrades, results) through `Game.ini` |
| `data\RCS.IMG/IND` | Race resources: 448 `.d3d` meshes, 106 `.tga` + 147 `.pcx` images, 74 `.wav`, 31 `.fnt` |
| `data\Mrcs.img/ind` | Menu resources: menu screens (`.pcx`), fonts (`.img`), music and spoken help (`.wav`) |
| `data\RC.IMG/IND`, `MENURC.IMG/IND` | Unnamed resource banks (453 and 482 entries) |
| `data\etappeN.rmp/.dmp/.cmp/.pmp/.omp/.pth` | The 7 tracks ("etappes"), see below |
| `data\CARS.DAT` | Car table (1480 bytes) |
| `data\*.smk` | Smacker films: intro, win, lose, exit |

## Archives (`.IND` + `.IMG`) -> [FORMAT_IMG.md](FORMAT_IMG.md), `tools/imgpack.py`
`u16 count`, then either `count x {char name[20]; u32 offset}` (RCS, Mrcs) or `count x u32 offset` (RC, MENURC).

## Tracks
`Raser.ini [MAPS]`: 0 Amsterdam (city), 1 Amsterdam - Den Haag (highway), 2 Den Haag (city), 3 Den Haag - Rotterdam,
4 Rotterdam (city), 5 Rotterdam - Amsterdam, 6 Amsterdam (city, second layout). Entries 7 (Munchen - Berlin) and 8
(Berlin) have no files on the CD. A track is a chain of *chunks* (818 in etappe1); `Game.ini visiblechunks=30`.
Loaded by `0x444bf0` (rd_main.cpp): `.cmp`, `.dmp` (draw map), `.pmp`, `.rmp` (road), `.omp` (objects). `.pth` is
loaded separately (`etappe%d.pth`, pth_main.cpp).

## The code
Both programs are MSVC 5.0 C++ builds without symbols, but full of debug and assert strings with the original source
file names. `python tools/srcmap.py` maps functions to those files ([SOURCES_raw.txt](SOURCES_raw.txt)).

spel.dat, by address:
- `0x401000-0x435000` the game (`C:\work\A2racII\Game\`): `ai_*` (traffic, racers, cops, ADAC breakdown service, lanes,
  paths), `car_*` (cars, wheels, cops), `ev_*` (events: cops, game over), `gfx_*` (3dfx, ASCII, background, flag,
  lights, progress bar, radar, skid marks, sky, sparks, clouds), `gm_*` (body, clock, ini, init, main), `plr_*`
  (player car), `snd_main`, `tr_*` (finish, time checks, signs), `txt_*`, `main.cpp`, `menuanim`, `lan_main`,
  `prt_main` (particles), `pth_*` (paths, helicopter)
- `0x435000-0x464000` the 3D library `win3ddp` / `win3dlib`: DirectInput (`di_*`), `obj_*` (objects, damage,
  physics), `tmg_main`, `rd_*` (road: chunks, draw map, rails), `cam_main`, `bbox`, `d3dlib`, `light`, `material`,
  `math3d`, `mesh`, `object3d`, `text`, `viewport`, `d3dev*`, `txformat`, `tga`
- `0x464000-0x471000` the 2D/system library `winlib`: initlib, surface, font, sound, pformat, clipblit, pal, ascii,
  inifile, rcpcx, sprite, hwinfo, fileio, filepath, mouse, msg, mixer, bmp, forms
- `0x470000-` MSVC C runtime

## Tools
`tools/disasm.py` (annotated listing per function, xrefs, string search; names from `docs/names/<exe>_<area>.txt`),
`tools/exe_strings.py`, `tools/pe_info.py`, `tools/imgpack.py`, `tools/srcmap.py`.
