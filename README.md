# A2RacerIIRE

A reimplementation of the engine of *A2 Racer II* (Davilex Games, 1998), rebuilt by reverse engineering the original.
It runs the original game data from **your own CD**: no game files are included in this repository or its releases.

Race over the Dutch motorways and through Amsterdam, Den Haag and Rotterdam, natively on Windows, Linux, the Steam Deck,
Android and (homebrew) Nintendo Switch, at any resolution and frame rate.

> Unofficial fan project for preservation. Not affiliated with or endorsed by Davilex or any other rights holder.
> A2 Racer and related names are trademarks of their owners. You need your own copy of the original game to play.

## Status
Playable from start to end, still being polished. What runs today:
- the whole career: intro, the menu program (nine save slots, garage with upgrades and repairs, showroom, controls,
  options, standings, help), the seven *etappes* (city races and highway qualifications), prize money and results
- all tracks with their scenery, traffic lights, trams, boats, the helicopter and the aircraft
- the player's car with the original's handling (both steering models, ported 1:1), the four cameras, the in-race
  overlay in the original's fonts, car shadows, brake lights, skid marks, tyre smoke and sparks, the optional fog
  (Game.ini `fog`) and the rain the CD leaves switched off (Game.ini `particles=1`)
- traffic, the three opponents and the police (speed checks, pursuit, arrest), collisions and damage
- the race rules: start sequence, laps and records, checkpoints and the highway time limit, speed cameras, quick
  service, game over, pause and quit
- the Smacker films, sound effects and engine sounds, the first-start copy from the CD or an ISO image
- the original's three display modes: the 3D-card mode and the 8-bit 640 x 480 and 320 x 200 software looks (below)

Linux and Android builds come from CI and have not been tried on a device yet.
See [docs/](docs) for everything known about the original.

## Playing
1. Get `A2RacerIIRE.exe` from the [Releases](../../releases) page, or build it yourself (below). The exe is not
   signed, so Windows SmartScreen may warn at the first start: *More info*, then *Run anyway*.
2. Start it. At the first start it looks for the A2 Racer II CD in every drive (a mounted ISO image works too); without
   a CD it asks for an ISO image or a folder with a copy of the CD. It copies the CD's `data` folder (about 115 MB) and
   checks every file against the Dutch version 1.01 CD (`A2RacerIIRE.exe --verify` repeats the check).

**Linux and Steam Deck:** download the `linux-x86_64.tar.gz` from the Releases page (or run `./build.sh`), unpack it and
start `a2racerre`. It needs SDL2 (`libsdl2-2.0-0`; SteamOS has it). The CD is looked for under `/media`, `/run/media` and
`/mnt`; an ISO image can be chosen in a file dialog (zenity or kdialog) or given on the command line:
`./a2racerre ~/A2Racer-II.iso`. The files go to `~/.local/share/A2RacerRE`. On the Steam Deck add `a2racerre` to Steam as
a non-Steam game.

**Android** (7.0 or later, 64-bit, OpenGL ES 2.0): install the `.apk` from the Releases page. At the first start choose
an ISO image of the CD, or a folder with a copy of it, in Android's file picker. You can also copy the CD's `data` folder
to `Android/data/io.github.jjmhalew.a2racerre/files/data` with a USB cable. The menus are played by tapping; the races
have touch controls (below), or plug in or pair a controller. The back button pauses the race and asks to quit.

**Nintendo Switch** (experimental; needs a Switch that runs homebrew, i.e. custom firmware such as Atmosphère): put
`a2racerre.nro` from the Releases page (the `-switch.zip` unpacks onto the SD card as it is) or from `./build_switch.sh`
into `/switch/a2racerre/` on the SD card, and an ISO image of the CD into the same folder. The first start unpacks the
ISO into `/switch/a2racerre/data` (once; the ISO can be deleted then). Or copy the CD's `data` folder to
`/switch/a2racerre/data` yourself. Start it from the Homebrew Menu (preferably by holding R while opening a game, which
gives homebrew the full memory). `Game.ini`, the saves and `mods/` live in `/switch/a2racerre`. The Joy-Cons (attached,
or one in each hand) and a Pro Controller work as any controller does (below), with their own button names in the
menu; the name of a new player is entered with the pad. In handheld mode the touch screen has the touch controls.

### Touch controls
On a phone, tablet or Windows touch screen the race shows on-screen controls from the first touch on, and hides them
again when a key or controller is used:
- bottom left: steer left / right (they steer as the arrow keys do), with *look back* above them;
- bottom right: gas (green) and brake / reverse (red), with turbo and the handbrake above them. On a screen too narrow
  for them beside the original's speed panel they move left of it;
- top left: pause and camera.

Several fingers work at once (steer and accelerate), and a finger can slide from left to right or from gas to brake.
The pause button asks whether to quit: tap the tick to quit or the cross to drive on (also the pause button again, or
the back button). The finish results and the game over screen go on with a tap anywhere.

The settings are in the `[touch]` section of `controller.ini` (the file and its section are written at the first
start): `show` = `auto` / `on` / `off`, `size` and `opacity` in percent, `steer=drag` for analogue steering instead of
the buttons (put a thumb down anywhere on the lower left and slide it left or right: the further, the sharper), and
`tilt=1` to steer by tilting the device like a wheel (`tilt=-1` reverses it, `tilt_angle` is the tilt for full lock;
the steering buttons are not shown then).

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

### Game options
The menu's options screen (*Opties*) has the original's sound, difficulty and *GRAPHICS* settings. *3D Kaart* is the
3D-card mode (640x480 in 16 bit, drawn at your screen's resolution and shape). *640 x 480* and *320 x 200* are the
original's 8-bit software modes for PCs without a 3D card, and look like them: the race is drawn at that resolution
and scaled up with big square-edged pixels into a 4:3 picture, with the 8-bit textures and palette, the scrolling
painted sky, the small overlay (in 640 x 480 the original puts it in the upper right corner) and without fog, tyre
smoke, brake-light glows, the waving finish flag and transparency.

The original's menu starts with *320 x 200* selected; this port starts in *3D Kaart* until you choose a mode on the
options screen and press *OK* (it then notes `RESOLUTION_CHOSEN=1` in `Raser.ini`). The race itself reads
`Raser.ini [Program] RESOLUTION` (0 = 320 x 200, 1 = 640 x 480, 3 = 3D Kaart), which the menu writes at every start.
Texture packs (below) also apply in the 8-bit modes.

**Extras of the port** (off by default, so the picture stays the original's): `graphics.ini` in the user folder, written
with comments at the first race, has `msaa=` (edge smoothing: 0, 2, 4 or 8 samples) and `ao=1` (ambient occlusion,
GTAO: shading in the corners and at contact points, under the guardrails and the cars, at the foot of buildings and
bridges). They apply to *3D Kaart* from the next race on and need OpenGL 3 or OpenGL ES 3 (nearly every PC, most
phones from about 2015 on, the Switch); without it they stay off.

### Files
Everything the game writes lives in the user folder (`%APPDATA%\A2RacerRE` on Windows, `~/.local/share/A2RacerRE` on
Linux), or next to `A2RacerIIRE.exe` when a `data` folder is there (portable). The original's own settings files are kept
there in their original formats, so they can be edited the same way: `Game.ini` and `Raser.ini` (commented), `SLOTS.RAS`
(the nine save slots), `options.ras`. The port adds `controller.ini` (the controller mapping and the touch controls,
see above) and `graphics.ini` (the extras above).

### Texture packs and mods
A `mods` folder in the user folder (or next to the program) replaces the game's files without touching them:
- `mods/textures/NAME.png` replaces texture `NAME` (also `.tga`, `.pcx`, `.jpg`, `.bmp`), at any resolution: the
  game's textures are 64 to 256 pixels, so 4x upscales are a good start. Transparency comes from the PNG's alpha, or
  from the original's colour key pink (255, 0, 127).
- `mods/data/NAME` replaces any other game file by name: sounds (`.wav`), the menu's images (`.pcx`; not the overlay's
  sprites, which have no names),
  films (`.smk`), track and object files.

`A2RacerIIRE --dump-textures FOLDER` writes all 147 textures of the game as PNG with their names, the starting point of a
pack. In *3D Kaart* textures are drawn with mipmaps and 8x anisotropic filtering where the graphics card has it.

## Building from source
Windows 10 or 11; nothing needs to be installed first:
```bat
build.bat
```
builds `A2RacerIIRE.exe`. The C compiler is [Zig](https://ziglang.org): an existing `zig` or `pip install ziglang` is
used when present, otherwise `build.bat` downloads Zig 0.16.0 into `tools\zig` once and checks its SHA-256. SDL 2.32.10
(zlib license) is downloaded into `tools\sdl` the same way and linked statically, so the exe is a single file.
`build.bat dev` builds the developer build `out\a2racer.exe` with a console.

`make_standalone.bat` packs `A2RacerIIRE.exe` and **your** game files (the copy A2RacerIIRE.exe made at its first start)
into a single `A2RacerIIRE-standalone.exe` that unpacks itself to `%LOCALAPPDATA%\A2RacerRE` at its first start and runs
from there; settings and saves stay in `%APPDATA%\A2RacerRE`, shared with A2RacerIIRE.exe. It contains the game's files, so
it is for your own use only: never share or upload it.

- Linux: `./build.sh` builds `a2racerre` with the system's C compiler and SDL2 (Debian / Ubuntu:
  `sudo apt install build-essential libsdl2-dev`). `sh tests/run.sh` runs the tests that need no game data.
- Android: `cd android` then `gradlew assembleRelease` builds the APK in `android/app/build/outputs/apk/`. It needs the
  Android SDK with NDK 27.2 and CMake 3.22 (Android Studio's SDK Manager installs both). The build downloads SDL 2.32.10
  and checks its SHA-256. Without your own signing key (`-Pa2Keystore=... -Pa2KeyAlias=... -Pa2KeyPassword=...`) the
  release APK is signed with the debug key.
- Nintendo Switch: `./build_switch.sh` builds `a2racerre.nro` with devkitPro's devkitA64, libnx, switch-sdl2 and
  switch-mesa. Without `$DEVKITPRO` set it builds in the official `devkitpro/devkita64` Docker image instead. The same
  SDL code as on Linux and Android, on OpenGL ES 2.0; the `__SWITCH__` parts are the folder on the SD card, the ISO
  unpacked from there, the system's error dialog for messages and the Nintendo button names.

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
`tools/imgconv.py` (all images, fonts and sprites to PNG), `tools/physsim.py`, `tools/mkmanifest.py`,
`tools/coverage.py` (which functions of the original no doc names yet: [docs/COVERAGE.md](docs/COVERAGE.md)).

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
`A2_TEST_FOG=1` makes Game.ini `fog=1`/`2` apply in every race instead of a random half (with `--menu-userdir`),
`A2_TEST_OBJ=N[,C]` starts the player car C chunks (default 3) before `.omp` object N, in line with it (to drive into
a pylon, speed-camera pole or median with `--drive 0`; a negative C starts after it, e.g. `A2_TEST_OBJ=30,-1
A2_TEST_CAM=6 --race 5 --seconds 7` looks back at the parked police car of etappe 5),
`A2_NO_FILMS=1` skips the films, `A2_TEST_PAD="T:EL=V ...; T:..."` attaches a virtual game controller and sets its
elements T seconds after the start (e.g. `"0:RT=1; 5:LEFTX=-0.5; 7:START=1; 7.2:START=0"`, also `T:unplug` / `T:plug`;
`A2_TEST_PAD_NAME` names it, e.g. `"DualSense Wireless Controller"`), `A2_TEST_JOY` the same for a raw wheel-like
joystick (`axis0=-1..1 button3=1 hat0=down`); rumble reaching them is printed (needs SDL 2.24 or later),
`A2_TEST_TOUCH="T:CMD; ..."` puts fingers on the screen T seconds after the start: `down ID X Y`, `move ID X Y`
(X, Y 0..1 of the window), `up ID`, `tap ID X Y`, `accel X Y Z` (the accelerometer's reading in m/s^2) and `key` (a key
press, which hides the touch controls), e.g. `"0.5:down 1 0.93 0.85; 7:down 2 0.2 0.85; 7.5:up 2"`;
`A2_TEST_WINDOW=WxH` sets the window size (e.g. `1600x720`, a phone's 20:9), `A2_TEST_MENU="EVENTS"` feeds every menu the events of `--menu-script` (with
`A2_TEST_FINISH` this plays a whole career unattended), `A2_TRACE=1` prints the car and AI state,
`A2_TEST_BRAKE=1` keeps the player car's brake lights lit (the texture copies of docs/RENDER.md 11),
`A2_MSAA=N` and `A2_AO=1` override graphics.ini, `A2_TEST_GLRESET=T` throws the OpenGL context away T seconds after the start, as Android can do while the app is in the
background (the textures, buffers and shader are then made again from the copies the renderer keeps on Android).
