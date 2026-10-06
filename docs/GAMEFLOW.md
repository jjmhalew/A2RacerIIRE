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

RACE-SECTION-PLACEHOLDER
