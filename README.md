# A2RacerRE

A reimplementation of the engine of *A2 Racer II* (Davilex Games, 1998), rebuilt by reverse engineering the original.
It runs the original game data from **your own CD**: no game files are included in this repository or its releases.

Race over the Dutch motorways and through Amsterdam, Den Haag and Rotterdam, natively on Windows, Linux, the Steam Deck
and Android, at any resolution and frame rate.

> Unofficial fan project for preservation. Not affiliated with or endorsed by Davilex or any other rights holder.
> A2 Racer and related names are trademarks of their owners. You need your own copy of the original game to play.

## Status
Playable from start to end, still being polished. What runs today:
- the whole career: intro, the menu program (nine save slots, garage with upgrades and repairs, showroom, controls,
  options, standings, help), the seven *etappes* (city races and highway qualifications), prize money and results
- all tracks with their scenery, traffic lights, trams, boats, the helicopter and the aircraft
- the player's car with the original's handling (both steering models, ported 1:1), the four cameras, the in-race
  overlay in the original's fonts, skid marks, tyre smoke and sparks
- traffic, the three opponents and the police (speed checks, pursuit, arrest), collisions and damage
- the race rules: start sequence, laps and records, checkpoints and the highway time limit, speed cameras, quick
  service, game over, pause and quit
- the Smacker films, sound effects and engine sounds, the first-start copy from the CD or an ISO image

Linux and Android builds come from CI and have not been tried on a device yet.
See [docs/](docs) for everything known about the original.

## Playing
1. Get `A2RacerRE.exe` from the [Releases](../../releases) page, or build it yourself (below). The exe is not
   signed, so Windows SmartScreen may warn at the first start: *More info*, then *Run anyway*.
2. Start it. At the first start it looks for the A2 Racer II CD in every drive (a mounted ISO image works too); without
   a CD it asks for an ISO image or a folder with a copy of the CD. It copies the CD's `data` folder (about 115 MB) and
   checks every file against the Dutch version 1.01 CD (`A2RacerRE.exe --verify` repeats the check).

**Linux and Steam Deck:** download the `linux-x86_64.tar.gz` from the Releases page (or run `./build.sh`), unpack it and
start `a2racerre`. It needs SDL2 (`libsdl2-2.0-0`; SteamOS has it). The CD is looked for under `/media`, `/run/media` and
`/mnt`; an ISO image can be chosen in a file dialog (zenity or kdialog) or given on the command line:
`./a2racerre ~/A2Racer-II.iso`. The files go to `~/.local/share/A2RacerRE`. On the Steam Deck add `a2racerre` to Steam as
a non-Steam game.

**Android** (7.0 or later, 64-bit, OpenGL ES 2.0): install the `.apk` from the Releases page. At the first start choose
an ISO image of the CD, or a folder with a copy of it, in Android's file picker. You can also copy the CD's `data` folder
to `Android/data/io.github.jjmhalew.a2racerre/files/data` with a USB cable.

### Controls
| Action | Keyboard (as the original, changeable in the menu) | Xbox / Steam Deck controller | PlayStation controller |
|---|---|---|---|
| Steer | Left / Right | left stick or D-pad | left stick or D-pad |
| Gas | Up | RT or A | R2 or Cross |
| Brake / reverse | Down | LT or X | L2 or Square |
| Turbo | Ctrl | Y or RB | Triangle or R1 |
| Handbrake | Alt | B | Circle |
| Cameras: chase, cockpit, far chase | 1, 2, 4 | View / Back cycles | Share / Create cycles |
| Look back (held) | 3 | LB | L1 |
| Overlay detail | D | | |
| Horn | T | LS (click) | L3 |
| Pause / quit question | P / Esc | Start / Menu (then A = quit, B = go on) | Options (then Cross = quit, Circle = go on) |
| Fullscreen / window | Alt+Enter | | |

The controller column is the default mapping; a Switch Pro controller uses the letters printed on it (A = gas is the
right face button there).

### Controllers
Every controller SDL knows works, several at once (their input is merged): Xbox and other XInput pads, DualShock 4
and DualSense (with rumble, lightbar and player LEDs), Switch Pro, the Steam Deck (with Steam Input's gamepad template,
the default for non-Steam games) and the many pads in SDL's built-in database. Pads SDL does not know can be added with
a `gamecontrollerdb.txt` ([SDL's format](https://github.com/mdqinc/SDL_GameControllerDB)) in the user folder or next
to the program. Wheels, pedals and flight sticks that are not game controllers work as plain joysticks with numbered
axes and buttons. Controllers can be plugged in and out at any time. The pad rumbles on collisions (stronger with the
impulse) and lightly with the turbo; a force-feedback wheel gets a centring spring during the race.

All menus work with a pad: D-pad or left stick to move, A (Cross) to choose, B (Circle) to go back. The player name:
up / down turn the letter under the cursor, left / right move the cursor, X (Square) deletes, A or Start accepts.

The mapping is in `controller.ini` in the user folder, written with comments and the defaults above at the first
start. In the menu's controls screen (*Besturing*) choose the joypad or the wheel / joystick device, then
*Instellen*: press a button or move a stick, trigger, pedal or wheel for each of the six driving functions in turn
(Start or Esc breaks off), and *OK* saves it. Look back, camera, horn and pause, the dead zones, the steering curve
(`steer_linearity`, `steer_sensitivity`), rumble and the wheel spring are edited in the file. With the wheel /
joystick device the screen's *Gevoeligheid* slider shapes the analogue steering, as in the original. Buttons work like
the keys (gas and steering build up); sticks, triggers, wheels and pedals are analogue.

### Files
Everything the game writes lives in the user folder (`%APPDATA%\A2RacerRE` on Windows, `~/.local/share/A2RacerRE` on
Linux), or next to `A2RacerRE.exe` when a `data` folder is there (portable). The original's own settings files are kept
there in their original formats, so they can be edited the same way: `Game.ini` and `Raser.ini` (commented), `SLOTS.RAS`
(the nine save slots), `options.ras`. The port adds `controller.ini` (the controller mapping, see above).

### Texture packs and mods
A `mods` folder in the user folder (or next to the program) replaces the game's files without touching them:
- `mods/textures/NAME.png` replaces texture `NAME` (also `.tga`, `.pcx`, `.jpg`, `.bmp`), at any resolution: the
  game's textures are 64 to 256 pixels, so 4x upscales are a good start. Transparency comes from the PNG's alpha, or
  from the original's colour key pink (255, 0, 127).
- `mods/data/NAME` replaces any other game file by name: sounds (`.wav`), the menu's images (`.pcx`; not the overlay's
  sprites, which have no names),
  films (`.smk`), track and object files.

`A2RacerRE --dump-textures FOLDER` writes all 147 textures of the game as PNG with their names, the starting point of a
pack. Textures are drawn with mipmaps and 8x anisotropic filtering where the graphics card has it.

## Building from source
Windows 10 or 11; nothing needs to be installed first:
```bat
build.bat
```
builds `A2RacerRE.exe`. The C compiler is [Zig](https://ziglang.org): an existing `zig` or `pip install ziglang` is
used when present, otherwise `build.bat` downloads Zig 0.16.0 into `tools\zig` once and checks its SHA-256. SDL 2.32.10
(zlib license) is downloaded into `tools\sdl` the same way and linked statically, so the exe is a single file.
`build.bat dev` builds the developer build `out\a2racer.exe` with a console.

- Linux: `./build.sh` builds `a2racerre` with the system's C compiler and SDL2 (Debian / Ubuntu:
  `sudo apt install build-essential libsdl2-dev`). `sh tests/run.sh` runs the tests that need no game data.
- Android: `cd android` then `gradlew assembleRelease` builds the APK in `android/app/build/outputs/apk/`. It needs the
  Android SDK with NDK 27.2 and CMake 3.22 (Android Studio's SDK Manager installs both). The build downloads SDL 2.32.10
  and checks its SHA-256. Without your own signing key (`-Pa2Keystore=... -Pa2KeyAlias=... -Pa2KeyPassword=...`) the
  release APK is signed with the debug key.

The engine is plain C99 on SDL2 and OpenGL 2.1 / OpenGL ES 2.0 with one code path for every platform.

## License
The code in this repository is licensed under the [GNU General Public License v3.0](LICENSE) or later. This does not
cover the original game, its data or its trademarks, which belong to their owners and are not part of this project.
`src/stb/` holds Sean Barrett's stb_image_write (public domain or MIT). `android/app/src/main/java/org/libsdl/` is
SDL's Android code (SDL 2.32.10, zlib license).

# Development and reverse engineering
Developers keep the CD's contents in `extract/` (ignored by git, as are `data/` and the scratch folder `out/`).
The original consists of three programs: the launcher `a2_II.exe`, the menu `menu.dat` and the race `spel.dat` (both
renamed Win32 executables, MSVC 5.0 C++ without symbols but full of assert strings naming their source files). Its
internals are documented in [docs/](docs), starting with [docs/ANALYSE.md](docs/ANALYSE.md):

| Doc | What |
|---|---|
| [FORMAT_IMG.md](docs/ANALYSE.md) | the `.IND`/`.IMG` archives |
| [FORMAT_ROAD.md](docs/FORMAT_ROAD.md) | tracks: `.rmp` road mesh, `.pmp` chunks, `.cmp` guard rails, `.dmp` waypoints |
| [FORMAT_OBJECTS.md](docs/FORMAT_OBJECTS.md) | `.omp` objects, `.pth` paths (trams, boats, helicopter), CARS.DAT |
| [FORMAT_D3D.md](docs/FORMAT_D3D.md) | `.d3d` meshes and how they get their textures |
| [FORMAT_IMAGES.md](docs/FORMAT_IMAGES.md) | PCX/TGA, palettes, `.fnt`/`.img` fonts, RC/MENURC sprite banks |
| [RENDER.md](docs/RENDER.md), [HUD.md](docs/HUD.md) | the 3D pipeline, cameras, sky, effects; the overlay |
| [PHYSICS.md](docs/PHYSICS.md), [INPUT.md](docs/INPUT.md) | car physics (both steering models), collisions, input |
| [AI.md](docs/AI.md) | traffic, opponents, police |
| [GAMEFLOW.md](docs/GAMEFLOW.md), [MENU.md](docs/MENU.md) | race rules, career, the menu program, save games |
| [SMACKER.md](docs/SMACKER.md) | the Smacker film decoder (written from the format description) |

Analysis tools (Python: `pip install pefile capstone pillow numpy ziglang`):
`tools/disasm.py` (annotated disassembly per function, cross references, string search; function names from
`docs/names/<exe>_<area>.txt`), `tools/srcmap.py` (functions per original source file), `tools/imgpack.py` (archives),
`tools/roadparse.py`, `tools/objparse.py`, `tools/d3dparse.py` (format parsers with self-tests and OBJ export),
`tools/imgconv.py` (all images, fonts and sprites to PNG), `tools/physsim.py`, `tools/mkmanifest.py`.

Developer switches of `out\a2racer.exe`:
```bash
./out/a2racer.exe --race 1 --car 4                  # race on etappe 1 with car 4 (Porsche)
./out/a2racer.exe --race 1 --drive 0 --seconds 8 --shot out/s.png   # full gas, straight, screenshot after 8 s
./out/a2racer.exe --track 0 --chunk 300             # fly along the track
./out/a2racer.exe --film intro.smk                  # play a film
./out/a2racer.exe --image hoofd.PCX --archive Mrcs  # show an image
./out/a2racer.exe --menu                            # the menu program alone (--menu-first: as after the intro)
./out/a2racer.exe --menu-shot garage out/g.png      # one menu screen as a PNG
./out/a2racer.exe --menu-userdir out/t --menu-script "w20 enter"   # scripted menu input, own settings folder
```
Environment: `A2_HIDDEN=1` opens no visible window (screenshots still work; with `SDL_AUDIODRIVER=dummy` also no sound),
`A2_TEST_FINISH=N` finishes a race N seconds after the start (and continues 3 s later), `A2_TEST_GAMEOVER=N[,R]` ends
it N seconds after the start with game over reason R (0 total loss, 1 time up, 2 arrested; continues 8 s later),
`A2_TEST_CAM=N` holds camera view N (0..10, the original's player views, e.g. 6 front-left, 8 low rear-left),
`A2_NO_FILMS=1` skips the films, `A2_TEST_PAD="T:EL=V ...; T:..."` attaches a virtual game controller and sets its
elements T seconds after the start (e.g. `"0:RT=1; 5:LEFTX=-0.5; 7:START=1; 7.2:START=0"`, also `T:unplug` / `T:plug`;
`A2_TEST_PAD_NAME` names it, e.g. `"DualSense Wireless Controller"`), `A2_TEST_JOY` the same for a raw wheel-like
joystick (`axis0=-1..1 button3=1 hat0=down`); rumble reaching them is printed (needs SDL 2.24 or later), `A2_TEST_MENU="EVENTS"` feeds every menu the events of `--menu-script` (with
`A2_TEST_FINISH` this plays a whole career unattended), `A2_TRACE=1` prints the car and AI state.
