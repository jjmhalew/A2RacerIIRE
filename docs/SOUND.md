# Sound

Names: [names/spel_flow.txt](names/spel_flow.txt) (plus spel_physics/spel_ai/spel_objects for some callers),
[names/menu_flow.txt](names/menu_flow.txt). Menu sounds and music: [MENU.md](MENU.md). DirectSound volumes are in
1/100 dB (0 = full, -10000 = silent).

## 1. Settings
| Setting | Where | Effect |
|---|---|---|
| Raser.ini `[SOUND] ON` | 0x4895f9 | 0 = no sound at all |
| Raser.ini `[SOUND] BUFFERS` | 0x4895fd | DirectSound buffer pool size (60) |
| Raser.ini `[SOUND] SFXVOLUME` | 0x489631 | **attenuation**: every race volume is `base - SFXVOLUME`; 0 = sound effects off. The menu writes `3100 - 30*slider` (slider 1..100), so 100 = loudest, 3100 = quietest (the ini comment has it backwards) |
| Raser.ini `[SOUND] MUSICVOLUME`, `[MUSIC]` | 0x48962d, 0x4d9950 | read, never used: **the race plays no music** (M_FUNK..M_ZZTOP.wav are not on the CD) |
| Game.ini `[OPTIONS] 3dsound` | 0x4d9a94 | 1 = stereo pan for positional sounds, 0 = centre |
| Raser.ini `[HELPDESK] SNDFRT` (menu only) | menu 0x409730 | 0 = best format, 1 = 11025/8/mono |

## 2. winlib sound layer (Sound.cpp, spel 0x466b40..0x468210; menu has the same code at 0x4152a0..)
- `snd_init(n)` 0x4674d0: DirectSoundEnumerate (prefers a driver whose module is SB16.VXD, then the secondary driver,
  then default), DirectSoundCreate, cooperative level DSSCL_PRIORITY. Primary buffer formats tried: 22050 Hz/16 bit/
  stereo, then 22050/8/mono, then 11025/8/mono (mode table 0x49f748). On failure "Soundlib init FAILED" and sound off.
- Buffer pool `g_snd_buffers` 0x4eac38: n entries of 64 bytes `{char name[40]; IDirectSoundBuffer*; IDirectSound3DBuffer*
  (unused); inUse; type (0 = 2D); useCount; dataLength}`. Secondary buffers have CTRLVOLUME|CTRLPAN|CTRLFREQUENCY
  (0xE0); **DirectSound3D buffers exist in the code (0x466f10) but are never created**.
- WAV loading (0x467c30): fixed 44-byte RIFF header (data length at +0x28, channels > 1 = stereo), data read from the
  resource archive (RCS) into the locked buffer.
- `snd_play(name, flags)` 0x467dc0 / `snd_play_vol` 0x467fe0: find a buffer with that name; if it is busy find another
  idle one with that name or DuplicateSoundBuffer it into a free slot; if there is no free slot evict the buffer with
  the lowest useCount (stopped and released even when playing). flags: 1 = loop, 4 = lock (useCount 1000000).
  Returns the buffer index or -1.
- Range checks: SetVolume -10000..-100, SetPan -10000..10000, SetFrequency 100..100000 (outside -> fatal "Fout").

## 3. Race sample table (`g_sndfx_table` 0x48baf0, 86 entries x 19 bytes `{char name[15]; int preload;}`)
`sndfx_init` 0x428590 "preloads" every entry with preload=1 by playing it and setting volume -10000. Index 0 = no sound.
| Idx | WAV | Pre | Trigger | Kind / caller |
|---|---|---|---|---|
| 0 | werk01 | 0 | (index 0 means "no sound") | - |
| 1 | werk02 | 0 | road-works objects from the .omp (PILON, CNSTR, KEET, WALS, ...), once when in range | positional, object sample field |
| 2 | kerkklok | 0 | church bell objects (E00_OUDK, E02_GROT), looping | positional, object type 1/5 0x429380 |
| 3-9 | tick | 0 | unused | |
| 10 | tick | 1 | highway: each second while < 6 s of the time limit are left | 2D, handle_game 0x424bd3 |
| 11 | flits | 1 | speed camera when faster than the limit (+ white flash) | 2D, 0x42a2e0 |
| 12-15 | 3git, 2git, 1git, gogit | 0 | unused, files missing | |
| 16 | pionb1 | 1 | traffic cone hit | positional, object type 2 0x4293f0 |
| 17 | pionb3 | 1 | cone second impact | " |
| 18 | paal | 1 | pole hit: construction pole (type 3 0x4297d0), speed-camera pole (type 7 0x428d30) | positional |
| 19 | paal | 1 | pole second impact | " |
| 20 | tanken | 0 | unused (no refuelling in this version) | |
| 21 | juich | 1 | crowd cheer at the finish | 2D, finish_init 0x4154d0 |
| 22 | start | 0 | unused | |
| 23 | motor | 1 | player engine idle loop | 2D loop, section 4 |
| 24 | toeren1 | 1 | player engine rev loop (no motor upgrade) | 2D loop |
| 25, 26 | carsoft | 1 | collision, impulse < 18000 (random of the two) | 2D, plr_snd_collision 0x427cc0 |
| 27, 28 | carmid, carmid2 | 1 | collision, impulse < 25000 | " |
| 29, 30 | carhard, carhard2 | 1 | collision, impulse >= 25000 | " |
| 31-33 | auto1, auto2, auto3 | 1 | traffic car engines (car types 0-10, 13, 15); auto2 also city path cars | positional loop, car type table 0x484b26 +0xDC |
| 34 | best_bus | 1 | delivery van engine (car type 14) | positional loop |
| 35 | bus | 1 | bus engine (car type 11) | positional loop |
| 36 | vracht | 1 | truck engine (car type 12) | positional loop |
| 37 | rammel | 1 | rattle while damage >= 65 | 2D loop, handle_player_car, table 0x48b440 |
| 38 | rammel2 | 1 | second rattle while damage >= 75 | 2D loop |
| 39 | sliploop | 1 | tyre squeal, skid level >= 4 | 2D loop, 0x4259a0 |
| 40 | psirene1 | 1 | police siren: replaces the cop car's engine sample during a chase | positional loop, cop_siren 0x40c640 |
| 41 | toeter | 1 | horn, key T held (one-shot every frame) | 2D, check_game_keys |
| 42 | getset | 1 | countdown "3", "2", "1" | 2D, countdown 0x416bd0 |
| 43 | go | 1 | countdown "GO" | 2D |
| 44 | vliegt | 1 | aeroplane path object | positional loop, 0x4345e1 |
| 45 | trein | 1 | train / tram path object | positional loop, 0x434b01 |
| 46 | heli | 1 | helicopter (city tracks) | positional loop, pth_heli 0x43332c |
| 47 | repair | 1 | quick service (damage 0, turbo refill) | 2D, 0x42a4d0 |
| 48-53 | aan11a, aan14a, aanh11..14 | 0 | police speech, 1st catch (random) | 2D, cop_stop_init 0x415300, table 0x486648 |
| 54-58 | aan21a, aan22a, aanh21..23 | 0 | 2nd catch | table 0x486660 |
| 59-63 | aan31a, aan31b, aan32a, aanh33, aanh34 | 0 | 3rd catch (arrest) | table 0x486678 |
| 64-76 | achter1..9, 11, 12, 14, 19 | 0 | police radio when a cop starts a chase (always if it chases the player, else 1/3), only when the previous one finished | 2D, 0x4150f0 (table 0x486690) |
| 77-81 | opgev1, 2, 3, 4, 9 | 0 | cop gives up the chase | 2D, 0x415170 (table 0x4866c8) |
| 82 | schuur2 | 1 | scrape: repeated contact within 300 ms with impulse < 18000 | 2D, 0x427d09 |
| 83 | checkp | 1 | highway checkpoint passed (extra time) | 2D, 0x42a5a8 |
| 84 | turbo | 1 | turbo key held while turbo > 0, stops on release | 2D loop, 0x425d49 |
| 85 | toeren2 | 1 | engine rev loop with the motor upgrade | 2D loop |
RCS also holds `achter.wav` (not in the table). The play functions check `index <= 0x56` (off by one).

## 4. 2D sounds and the player car
- `sndfx_play_sample(i)` 0x4286a0 / `_loop` 0x428700: no pan, native frequency, volume `-500 - SFX` (set after Play
  started); police speech aanh* `-100 - SFX`. All return at once when SFXVOLUME = 0.
- **Engine** (`plr_snd_engine_update` 0x427070, every frame; r = (int)rpm from the physics, 0x4a63d8, idle 800):
  ```
  idle loop motor.wav:  freq = clamp(24000 - r, 11000, 24000);  vol = clamp(-(r / 8), -2000, -100) - SFX - 200
  rev loop toeren1/2:   freq = min(10000 + 3*r, 40000);         vol = clamp(r / 2 - 2000, -2000, -100) - SFX - 200
  ```
  (integer division truncates toward zero). At idle: motor 23200 Hz at -100, toeren 12400 Hz at -1600; the rev loop is
  at full volume from r >= 3800 and its pitch caps at r = 10000. Both loop all race, no pan.
- **Tyre squeal** (0x4259a0, skid level n = 0..10 at 0x4a63dc): n < 4 stop; else sliploop with
  `freq = clamp(15000 + 700*n, 15000, 28000)`, `vol = clamp((n - 11)*100, -1100, -100) - SFX - 100`.
- **Damage rattle**: rammel loops while damage >= 65, rammel2 additionally while >= 75.
- **Collisions** (0x427cc0): impulse < 18000 carsoft (or schuur2 scrape for repeated contact), < 25000 carmid/carmid2,
  else carhard/carhard2; impulse >= 18000 also adds damage (see GAMEFLOW.md).

## 5. Positional sound (snd_main.cpp)
`sndfx_play_object_sample(obj, loop)` 0x428bc0 is called every frame for AI cars (racers loop; other traffic per AI
state), cops, path objects (helicopter, plane, train, path cars) and road-side objects. The listener is the **player
car**, not the camera.
```
sample = object.sample (+0x14); if 0: return done
dx = (int)(src.x - player.x); dz = (int)(src.z - player.z)        ; height ignored
if 3dsound: len = sqrt(dx*dx + dz*dz)
            pan = clamp((int)(((dx/len)*M[10] - (dz/len)*M[8]) * 90), -90, 90)   ; M = player 4x4 matrix
else        pan = 0
dist = |dx| + |dz|                                                 ; Manhattan distance
if dist >= 2500: stop, free slot, return done
slot = this object's slot or the first free of 20 (0x4da1b0); none free -> sound dropped (no priority)
new:    play at vol[dist] - SFX (loop flag), then SetVolume(vol[dist] - SFX - 100), SetPan(111*pan),
        SetFrequency(original + rand() % 500) clamped 8000..40000 (random detune, no Doppler)
update: SetVolume(vol[dist] - SFX), SetPan(111*pan)
vol[d] = clamp(-trunc(100 * 30^(d/2500)), -3000, -100)            ; -1 dB at 0 .. -30 dB at 2500
```
One-shot object sounds (cones, poles, road works) set the object's sample, play until the buffer stops, then the object
sample is reset to 0. `sndfx_set_object_sample` 0x4289e0 swaps a sample (siren), `sndfx_stop_object_sample` 0x428a70
stops it when a path object is removed. Limits: 20 positional sounds at once, 60 DirectSound buffers in total.

## 6. Menu sounds
See MENU.md: menu sfx (klik, click3, fout, glow, slide, print, motor, item sounds) play once at `vol*30 - 3100`, music
loops per etappe at the music volume, spoken help help_NN.wav, Smacker films use DirectSound for their audio.

## 7. Open questions
- World units of the 2500 distance limit; which side a positive pan is (sign of M[8]/M[10]).
- The other names file calls 0x428bc0 `object_hit`; by its use it is the positional sound update (see above).
- AI state 9 cars play their engine non-looping; the AI horn (sample 41) is tested but never set.
- 2D one-shots set the volume after Play, and preloading plays each sample before muting it: possible audible blips.
