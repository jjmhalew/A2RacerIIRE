# The menu program (menu.dat)

`menu.dat` is a 640x480 DirectDraw program (MSVC 5.0, source `C:\work\A2racII\menu\Ned\prog\*.cpp` + the 2D library
`winlib`). It is started by the launcher (see [GAMEFLOW.md](GAMEFLOW.md)), shows the menus, and ends with exit code
**0x28 (40) = start the race** or **0xFF = quit the game**. Names: [names/menu_flow.txt](names/menu_flow.txt).
All coordinates below are 640x480 screen pixels, `(x,y)` top-left.

## Start-up (`menu_winmain` 0x4098f0)
1. `read_helpdesk` 0x4096c0: `Raser.ini [HELPDESK] DEBUGON` (debug.txt), `SNDFRT` (sound format), `WINMSG`.
2. Graphics mode 640x480 (title "Autobahn Raser", company "Davilex Software" - left over from A2 Racer 1), CD check
   ("Kan de A2 Racer II CD-rom niet vinden.", looks for `\data\intro.smk`), opens `Mrcs.img/.ind`.
3. `ini_read_all` 0x402410: `garage.ini` prices/percents, `menu.ini start_budget`, `Raser.ini [program] version`.
4. `init_options` 0x408b00 (`options.ras`), `init_slots` 0x40b6c0 (`SLOTS.RAS`), controls, `init_sound` 0x409730
   (SNDFRT=0: try 22050 Hz/16 bit/stereo, then 22050/8/mono, then 11025/8/mono; SNDFRT=1: 11025/8/mono; RASER.INI
   `[SOUND] ON=0` disables everything), menu animations (`menurc.img/.ind`), Windows mouse, help, Smacker
   (`smacker_init` 0x416ab0: a film is skipped with Esc, Enter, Space or the left mouse button).
5. If the command line contains `\FIRST` or `/FIRST` (only the first start by the launcher): play **intro.smk**.
   Otherwise: `menu.ini [PLAYER] save_game` = active slot, and if `Game.ini [PLAYER] finished` != 0 the race result
   is taken over (`read_race_results` 0x402390, `advance_etappe` 0x409870, `hiscore_build_totals` 0x40aeb0, see
   GAMEFLOW.md).
6. `start_music` 0x407500, then `menu_session` 0x401220: with `\FIRST` the player/slot screen comes first (cancel =
   quit), then `menu_main`.
7. Exit 0x28: `ini_write_race_setup` 0x401cc0 writes Game.ini/Raser.ini/menu.ini, then `save_slots`.
   Exit 0xFF: stop music, play **exit.smk**, `save_slots`.

Global keys in every screen loop: **Ctrl+F10** saves a screenshot `scr%02d.pcx` (`screenshot_key` 0x409660).
In the main menu **Ctrl+Space+M** held together sets the budget to **700000** (cheat, 0x407706).
A player named **COPLAND** gets `Game.ini [PLAYER] COPLAND=1` (cop mode in the race, `ini_write_player_items`).

## Common screen machinery
- Background: a full-screen PCX from Mrcs (`sub_4139c0(1, "x.pcx", 0x1f)`).
- Forms (winlib `forms.cpp`, `create_form` 0x411ba0): a table of 28-byte entries `{int x, y, type, ctrl*, style, 0,
  0}` ending with type -1. Type 1 = button `{int id, -1, -1, char key, char *text, 1, char *font, 0, -1, 0}` (37
  bytes, packed), type 4 = mouse hotspot `{int w, h, ...}`. The event code returned by `get_menu_event` 0x409d00 is
  the index in the table; 0x2710 = Esc (cancel), 0x2711-0x2714 = cursor keys (move focus, `click3.wav`).
  Enter activates the focused button. Button fonts: `butfnt.img` (big), `smallbt.img`; text fonts `stdfont.img`,
  `midfont.img`, `carved.img` (slot names), `cyber.img`, `ascii12.img`.
- Big buttons slide in from x=-100 to x=8 (`add_animated_button` 0x40c470, delays 0,2,2,4,6,8,10 frames); the title
  bar (0,0)-(640,47) has a per-screen animated header (`init_animated_header` 0x40c320: 0 player, 1 main, 2 garage,
  3 showroom, 5 options, 6 standings).
- Sounds: `klik.wav` on a button, `click3.wav` on focus moves, `fout.wav` error, `glow.wav` item highlight,
  `slide.wav` when the animated buttons slide in (`init_animated_buttons` 0x40c430), `print.wav` per frame of the
  typewriter text (`next_animated_text_frame` 0x40c110),
  `motor.wav` on Start. The help button **H** at (562,14) plays a spoken guided tour (`play_help` 0x40fcb0): each
  step highlights a form element and plays a `help_NN.wav`; any key stops it; music is turned down to -2000 during
  the tour. Help_05/06/07/53.wav exist but are not used.
- Dialogs: `janee_request` 0x4025a0 = centred panel (menu anim 0x47) with title (stdfont), message (midfont) and
  buttons **JA** (J) / **NEE** (N); returns 1 for JA.
- Menu animation bank: `menurc.img/.ind` (MENURC archive), frames by `anim_frame(anim, frame)` 0x409c80.

## Screens

### Main menu "hoofd" (`menu_main` 0x4076a0, `mm_open_screen` 0x406fd0, background `hoofd.pcx`)
| idx | Element | Pos / size | Action | Help |
|---|---|---|---|---|
| 0 | **Start** (S) / "Restart" | (8,63) | if etappe >= 7 reset career (`reset_career` 0x407320); `motor.wav`; exit 0x28 | help_13 |
| 1 | **Garage** (G) | (8,123) | `menu_garage(0)` | help_14 |
| 2 | **Besturing** (B) | (8,183) | `menu_bediening` | help_15 |
| 3 | **Opties** (O) | (8,243) | `menu_opties` | help_16 |
| 4 | **Standen** (C) | (8,303) | `menu_highscore` | help_17 |
| 5 | **Nieuwe speler** (P) | (8,363) | JA/NEE "Weet je zeker dat je de race wilt onderbreken?" -> stop music, `menu_nspeler(0)` | help_18 |
| 6 | **Einde** (E) | (8,423) | JA/NEE "Wil je het spel beeindigen?" -> exit 0xFF; Esc does the same | help_19 |
| 7 | H (help) | (562,14) | guided help | - |
| 8 | title bar hotspot | (0,0) 640x47 | | help_09 |
| 9 | player info | (186,66) 429x192 | | help_10 |
| 10 | next etappe | (186,277) 281x147 | | help_11 |
| 11 | control device | (468,277) 149x147 | | help_12 |

Content: box titles (stdfont, centred) "Spelerinformatie" in (195,71)-(610,94), "Volgende etappe" in (195,282)-(459,306),
"Besturing" in (475,282)-(610,306); labels "Naam" (207,114), "Budget" (207,145), "Schade" (207,201) in colour 0x1f, values
at x=297 in colour 0x3a: name, "Fl %d", "%d %%"; damage gauge anim 0x14 frame `min(damage/10, 9)` at (349,180);
next etappe: anim 0x13 frame = track type (0 highway, 1 city) at (214,340), track name (midfont) and "Kwalificatie"
(highway) or "Stedenrace" (city) from `Raser.ini [MAPS]` (`read_track_info` 0x406ca0; etappe >= 7 shows track 0);
control device icon anim 0x15 frame 0-3 at (506,333); animated header uses the car's animation.
The label of button 0 is "Restart" when the career was just completed (`g_game_complete` 0x43e70c) and the slot was not
just created. When `g_game_complete` is set on entry, the standings screen is shown first, then `end_of_game` 0x407580.

### Player / save slots "Nieuwe speler" (`menu_nspeler` 0x4081b0, background `newgame.pcx`)
Header "Naam       Auto     Geld    Etappe   Tijd" at (250,74). 9 slot rows (form elements 4-12) at (218, 104+36*i),
hotspot 393x27, row background anim 0x25 frame 0/1 (selected). Used slot: name (carved.img) at (x+14,y+7), car icon
(anim 0x27 frame car, selected anim 0x26) at (x+140,y+13), money `%d` at x+185, etappe `etappe+1` (or "-" when >= 7)
at x+275, total time `%02d:%02d` (sum of the player's times in the etappe tables) at x+324.
| idx | Element | Pos | Action | Help |
|---|---|---|---|---|
| 0 | **OK** (O) | (8,363) | empty slot: name request -> new slot -> showroom (new game); used slot: make it active | help_03b |
| 1 | **Annuleren** / **Einde** (A) | (8,423) | back (from the first start: "Einde" = quit the game) | - |
| 2 | H | (562,14) | help: help_01, help_02, help_03a (row), help_03b (OK), help_04 (row), help_08 (Verwijderen) | |
| 3 | **Verwijderen** (H) | (313,431) smallbt | only for a used slot that is not active: JA/NEE "Weet je zeker dat je dit spel wilt verwijderen?" -> `delete_slot` | help_08 |
| 4-12 | slot rows | | click selects; clicking the selected row = OK | |

The initially selected row is the most recently used slot (`get_last_slot` 0x40b710). If no slot is used at all, OK is
triggered at once. New player: `nsp_request` 0x40a490 - panel "Vul je naam in en wordt lid van de A2 racing club",
edit box (max 8 characters), OK/Annuleren; then slot.used=1, `slot_touch`, `g_new_player`=1, `menu_kopen(1)`
(showroom, OK continues to `menu_garage(1)`; cancelling the garage returns to the showroom).

### Garage (`menu_garage` 0x4046f0, background `garage.pcx`)
"BUDGET" label (238,76) + "Fl %d" centred in (301,74)-(401,92); "SCHADE" (430,76) + "%d %%" centred in (498,..)-(598,..);
car picture + property bars (186,102). Six item panels (133x109, anim from the item table, frame 0 normal, 1 highlighted
(`glow.wav`), 2 not affordable, 3 already bought / nothing to repair), name centred, price "Fl %d" below:

| idx | Item (garage.ini itemN) | Panel pos | anim | Effect | Sound |
|---|---|---|---|---|---|
| 4 | MOTOR (item1) | (186,270) | 47 | upgrade MOTOR=1 | motor.wav |
| 5 | BANDEN (item2) | (332,270) | 48 | BANDEN=1 | banden.wav |
| 6 | REPAREREN (item3) | (478,270) | 49 | cost = price x damage%; damage=0 | repair.wav |
| 7 | TURBO (item4) | (186,369) | 50 | TURBO=1 | banden.wav (sic) |
| 8 | ABS (item5, "remmen") | (332,369) | 51 | ABS=1 | antiblok.wav |
| 9 | 6 VERSNELLINGEN (item6) | (478,369) | 52 | VERSNELLINGSBAK=1 | versne~1.wav |

Buttons: **Showroom** (S) (8,303) -> `menu_kopen(0)`, **OK** (O) (8,363) keeps the purchases, **Annuleren** (A) (8,423) /
Esc restores the slot copy made on entry. Buying asks "Weet je zeker dat je dit onderdeel voor Fl %d wilt kopen?
(Budget: Fl %d)" (title = item name); repair asks "... wilt laten repareren?" (title REPAREREN). Hovering an item shows
its property increase on the 4 bars. Help: help_20/21 (title), 22-25a (hotspots), 25b-25f (items), 26, 30-32 (buttons).

Prices (garage.ini [prices], Fl), cars 1-7 = Trabant, Astra, VW, VW, BMW, Porsche, Diablo in the ini comments; the
menu names them Brabant, Opol Astro, Wolfswagen, BWM Z3.5, PARCHE 913, Mersedes SLK GTR, Lambdaghini (qrace: Trabant,
Astra, Kever, ?, Porsche, Mercedes, Lamborgini):

| car | price | motor | banden | repair/% | turbo | remmen | versn. | speed | accel | brakes | grip |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 Brabant | 15000 | 3500 | 500 | 100 | 2500 | 1500 | 2000 | 150 | 20 | 30 | 20 |
| 1 Opol Astro | 25000 | 5000 | 1000 | 200 | 4000 | 2000 | 3000 | 165 | 25 | 50 | 30 |
| 2 Wolfswagen | 40000 | 7000 | 1500 | 300 | 5000 | 3000 | 3500 | 180 | 35 | 60 | 40 |
| 3 BWM Z3.5 | 60000 | 10000 | 2000 | 400 | 8000 | 4000 | 6000 | 195 | 50 | 60 | 50 |
| 4 PARCHE 913 | 100000 | 20000 | 3000 | 600 | 12000 | 5000 | 10000 | 210 | 65 | 70 | 60 |
| 5 Mersedes SLK GTR | 150000 | 25000 | 4000 | 800 | 19000 | 8000 | 14000 | 225 | 70 | 80 | 70 |
| 6 Lambdaghini | 200000 | 35000 | 5000 | 1100 | 25000 | 15000 | 20000 | 240 | 80 | 90 | 80 |

Item effects ([percents] itemN_percM; M = 1 speed, 2 acceleration, 3 brakes, 4 grip): motor +15 speed, banden +50 grip,
remmen +50 brakes, versnellingsbak +15 speed +20 accel; repair and turbo 0. The bars show `(v - min) / (max - min) * 100`
with (min,max) = (100,300) for speed and (0,100) for the others, clamped to 1..100 for cars, 0..100 for items (so the
+15 speed of an item is drawn as 0). These numbers are only for the menu bars; the race uses its own car data.

### Showroom "kopen" (`menu_kopen` 0x405f10, background `showroom.pcx`)
BUDGET + "Fl %d" as in the garage; own car: picture (car anim, frame 5) at (522,199), name (437,109), "Waarde : Fl %d"
(437,121) with `car_trade_in_value` 0x405ac0 = price/2 + sum over bought items (not repair) of item price/2 - repair
price x damage. The other 6 cars (`build_car_list`) are browsed with **1** (prev, (192,432)) and **2** (next,
(516,432)); the car slides in (`scroll_special_animation`), its name and "Prijs : Fl %d" appear as animated text and its
property bars at (186,270). **KOPEN** (K) (300,432) is enabled when budget >= price - trade-in value; JA/NEE "Weet je
zeker dat je je auto voor een %s wilt inruilen? Inruilkosten: Fl %d (Budget: Fl %d)" (or "Meerwaarde: Fl %d" when the
trade-in is worth more). Buying: car = new, money -= (price - value), clamp to 999999, all 6 item fields (upgrades and
damage) = 0. **OK** (8,363): in new-game mode continue to the garage, else back. **Annuleren** (8,423) / Esc restores the
slot. Help: help_33-40.
A new player owns car 0 (Brabant, value 7500) and 35000 budget, so the first purchase can be up to car 2 (40000 - 7500 =
32500).

### Controls "Besturing" (`menu_bediening` 0x4035f0, background `bedien.pcx`)
Device icons (80x80 hotspots, radio): (210,150) keyboard, (301,150) joypad, (210,241) wheel/joystick, (301,241) Sidewinder
Force Feedback -> slot device 0-3 (written as Game.ini `DEVICE` 1,2,3,3). Sensitivity slider (215,391) 151x30, only for
devices 2 and 3 (`Sensitivity`, default 50). Key panel (417..)/"Toetsen" with Links, Rechts, Gas, Rem, Turbo, Handrem
and their key names (table 0x42a4a8: VK code -> "Links", "Rechts", "Omhoog", "Omlaag", "NUMPAD n", "SPACE", "ENTER",
...); "Instellen" (426,146) 179x21 (keyboard only) asks "Druk op een toets voor <function>" for the 6 keys in turn; Esc
aborts and restores, a key used by an earlier function is refused (`fout.wav`), otherwise duplicates are swapped.
Defaults: Left 0x25, Right 0x27, Up 0x26, Down 0x28, Ctrl 0x11 (turbo), Alt 0x12 (handbrake). OK / Annuleren (Esc restores).
Help: help_41-49.

### Options "Opties" (`menu_opties` 0x409190, background `opties.pcx`)
Box titles "GELUID", "GRAPHICS", "Moeilijkheidsgraad"; version "v%0.2f" at (600,460).
| idx | Element | Pos | Value |
|---|---|---|---|
| 3 | SFX volume slider | (377,131) 151x30 | 1..100 |
| 4 | music volume slider | (377,202) | 1..100 |
| 5 | "Geluidseffecten" checkbox | (248,130) | sfx on |
| 6 | "Muziek" checkbox | (248,202) | music on (starts/stops the menu music at once) |
| 7/8/9 | radio "320 x 200" / "640 x 480" / "3D Kaart" | (257,313) / (257,341) / (407,322) | RESOLUTION 0 / 1 / 3 |
| 10/11 | radio "Makkelijk" / "Moeilijk" | (250,427) / (407,427) | Difficulty 0 / 1 |
OK saves `options.ras`, Annuleren / Esc reloads it. Help: help_50-52, 54-62.

`options.ras` (18 bytes, packed, at 0x43e7d8): `int sfx_on; int music_on; u8 sfx_vol; u8 music_vol; int resolution;
int difficulty;` defaults 1, 1, 80, 80, 0, 1. Menu volume: DirectSound volume = `vol*30 - 3100` (1/100 dB). Written to
Raser.ini `[Sound] SFXVOLUME/MUSICVOLUME = on ? 3100 - 30*vol : 0` (an attenuation; 0 = off).

### Standings "Standen" (`menu_highscore` 0x405630, background `hiscore.pcx`)
8 tabs (x=177, y=59+45*i, 108x50 hotspots; selected tab anim 0x3b frame i, others anim 0x3a, colour 0x1f / 0x74):
tab 0 "TOTAAL", tabs 1-7 the track names from `Raser.ini [MAPS]` (split at '-' onto two lines). Header
"    Naam         Auto        Tijd" (307,76); 4 rows at y = 108 + 36*i: name (x=325), car icon anim 0x39 (x=460),
time `%02d:%02d` (x=540). A table is only filled when the slot has reached that etappe. The selected tab is saved in
the slot (byte +0x57). OK (8,423) / Esc. Help: help_63-68.

## Save games: `SLOTS.RAS` (`slotio.cpp`)
Raw dump of `g_slots` 0x43ec28: **9 slots x 536 bytes = 4824 bytes** (packed, little endian). Created with
`init_slot` defaults when missing; saved on every exit of the menu.
| Off | Type | Field |
|---|---|---|
| 0x00 | i32 | used (0 = empty) |
| 0x04 | i32 | last-used counter (highest = most recent; renumbered on delete) |
| 0x08 | char[10] | name (max 8 chars typed) |
| 0x12 | i32 | car 0..6 |
| 0x16 | i32 | etappe: number of finished etappes = next track 0..6 (7 = career done) |
| 0x1A | i32 | unused (cleared by reset) |
| 0x1E | i32 | money (budget, 0..999999) |
| 0x22 | u8 | control device 0..3 |
| 0x23 | i32[6] | items: motor, banden, **damage %** (item 3 = repair), turbo, abs, versnellingsbak (0/1) |
| 0x3B | i32[6] | keys (VK codes): left, right, up(gas), down(brake), extra1(turbo), extra2(handbrake) |
| 0x53 | i32 | sensitivity (default 50) |
| 0x57 | u8 | selected standings tab |
| 0x58 | rec[8][4] | standings: table 0 = TOTAAL, table e+1 = etappe e; 4 places; rec = 14 bytes `{u8 car; char name[9]; i32 time_s}` |

`menu.ini [PLAYER] save_game` holds the active slot (0-8), `start_budget` the budget of a new slot (35000).
Per-etappe tables are written from `Game.ini` after a finished race: `table[e+1][position] = {car, name, ms/1000}` for the
player and for the 3 CPU racers (named "racer 1".."racer 3"). TOTAAL is rebuilt every time: for the player and each racer
the sum of their times over tables 1..etappe, insertion-sorted ascending (`hiscore_insert_total` 0x40a970).

## Smacker films (`play_smacker` 0x409f70, winlib `smacker.cpp`)
All four are 320x240, 15 fps, in `\data\` of the CD: **intro.smk** (1375 frames, 92 s) on the first start (`\FIRST`),
**win.smk** (236 frames) / **lose.smk** (300 frames) after the 7th etappe (win when the player is first in TOTAAL),
**exit.smk** (99 frames) when quitting. `Raser.ini [MOVIES] ON=0` disables films; `SIZE=1` blits to (0,0)-(640,480)
(stretched 2x), `SIZE=0` to (160,120)-(480,360) (1:1, centred). Skip keys: Esc, Enter, Space, left mouse button. Smacker
uses DirectSound for its audio track.

## Menu music
`start_music` 0x407500 plays `g_music_per_etappe[etappe % 7]` looped at the music volume: etappe 0 speed.wav,
1 energy.wav, 2 jammin.wav, 3 moody.wav, 4 disco.wav, 5 speed2.wav, 6 moody.wav. `ALGINTRO.WAV`, `rock.wav`, `flash.wav`,
`CLICK1.WAV`, `CLICK2.WAV` in Mrcs are not referenced by name in menu.dat.
