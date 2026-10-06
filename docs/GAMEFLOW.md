# Game flow, career and race rules

Names: [names/a2_flow.txt](names/a2_flow.txt), [names/menu_flow.txt](names/menu_flow.txt),
[names/spel_flow.txt](names/spel_flow.txt). Menus in detail: [MENU.md](MENU.md); sound: [SOUND.md](SOUND.md).

## 1. Processes

```
a2_II.exe (launcher, hidden window class "MOTOR")
  arg = "\FIRST"
  loop:
    code = spawnv(P_WAIT, "menu.dat", {"menu.dat", arg})      ; arg is "" after the first run
    if code != 40: close window, end                          ; 0xFF = quit
    spawnv(P_WAIT, "spel.dat", {"spel.dat", ""})              ; race exit code is ignored
    arg = ""
```
`launcher_winmain` 0x401120, `run_program` 0x401000 (`_spawnv` mode 0 = wait; on failure a MessageBox with the errno
text). The launcher pumps messages between runs; the programs talk only through **INI files** and the menu's exit code:

| Exit code of menu.dat | Meaning |
|---|---|
| 0x28 (40) | start a race: the menu has written Game.ini / Raser.ini / menu.ini |
| 0xFF | quit (after exit.smk) |

`\FIRST` (or `/FIRST`) makes the menu play intro.smk and open the player/slot screen first; without it the menu
continues with the slot in `menu.ini [PLAYER] save_game` and reads the race result from Game.ini.

**qrace.exe** (quick race, separate starter, not used by the launcher): a dialog (class "3DS2N64") with three combo
boxes - display device ("Automatisch" + DirectDraw devices, not saved), track ("Amsterdam start", "Amsterdam - Den
Haag", "Den Haag", "Den Haag - Rotterdam", "Rotterdam", "Rotterdam - Amsterdam", "Amsterdam finish" = etappe 0-6) and
car (Trabant, Astra, Kever, ?, Porsche, Mercedes, Lamborgini = 0-6). OK / Enter writes `game.ini` (in the exe directory):
`[ETAPPE] etappe`, `[PLAYER] car`, `damage=0`, `copland=0`, `motor=banden=abs=turbo=versnellingsbak=1`, then
`CreateProcess("spel.dat", " ", HIGH_PRIORITY_CLASS|CREATE_NEW_PROCESS_GROUP)` ("Failed to launch A2Racer 2" on error).
Esc closes. Functions: 0x401310 write ini + 0x4015a0 launch, 0x4012d0 read current etappe/car, 0x401210 fill lists.

## 2. INI exchange

### Written by the menu before a race (`ini_write_race_setup` 0x401cc0)
| File | Section / key | Value |
|---|---|---|
| Raser.ini | [Program] RESOLUTION | options: 0 = 320x200x8, 1 = 640x480x8, 3 = 640x480x16 (3D card) |
| Raser.ini | [Sound] SFXVOLUME, MUSICVOLUME | `on ? 3100 - 30*vol : 0` (vol 1..100) |
| Game.ini | [OPTIONS] Difficulty | 0 easy / 1 hard |
| Game.ini | [PLAYER] MOTOR, BANDEN, DAMAGE, TURBO, ABS, VERSNELLINGSBAK | slot items (DAMAGE = damage %) |
| Game.ini | [PLAYER] Car | 0..6 |
| Game.ini | [PLAYER] COPLAND | 1 if the player name is "COPLAND" |
| Game.ini | [INPUT] LEFT, RIGHT, UP, DOWN, EXTRA1, EXTRA2 | VK codes |
| Game.ini | [INPUT] Device, Sensitivity | device 0..3 -> 1,2,3,3; sensitivity 0..100 |
| Game.ini | [PLAYER] money | budget |
| Game.ini | [ETAPPE] etappe | slot etappe (0..6) |
| menu.ini | [PLAYER] save_game | active slot |

### Read by the menu after a race (only if `[PLAYER] finished` != 0)
`read_race_results` 0x402390:
- `[RACERS] COUNT` must be 3; for n = 0..2 `n = car,position,time_ms` -> standings table `etappe+1`, place `position`
  = {car, "racer n+1", time_ms/1000}.
- `[PLAYER] money` -> slot money (clamped 0..999999); `tracktime`, `position` -> table `etappe+1`, place `position` =
  {slot car, player name, tracktime/1000}.
- `[PLAYER] damage` -> slot damage.
- then `advance_etappe` 0x409870: `g_game_complete = (etappe >= 6)`; `etappe++`; `hiscore_build_totals`.

If `finished` is 0 (race aborted, arrested, time up, total loss, out of fuel) **nothing is read back**: money, fines and
damage of that race are discarded and the same etappe is offered again with the pre-race budget and damage.
(DAMAGE_MESH/DAMAGE_VALUE, DAMn/VALn and BEST TIMES are only used by spel.dat itself.)

## 3. The career
- New player: name (max 8 chars), budget `menu.ini start_budget` = **35000**, car 0 (Brabant/"Trabant", value 7500),
  no upgrades, damage 0, etappe 0. The new player first visits the showroom, then the garage.
- Etappes are played strictly in order 0..6 (`Raser.ini [MAPS]` 0..6): Amsterdam (city), Amsterdam - Den Haag
  (highway), Den Haag (city), Den Haag - Rotterdam, Rotterdam (city), Rotterdam - Amsterdam, Amsterdam (city).
  The menu calls highway etappes "Kwalificatie" and city etappes "Stedenrace". There is no branching: a finished race
  (any position) advances to the next etappe; an unfinished race is repeated.
- Between races: garage (upgrades, repair = price-per-% x damage), showroom (trade in: value = half the price of car
  and bought upgrades minus the repair cost), options, controls, standings.
- After the 7th etappe (`g_game_complete`): the standings screen is shown, then **win.smk** if the player is first in
  the TOTAAL table (sum of the 7 race times, compared with the three CPU racers), else **lose.smk**; the slot's etappe
  is set back to 0 (car, budget and upgrades are kept; Start reads "Restart").

## 4. The race program (spel.dat): process
- `WinMain` 0x431710: `init_main` 0x431610, `start_game` 0x424e30, then while `g_game_running` [0x4d9a78]:
  `handle_game` 0x424a70, frame timing, message pump. **The command line is ignored and the exit code is always 0**
  (also after a fatal error: MessageBox "Fout" + exit(0), 0x464e50). The result travels only through Game.ini.
- `init_main` order: Raser.ini settings (0x4209b0) -> debug -> language (0x431d60, LANGUAGE.INI `LANGUAGE` 0 NL / 1 EN
  / 2 DE, 56 texts of 256 bytes at 0x4dcc5c) -> **`[PLAYER] finished=0`** (0x421b40, so a crash counts as "not
  finished") -> GFX options -> 2D lib + CD check (`\DATA\intro.smk`, text 32) -> progress bar -> `init_game` 0x423d20
  -> end of progress bar -> frame timing.
- Global keys: Ctrl+Q emergency exit (fatal), Ctrl+F10 screenshot `lvl%2.2d_%2.2d.bmp` (etappe, counter).

### INI read by spel.dat
| File / section | Keys | Reader |
|---|---|---|
| Raser.ini PROGRAM / SOUND / IMG / PALET / HELPDESK / DIRECTX / MOUSE | RESOLUTION, NAME, FOCUS; ON, BUFFERS, SFXVOLUME (MUSICVOLUME stored, unused); RCSFILE; ETAPPEn, SHOW; DEBUGON, tweak_mode; INDEX, NAME, GUID; TYPE | 0x4209b0, 0x421b70 |
| Raser.ini MAPS | COUNT (<=20), n = type,file,4 textures,sky lo,sky hi,name,music,timer | 0x4225d0 (array 0x4d8e9c, 137 bytes each) |
| Raser.ini MUSIC | COUNT, n (loaded, never played) | 0x422b20 |
| Raser.ini wait_lo / wait_hi | 0..8 (8-bit / 16-bit loading screen; 16-bit uses .tga) | 0x4220e0 |
| Raser.ini progress | CALCULATE, COUNT, percentN | 0x41c1f0 |
| Game.ini ETAPPE | ETAPPE, DIRECTION (start lane) | 0x421b70 |
| Game.ini OPTIONS | resizetexture, startchunk, visiblechunks (10..50), 3dsound, benzineprijs (unused), texturecopy, collisionoff, Difficulty, fog, WAITVBL, FILTER, Nrlaps (1..999, forced 1 on highway), stuur_model, ai_model, dif_mod, demo | 0x421b70, 0x423d20 |
| Game.ini GFX OPTIONS | clouds, finish_flag, skidmarks, fade_skmarks, particles, part_rain, disable_overlay, overlay_detail, object_lights, gamma, alpha_blending | 0x421fd0 |
| Game.ini PLAYER | motor, banden, turbo, abs, versnellingsbak, injectie; money (0..999999), CAR (0..7), copland, position, damage, DAMAGE_MESH (8 int), DAMAGE_VALUE (8 float) | 0x421b70, 0x4228b0 |
| Game.ini RACERS | COUNT (must be 3), n = car,pos,time; DAMn, VALn | 0x422360 (array 0x485b20, 0x58 bytes each) |
| Game.ini BEST TIMES | LAP_RECORD_E%d, RACE_RECORD_E%d (0 = none -> 0x0fffffff), TIME_CHECK_COUNT_E%d (<10), TIME_CHECK_E%d_C%d | 0x422220 |
| Game.ini INPUT | DEVICE, LEFT, RIGHT, UP, DOWN, EXTRA1, EXTRA2, SENSITIVITY | 0x423910 |

### INI written by spel.dat (`exit_game` 0x421af0 -> `ini_save_all` 0x422d30)
- Always at start-up: `[PLAYER] finished=0`.
- At exit **only if finished=1**: `[PLAYER]` finished=1, money, damage, tracktime (ms), position (0-based),
  DAMAGE_MESH/DAMAGE_VALUE (0x422ec0); `[RACERS]` COUNT=3, n=car,position,time_ms, DAMn, VALn (0x422d40);
  `[BEST TIMES]` LAP_RECORD_Ee, RACE_RECORD_Ee, TIME_CHECK_COUNT_Ee, TIME_CHECK_Ee_Cn (0x422c10).
- With `[progress] calculate=1`: `COUNT` and `percentN` (measured load-step times) to Raser.ini (0x41c2b0).
So aborting, total loss, time-up and arrest all leave finished=0 and the menu discards the race (section 2).

## 5. Race lifecycle

### Game states (`g_game_state` 0x4d9a7c, `set_game_state` 0x424d20)
| State | Meaning | Side effects |
|---|---|---|
| 1 | START (camera fly-in + countdown) | action slot 2 = start camera |
| 0 | RACING | race_active 0x4d9a9c = 1, player_active 0x4d9e58 = 1, HUD on |
| 3 | FINISHED | action slot 4 = finish camera + results; HUD off (0x4d9aa0 = 1) |
| 6 | GAME OVER | action slot 12 = game-over sequence; HUD off |
| 8 | COP STOP (caught by police) | action slot 11; HUD off |
| 4 | QUIT | race_active = player_active = 0, `g_game_running` = 0 -> program ends |
| 2, 5, 7 | unused (finish is also accepted in 7 and 8) | |

Game "actions" (ev_main.cpp, 13 slots of `{init, update, done, free}` at 0x4a75c8): `start_game_action` 0x416760 calls
init; `handle_game_actions` 0x4168c0 calls update every frame and, when it returns non-zero, done.

### Start sequence
1. `start_game` 0x424e30: state 1; `init_player_info` 0x427fd0 (turbo charge 100 if the turbo upgrade is bought else 0;
   fuel 100 - never used again; race clock timer 3 count-up; easy flag = Difficulty==0); damage meshes applied to the
   player car (DAMAGE_MESH) and racers (DAMn).
2. Grid: start chunk = `startchunk` from Game.ini, else the chunk before the one carrying trigger 13 (`find_start_chunk`
   0x42a460). The player is placed `position` chunks behind it (last finish position = start position); copland=1
   forces car 6 (police car).
3. Start camera (ev_stcam): wait 1000 ms, fly in for 2 s, then the countdown action.
4. Countdown (ev_start, table 0x4869d8 of 18-byte entries `{char text[10]; int sample; int effect_serie;}`): every 1000 ms show
   **"3", "2", "1"** as 3D text 400 units in front of the car (RENDER.md "3D font", effect serie 1) with sound 42
   **getset.wav**, then **"GO?"** (drawn as "GO!", serie 2) with sound 43
   **go.wav**. (3git/2git/1git/gogit.wav = sounds 12-15 are never played and not in RCS.) After the 4th step:
   state 0, camera view 1; if copland: flash message 47 "COP MODE".
5. Trigger 13 (`trigger_race_start` 0x42a420, once) when crossing the start line: starts timer 0 (lap clock), timer 11
   (highway time limit), timer 3 (race clock) and the racer clocks 8/9/10.

### Per frame (`handle_game` 0x424a70)
timer update, objects, AI range, cameras, player car, skid marks, render, `check_game_keys` 0x424ee0, paths, game
actions, then:
- **Highway time limit** (state 0, highway only): `timer_expired(11)` -> game over reason 1. Each whole second while
  fewer than 6 s remain (5..0): sound 10 **tick.wav**.
- **Total loss**: damage >= 99 in state 0 -> game over reason 0.
- AI, 2D overlay (`draw_2d_display`, see [HUD.md](HUD.md)), flip.
- Auto detail: in state 0 with fps < 10, visiblechunks-- down to 15 (low-res) / 25 (16-bit D3D).
- Pause / quit dialog if requested.

### Timers (gm_clock.cpp, 12 x `{duration, start_tick (-1 = not started), countdown, inited, pause_tick}` at 0x4d8d58)
`timer_get` 0x4214d0: elapsed = now - start - paused span; countdown -> max(duration - elapsed, 0), else
min(elapsed, duration). `timer_set_duration` 0x421680 restarts a running timer. Pause freezes timers 0, 3, 8-11.
| Timer | Use |
|---|---|
| 0 | lap clock, count-up, cap = [MAPS] timer x 1000 = 10,000,000 ms (the "timer 10000" is only a cap); restarted every lap |
| 1 | traffic lights (5000/500 ms) |
| 3 | player race time |
| 5, 7 | blinkers (1000/500 ms) |
| 8, 9, 10 | CPU racer race times |
| 11 | highway time limit (countdown) |

### Highway time limit and checkpoints ("EXTRA TIJD")
`init_game` 0x423d20: `dist[etappe] = {0, 142, 0, 110, 0, 142, 0}`;
`limit_ms = int(dist * 10 / (topspeed(car) * k)) * 1000` with k = **0.4 easy / 0.65 hard**; topspeed = CARS.DAT float
+0x40 (+0x5c with the 6-gear box), +4 with the motor upgrade (`get_car_top_speed` 0x410b00). Example: car 0, hard,
etappe 1: 1420 / (41.89 x 0.65) = 52 s. City tracks have no time limit.
Trigger 45 (`trigger_time_check` 0x42a530), ignored within +-1 chunk of a check already passed this lap:
- Highway: `timer_set_duration(11, timer_get(11) + limit_ms)` (remaining time grows by one full initial allotment),
  sound 83 **checkp.wav**, HUD "CHECKPOINT" + remaining time + "EXTRA TIJD" for 2000 ms.
- City: split = lap clock - record split of this check (0 without a lap record), shown as "+ss.t"/"-ss.t" for 2 s;
  the current split is stored (up to 10 checks per lap).
- Both: flash message 44 "EXTRA TIJD".

### Laps and finish (Tr_finis.cpp)
- `Nrlaps` (Game.ini, default 2) on city tracks, 1 on highways; arrays hold max 10 laps (lap index asserts beyond).
- Trigger 15 (`trigger_arm_finish` 0x429a10) arms the finish (half-way anti-cheat). Trigger 14 (`trigger_finish`
  0x429950), in states 0/7/8 and armed: if the car moves forward (chunk > previous) -> `finish_lap` 0x429860, else
  "trying to finish illegal" and disarm.
```
finish_lap: t = lap clock; last_lap = t; laptimes[lap] = t; lap = min(lap + 1, Nrlaps)
  if t < LAP_RECORD: LAP_RECORD = t; copy splits -> record splits; message 31 "NIEUW RONDE RECORD"
  else message 45 "LAATSTE RONDE"            (as coded: after every non-record lap)
  TIME_CHECK_COUNT = checks this lap; restart lap clock
trigger_finish, when lap >= Nrlaps:
  tracktime = race clock (timer 3), pause it; if tracktime < RACE_RECORD: RACE_RECORD = tracktime (no message)
  state 3 (finish camera, juich.wav, results), finished = 1
```

### Positions
`calc_player_position` 0x408c00 (while racing): number of racers whose path progress is ahead of the player's
(tie: geometric compare) -> `position` 0x4d9e60 (0-based, HUD "%d/4"). At the player's finish
`estimate_racer_finish_times` 0x4114a0 extrapolates unfinished racers: `time += time / (total - gap) * gap` with
total = chunks x Nrlaps and gap = player progress - racer progress; equal times +1 ms; racer position = number of
racers with a lower time, positions >= the player's are shifted by one.

### Money: prize money and radar bonus (the only money changes in the race)
- `get_prize_money` 0x4280c0: by position **{55000, 35000, 15000, 0}**; **x4 on etappe 6** (the last race);
  4th place on **easy** = **5000** (overrides the x4).
- Radar bonus: knocking over a speed-camera pole (object type 7, 0x428d30) -> count 0x4dc964++, message 51
  "RADAR BONUS", speed cameras (trigger 1) in the next 20 chunks disabled. Paid at the finish: **2000 per pole**.
- `finish_done` 0x415690: money += prize; money += 2000 x poles (clamp 0..999999); finished = 1; state 4.
- **Not implemented** in this version although texts exist: fines (GEFLITST, BOETE: FL %d), refuelling (GETANKT,
  KOSTEN, NIET GENOEG GELD), fuel running out (GEEN BENZINE MEER), breakdown service (WEGENWACHT), and the
  BUDGET/PRIJZENGELD/BOETES/NIEUW BUDGET and "NIEUW RACE RECORD" texts (language texts 0-16, 20-23, 30 are never
  referenced). Fuel 0x4d9e40 is set to 100 and never read; Game.ini `benzineprijs` is read and unused.

### Speed cameras, speed limits, quick service, turbo
- Trigger 1 (`trigger_speed_camera` 0x42a2e0): if speed (km/h = car speed x 3.6, 0x4d9e34) > current limit
  (0x48c480, initially -1) and moving forward: sound 11 **flits.wav** + white flash (8-bit: palette (235,235,235) for 2
  frames; 16-bit: one-frame fill). No fine.
- Speed limit triggers: 4 -> 50, 5 -> 70, 6 -> 100, 8 -> 120 km/h (also select the speed-sign texture on highways,
  `set_speed_sign_texture` 0x42b190, FFC1.PCX frames 0-3). Road-side sign triggers 22-30 (highway) and 31-39 (city)
  in tr_vbord.cpp copy sign textures (txt_vbrd/txt_main).
- Trigger 2 subtype 2 (`trigger_quick_service` 0x42a4d0): damage = 0, turbo = 100 if the turbo upgrade is bought,
  sound 47 repair.wav, message 48 "QUICK SERVICE".
- Turbo (0x4d9e3c, 0..100): while the turbo key is held, -1 per 100 ms (10 s per charge), boost factor 6.0, sound 84.

### Damage, total loss
`add_damage` 0x428390 is ignored unless state 0 and not invincible. Per collision (impulse v from the physics):
```
if v < 18000: dmg = 0
v = min(v, 35000)
dmg = clamp(int(maxd * 0.01 * int(v * 0.0028571)), 0, maxd)     ; maxd = 12 hard / 6 easy
```
Damage bar blinks at >= 80; rattle sounds at >= 65 / >= 75; **total loss at >= 99** -> game over reason 0.

### Police and arrest (ev_cops.cpp)
When a police car reaches the player in state 0 (AI 0x402b40 / 0x40a390) the game goes to state 8:
`cop_stop_init` 0x415300: times caught (0x4a70e8)++; 1st -> message 49 "EERSTE WAARSCHUWING", 2nd -> 50 "LAATSTE
WAARSCHUWING"; random arrest speech (1st: sounds 48-53, 2nd: 54-58, 3rd: 59-63); camera on the cop.
`cop_stop_update`: after 4000 ms back to racing (state 0); at the **3rd catch -> game over reason 2 "GEARRESTEERD"**.
No fines. Cop radio: chase start "achter*" (sounds 64-76), give-up "opgev*" (77-81).

### Game over (ev_g_ovr.cpp, state 6)
`g_gameover_reason` 0x4867a4: **0 = TOTAL LOSS (text 40), 1 = TIJD IS OM (41), 2 = GEARRESTEERD (42)**, second line
43 "RACE OVER". `gameover_init` 0x416060: orbit camera, two texts slide in line by line (0x415c50 / 0x415e80);
`gameover_update` 0x4162e0: after the texts and 1000 ms, Esc/Enter/Space -> `gameover_done` -> state 4. finished stays
0. There is no out-of-fuel or breakdown game over.

### Results panel (`finish_update` 0x4155d0, `draw_results` 0x419ad0; layout in [HUD.md](HUD.md))
After 1500 ms the panel is drawn until Esc/Enter/Space. Times as "%02d:%02d:%d" (mm:ss:tenths).
- Highway (0x419870): background anim 5, text 17 "GEFINISHT", position digit (anim 2, frame = position), race time,
  then four numbers: money " %d", prize "+%d", radar bonus "+%d", total " %d" (= money + prize + bonus).
- City (0x4194e0): background anim 7, lap 1, lap 2 (only two laps shown whatever Nrlaps is), total time, and the same
  four money rows. The row labels (BUDGET / PRIJZENGELD / BOETES / NIEUW BUDGET) are presumably baked into the
  background images; the "BOETES" row holds the radar bonus.

### Pause and quit
`check_game_keys`: **P** -> pause (texts 24-26 "PAUZE / DRUK OP EEN TOETS OM DE / RACE TE VERVOLGEN."), **Esc** ->
quit dialog (27-29 "EINDE / WEET JE ZEKER DAT JE DE / RACE WILT BEEINDIGEN? (J/N)"); not in states 3 and 6.
`handle_pause` 0x4248d0 (blocking): pauses timers, mixer volume 0; pause: any key; quit: **J or Y** -> state 4 (quit,
finished=0), **N or Esc** -> continue.

### Other race keys
T horn (toeter.wav), D overlay detail 0..3 (radar > 0, gauges > 1, info panels > 2), 1/2/3/4 camera views 1/3/5 (held)/0;
tweak mode (Raser.ini TWEAK_MODE=1) only: 5,7,8,9,0 more views, M camera target, Tab car tweak menu, L chunk number,
F12 framerate. Cheats, always active: **Ctrl+Space+T** (highway: 3:00 left, "EXTRA TIJD"), **Ctrl+Space+I**
(invincible, "INVINCIBLE"). Cop mode = player name COPLAND in the menu (Game.ini copland=1).

### Difficulty
| | Easy (Difficulty=0) | Hard (1) |
|---|---|---|
| highway time factor k | 0.4 | 0.65 |
| max damage per hit | 6 | 12 |
| 4th place prize | 5000 | 0 |
| AI parameters 0x484aa8 / 0x484aac | 5 / 7 | 10 / 14 |
`dif_mod` (0..9, default 4) scales the CPU racer car performance by {0.6, 0.7, ... 1.5}[dif_mod] (0x4108d0); the racer
car is derived from the player's car plus the number of upgrades bought (see [AI.md](AI.md)).

### Loading progress bar and wait screens (gfx_pbar.cpp)
`handle_progress_bar` 0x41c410 is called at 11 fixed points while loading (init_game, road loader 0x444bf0, ...). Each
call either records the elapsed ms (`calculate=1`) or draws `percent[i]` (0..100): the wait screen of the etappe
(`[wait_lo]` PCX in 8-bit, `[wait_hi]` with .tga in 16-bit) and the bar sprite (low-res anim 18 at (246,177), 16-bit
anim 19 at (489,425)) with width = sprite width x pct / 100 (min 2). Shipped percents: 10, 23, 72, 74, 75, 91, 91, 91,
94, 94, 100.

## 6. Open questions
- What starts a police pursuit (AI 0x402b40; is it linked to the speed-camera flash or trigger 7)? See AI.md.
- Labels of the results panel (anims 5/7) still to be checked in the images.
- `LAATSTE RONDE` after every non-record lap: verify in game.
- Purpose of triggers 7, 9, 16, 17 and of game states 2/5/7.
- AI racer names in the menu standings are literally "racer 1".."racer 3".
