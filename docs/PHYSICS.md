# Player car and vehicle physics (spel.dat)

Scope: `plr_car.cpp` (0x4256a0..0x427730 + collision 0x427190..0x427fd0), `plr_main.cpp` (0x427fd0..0x428400),
`car_main.cpp`/`car_whl.cpp` (0x410b00..0x413ed0), the unnamed car-physics file 0x40cfd0..0x410a10 (no assert
strings; it contains the new and old steering models and the AI drive code), the object library `obj_main.cpp`
(0x43f900..0x441490), `obj_phys.cpp`, `obj_dmg.cpp`, `bbox.cpp`, and the timer `tmg_main.cpp`. Input devices are in
[INPUT.md](INPUT.md), road file formats in FORMAT_ROAD.md (other agent). Names are in `docs/names/spel_physics.txt`.

All pseudocode is transcribed from the x87 code; constants are the exact float values (`f` = float32 in .rdata,
`d` = double). `dt` is always `timer_frame_dt()` (0x441cd0), called again at each use (it returns the same value
within a frame). `ftol()` = MSVC `_ftol` (0x4700e0) = truncation toward zero. Odd behaviour (frame-rate dependence,
apparent bugs) is transcribed as is and marked **[quirk]**.

## 1. Units and conventions

| Quantity | Unit |
|---|---|
| world position (3D instance +0x50/+0x54/+0x58) | 1/10 m ("decimetre"): the integrator multiplies velocities by 10 |
| object velocity (+0x6c/+0x70/+0x74) | m/s, in the car's local frame: x = lateral, y = vertical, z = forward |
| displayed speed `g_player_speed_kmh` 0x4d9e34 | `ftol(v_forward * 3.6f)` |
| angles of 3D instances (+0x74 pitch, +0x78 yaw, +0x7c roll) | 1024 units per turn (`c_angle_units` 0x49b014 = 1024.0f, mask 0x49b018 = 1023) |
| sin/cos tables | `SIN` = *(float**)0x4ea418, `COS` = *(float**)0x4ea4b4, 1024 entries, `SIN[i] = sin(2*pi*i/1024)` (math3d.cpp 0x450280) |
| yaw rate (object +0x7c) | rad/s (integrator converts with 1024/2pi) |
| steering input (new model) | -100..100 (negative = LEFT key) |
| gas / brake input (new model) | 0..100 (brake is an int) |

World yaw `a = 2*pi*yaw/1024`. A local displacement (dx, dz) moves the car by
`x += COS[a]*dx - SIN[a]*dz; z += SIN[a]*dx + COS[a]*dz` (see 6.2). In Direct3D's left-handed frame (+x right,
+y up, +z forward at yaw 0) an increasing yaw turns the nose from +z toward -x, i.e. **left**. LEFT key -> negative
steer -> positive yaw rate -> left turn.

## 2. Frame loop and timestep

### 2.1 Timer (tmg_main.cpp, object `g_game_timer` 0x4e4970)

```c
struct Timer { int has_qpc; float freq; float last_time; float dt; };   // 0x4e4970
float g_dt_hist[4];  int g_dt_idx;   // T_ARRAY 'g_elapsed_buffer' data ptr 0x4e4928, idx 0x4e4980; init 0.1f each

void timer_update(Timer *t)          // 0x441bd0, once per frame at the top of handle_game
{
    float now = (has_qpc ? QueryPerformanceCounter : timeGetTime) / t->freq;   // freq = QPF, or 1000.0f
    t->dt = now - t->last_time;  t->last_time = now;
    g_dt_hist[g_dt_idx] = t->dt;
    // weighted average of the last 4 raw frame times: 1/2, 1/4, 1/8, 1/8
    float w = 0.5f, s = t->dt * 0.5f; int i = g_dt_idx;
    for (int k = 0; k < 2; k++) { if (--i < 0) i = 3; w *= 0.5f; s += g_dt_hist[i] * w; }
    if (--i < 0) i = 3;  s += g_dt_hist[i] * w;          // last weight stays 0.125
    t->dt = s;
    if (++g_dt_idx >= 4) g_dt_idx = 0;
}
float timer_frame_dt(Timer *t)       // 0x441cd0
{   if (t->dt == 0.0f) return 0.1f;  if (t->dt > 0.1) return 0.1f;  return t->dt; }
// timer_reset 0x441d10: re-reads the clock, history = 0.1f, dt = 0 (used after pause, 0x4248d0)
```

**Variable timestep**, smoothed over 4 frames, clamped to 0.1 s (0.1 s also when dt == 0). No fixed step.

### 2.2 Per-frame order (`handle_game` 0x424a70)

1. `timer_update(&g_game_timer)`.
2. `rceng_handle_objects(player.chunk(+0x18), 2*g_visible_chunks, ...)` 0x440850 (obj_main.cpp):
   * for every used object index i ascending (0..last used): if `obj.active(+0x34)` and (chunk within the window
     around the player, or `obj.always_update(+0x40)`) call `obj.update_cb(+0x44)(i)`. The player's update callback is
     `handle_player_car` 0x425e20 (registered by `init_player_car`, see 4.1); AI and traffic have their own.
   * then `obj_move_all` 0x440ea0: integrates and collides every object of the physics list (6.1).
3. `ai_check_cars_outside_handling_range` 0x40b2d0, `handle_game_cameras` 0x4144b0, `tyre_smoke_emitter`
   0x425a90 (squeal sound + smoke, 9.2), skid marks `0x41dd00(player, g_plr_skid)`, `render_game` 0x425090,
   `check_game_keys` 0x424ee0, paths/helicopter, `handle_game_actions` 0x4168c0 (events), time-out check
   (clock 0xb), damage check: `if (plr_get_damage() >= 99 && g_game_event == 0) set_game_event(6)` (wrecked =
   game over), AI, 2D overlay, flip.

So within one frame: input + forces of the player (in its update callback) happen **before** the integration of all
objects; the player is integrated in 8 sub-steps (6.1).

## 3. Data

### 3.1 Object (0xA0 bytes, array `*(Obj**)0x4e48e0`, constructor 0x43f990)

| Off | Type | Meaning |
|---|---|---|
| +0x00 | int | physics type (1 = dynamic, integrated by obj_move_all) |
| +0x04 | int | ? (0) |
| +0x08 | int | index of the 3D instance (`*(Inst**)0x4ea9e0`, stride 0x88) |
| +0x0c | int | AI / car-list index (-1) |
| +0x10/+0x14 | int | |
| +0x18 | int | current road chunk |
| +0x1c | int | previous road chunk |
| +0x20 | int | waypoint (driver-map) index for obstacle lists |
| +0x24 | int | -1 |
| +0x28 | int | lane/side |
| +0x2c | int | 1 |
| +0x34 | int | active (1) |
| +0x38 | int | 1 = do **not** rotate the velocity with the yaw change in the integrator (AI cars); player = 0 |
| +0x3c | int | in-collision flag (set by obj_move_all) |
| +0x40 | int | always update (ignore chunk window) |
| +0x44 | fn | update callback `int cb(int obj)` |
| +0x48 | fn | collision callback (player: 0x427c70) |
| +0x4c..+0x58 | | 0 |
| +0x5c | | 0 |
| +0x60 | float | inverse mass (obj_init_physics 0x4403b0: `1/mass`, 0 for immovable) |
| +0x64 | float | inverse yaw inertia (`1 / (mass*(w*w+l*l)*c)`, c at 0x47fb8c) |
| +0x68 | float | coefficient of restitution (0..1, default 1.0) |
| +0x6c/+0x70/+0x74 | float | local velocity x (lateral), y (vertical), z (forward), m/s |
| +0x78 | float | pitch angular velocity (body motion, 1024-units/s) |
| +0x7c | float | yaw rate, rad/s |
| +0x80 | float | roll angular velocity (body motion) |
| +0x84/+0x88/+0x8c | float | accumulated force (old model, collisions) |
| +0x90 | float | accumulated yaw torque |
| +0x94..+0x9c | int | -1 (sound/light handles) |

3D instance (stride 0x88) fields used by physics: +0x10 4x4 matrix (built by 0x459360: roll, pitch, yaw, then
`rceng_set_object_to_chunk_orientation` 0x442390 multiplies the chunk rotation matrix (chunk +0x9c) in, so the car
is drawn tilted with the road), +0x50/+0x54/+0x58 position, +0x74 pitch, +0x78 yaw, +0x7c roll (all 1024 units).

Helpers: `obj_clear_forces` 0x43fb90 (+0x84..+0x90 = 0), `obj_add_force_at` 0x4407e0 (force += F;
torque += P.x*F.z - P.z*F.x), `obj_get_child_instance` 0x43fbb0 (wheel sub-objects 0..3).

### 3.2 Player globals (plr_car.cpp / car physics)

| Addr | Name | Meaning |
|---|---|---|
| 0x4d9e28 | g_plr_obj | player object index |
| 0x4d9e2c | g_plr_car | player car type 0..6 (Game.ini [PLAYER] car; copland=1 forces 6 = police) |
| 0x4d9cd8 | g_plr_steer | steering input (new: -100..100; old: 0..60, 30 = centre); init 30 |
| 0x4d9cdc | g_plr_gas | gas (new: 0..100; old: 0..40, 20 = neutral); init 20 |
| 0x4d9e08 | g_plr_brake | brake, **int** 0..100 (new model) |
| 0x4d9df8 | g_plr_handbrake | 0/1 |
| 0x4d9dfc | g_plr_turbo_active | 0/1 |
| 0x4d9e3c | g_player_turbo | turbo "fuel" 0..100 (100 if TURBO bought, else 0) |
| 0x4d9e40 | g_plr_fuel | set to 100 in plr_init 0x427fd0, never read: **there is no fuel consumption** |
| 0x4a63e0 | g_plr_gear | current gear index 0..4 (0..5 with the 6-speed box) |
| 0x4a63d8 | g_plr_rpm | engine rpm (float, only for the engine sound) |
| 0x4a63dc | g_plr_skid | skid/tyre-squeal intensity int 0..10 (skid marks, squeal sound, smoke) |
| 0x4a63cc | g_plr_brake_light | 1 while braking (brake light textures, 0x41b560) |
| 0x4a63e4 | g_plr_stop_latch | byte: braked to a stop (see 5.2) |
| 0x4a6480 | g_hb_time | handbrake slide time 0..1 |
| 0x4a6484 | g_hb_rev_time | time handbrake+full gas held at standstill (burn-out) |
| 0x4a6488 | g_hb_speed | |v| during a handbrake slide |
| 0x4a6490 | g_hb_heading | heading at slide start (written only) |
| 0x4a6494 | g_hb_sliding | byte, written only |
| 0x4a6498 | g_plr_forced_brake | forced handbrake latch for game events 3/6/8 |
| 0x4d9e68 | g_plr_vmax | theoretical top speed from the car-type table (sub_40ce10), used by the old model |
| 0x4d9e30 | g_player_damage | 0..100 |
| 0x4d9e34 | g_player_speed_kmh | int |
| 0x4d9e70 | g_plr_progress | chunks driven (signed) |
| 0x4d9ec0/c4/c8/cc/d0/d4 | g_upg_motor / banden / turbo / abs / versnellingsbak / injectie | Game.ini [PLAYER] upgrades |
| 0x4d9ab0 | g_difficulty | [OPTIONS] Difficulty 0 easy / 1 hard |
| 0x4d9b1c | g_dif_mod | [OPTIONS] dif_mod 0..9 |
| 0x4d9aa8 | g_collision_off | [OPTIONS] collisionoff |
| 0x4d9af0 | g_opt_stuur_model | 0 new, 1 old |
| 0x4d9e64 | g_plr_autopilot | player car is driven by the AI (`handle_basic_ai`) |

### 3.3 CARS.DAT (1480 bytes) -> `g_cars_dat` 0x4a5ee0 and `g_cars_body` 0x4a5df0

Read by `init_player_car` 0x4256a0 (`fread(0x4a5ee0, 0x7c, 10)`, `fread(0x4a5df0, 0x18, 10)`), written back by the
debug tweak menu (`car_tweak_menu` 0x412e90, raser.ini [helpdesk] tweak_mode=1, key Tab). Field names are those of
the tweak menu (Dutch). Index = car type 0..6 (7..9 are zero). Used by the **new model** and by the AI racers.

```c
struct CarsDat {               // 124 bytes, 0x4a5ee0 + type*0x7c
    float accel[2][6];         // +0x00 "Acceleratie constanten" per gear; [0] = 5-speed box, [1] = 6-speed box
    float vmax[2][6];          // +0x30 "Max. snelheid" per gear, m/s; [0][5] = 0 (unused)
    float shift_up;            // +0x60 "Schakelpunt vooruit"   (ratio v/vmax)
    float shift_down;          // +0x64 "Schakelpunt achteruit"
    float unused68;            // +0x68 (0)
    float brake;               // +0x6c "Remkracht" (negative)
    float engine_brake;        // +0x70 "Afremmen motor" / "Motor vertraging" (m/s^2)
    float roll_gain;           // +0x74 "Rolhoek versterking"
    float steer;               // +0x78 "Draaihoek"
};
struct CarsBody {              // 24 bytes, 0x4a5df0 + type*0x18
    float roll_damp, roll_factor;     // "Rolhoek demping / factor"
    float pitch_damp, pitch_factor;   // "Pitchhoek demping / factor"
    float height_damp, height_factor; // "Hoogte demping / factor"
};
```

| car | accel[0] (gears 1-5) | vmax[0] m/s | vmax[1] (6-speed) top | up | down | brake | eng.br | roll gain | steer | body (rd,rf,pd,pf,hd,hf) |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 Brabant (Trabant) | .25 .25 .2 .15 .15 | 5.556 11.11 19.44 27.78 41.89 | 45.89 | 0.7 | 0.4 | -0.3 | 3 | 1.6 | 9 | 3 20 3 20 3 20 |
| 1 Opol Astro | .25 .25 .2 .15 .15 | 5.556 11.11 20.83 33.33 45.97 | 50.04 | 0.6 | 0.1 | -0.5 | 3 | 0.9 | 8 | 3 30 3 30 3 30 |
| 2 Wolfswagen | .25 .25 .2 .2 .15 | 6.994 12.5 22.22 36.11 50 | 54.17 | 0.8 | 0.2 | -0.2 | 2 | 1 | 9 | 3 20 3 20 3 20 |
| 3 BWM Z3.5 | .3 .25 .2 .2 .15 | 6.944 12.5 22.22 38.89 54.22 | 58.33 | 0.8 | 0.2 | -0.2 | 2 | 1 | 12 | 3 20 3 20 3 30 |
| 4 PARCHE 913 | .3 .25 .25 .2 .15 | 8.333 13.89 25 41.67 58.33 | 62.72 | 0.8 | 0.2 | -0.2 | 3.5 | 1 | 12 | 3 20 3 20 3 30 |
| 5 Mersedes SLK GTR | .3 .25 .25 .2 .15 | 9.722 16.67 27.78 44.44 62.61 | 66.87 | 0.3 | 0.1 | -0.2 | 2 | 1 | 10 | 3.5 30 3 20 3 40 |
| 6 Lambdaghini | .3 .25 .25 .2 .2 | 11.11 19.44 30.56 47.22 66.67 | 70.94 | 0.8 | 0.2 | -0.5 | 2 | 0.1 | 12 | 2 30 3 15 5 40 |

accel[1] (6-speed): car 0 `.35 .3 .25 .25 .25 .2`, 1 `.35 .3 .25 .25 .2 .2`, 2 `.35 .35 .3 .25 .2 .2`,
3 `.35 .3 .3 .25 .2 .2`, 4 `.35 .3 .3 .25 .25 .25`, 5 `.35 .3 .25 .25 .25 .25`, 6 `.4 .35 .35 .3 .25 .25`;
vmax[1] gears 1-5 equal vmax[0] (car 0: 41.67 instead of 41.89, car 2: 6.944 instead of 6.994).

The top gear vmax x 3.6 is exactly the menu's speed figure (garage.ini `carN_perc1` 150..240 km/h). garage.ini
percentages are **not read by the race** (menu bars only, see MENU.md); the race uses CARS.DAT and the static
table below.

### 3.4 Static car-type table `g_car_types` 0x484b18 (16 x 0xEE bytes, .data)

16 types: RACER_1..7 (player cars 0..6), MPOLIZEI (7), ADAC (8), traffic N_GOLF, N_ASTRA, N_BUS, N_VRTAUT, N_POLO,
N_BESTEL, N_CALIBR (9..15). Used by the **old model** (torque/tyre physics), by `sub_40ce10` (theoretical top speed),
by the turbo (+0xda) and by the AI/traffic code. Values of record 0 (Trabant) / 6 (Diablo) given as examples:

| Off | Addr (type 0) | Meaning (old model; see section 7) | T0 | T6 |
|---|---|---|---|---|
| +0x00 | 0x484b18 | char anim_name[18] "RACER_1_ANIM" | | |
| +0x12 | 0x484b2a | int 100 (280 for type 4) | 100 | 100 |
| +0x16 | 0x484b2e | mass kg | 600 | 1050 |
| +0x1a | 0x484b32 | length m | 4.5 | 4.5 |
| +0x1e | 0x484b36 | width m | 1.7 | 1.7 |
| +0x22 | 0x484b3a | CG to front axle m | 1.01 | 1.33 |
| +0x26 | 0x484b3e | CG to rear axle m | 1.01 | 1.33 |
| +0x2a | 0x484b42 | CG height? | 0.4 | 0.4 |
| +0x2e | 0x484b46 | drag coefficient Cd | 0.38 | 0.33 |
| +0x32 | 0x484b4a | frontal area m^2 | 1.8 | 1.7 |
| +0x36/+0x3a | | 13 / 13 (5..13) | 13 | 5 |
| +0x3e | 0x484b56 | g = 9.81 | | |
| +0x42 | 0x484b5a | air density 1.2 | | |
| +0x46 | | 0.001 | | |
| +0x4a/+0x4e/+0x52 | | front tyre (B, C, D-like) 10.7 1.11 0.59 | | 11.3 1.11 2.95 |
| +0x56/+0x5a/+0x5e | | rear tyre 10.7 1.11 0.65 | | 11.3 1.11 3.5 |
| +0x62 | | rolling resistance 0.02 | | |
| +0x66 | 0x484b7e | wheel radius m | 0.303 | 0.333 |
| +0x6a | 0x484b82 | max rpm | 9000 | 9000 |
| +0x6e | 0x484b86 | max torque Nm | 45 | 580 |
| +0x72 | 0x484b8a | rpm of max torque | 3500 | 5200 |
| +0x76 | | int 2 | | |
| +0x7a | 0x484b92 | gearbox A: float ratio[7] (ratio[0] = 0, -1 = no gear), +0x96 final drive, +0x9a, +0x9e, +0xa2, int +0xa6 | 0,4,2.3,1.3,1,-1,-1 / 5 | |
| +0xaa | 0x484bc2 | gearbox B (6-speed upgrade), same layout | 0,3.55,1.96,1.3,0.95,0.76,-1 / 5.49 | |
| +0xda | 0x484bf2 | turbo multiplier: 1.0 (2.0 for type 6), set to 6.0 while the turbo is active | 1 | 2 |
| +0xde/+0xe2/+0xe6 | | 1.5 / 0.75..2.25 / 1.2 | | |
| +0xea | | int 31..36 | | |

### 3.5 Upgrades, difficulty

`plr_apply_upgrades` 0x428400 (called from init_player_car, modifies the static table in memory):

```c
CarType *t = &g_car_types[g_plr_car];
g_plr_abs_old = 0;                    // 0x4a63c8 (old model ABS flag)
t->turbo_mul = 1.0f;                  // +0xda
if (g_upg_motor)  { t->max_torque *= 1.3; t->torque_rpm *= 1.1; t->max_rpm *= 1.1; }      // +0x6e +0x72 +0x6a
if (g_upg_banden) { t->tyreF_B *= 1.2; t->tyreF_D *= 1.2; t->tyreR_B *= 1.2; t->tyreR_D *= 1.2; } // +0x4a +0x52 +0x56 +0x5e
if (g_upg_abs)    g_plr_abs_old = 1;
if (g_upg_gearbox) copy gearbox B (+0xaa..+0xce) over gearbox A (+0x7a..+0x9e)   // ratios, final drive, +0x9a, +0x9e
if (g_upg_turbo)  g_player_turbo = 100;
g_turbo_saved = t->turbo_mul;         // 0x4d9cf0
```

New model upgrade effects (inline in the code): MOTOR: vmax of every gear + 4.0 m/s (`car_gear_accel` 0x40e9f0);
VERSNELLINGSBAK: uses `accel[1]`/`vmax[1]` and allows gear index 5; BANDEN: lateral damping 10 instead of 5 and no
x0.8 lateral factor (5.3); ABS: brake -0.5 extra (5.2); TURBO: 100 turbo charges (5.5). INJECTIE (0x4d9ed4) is read but
only used in the AI difficulty formula.

Difficulty: the player physics does not depend on Difficulty or dif_mod. `ai_apply_difficulty` 0x4108d0 (for each of
the 3 racers in 0x4a55d0, stride 0x60): `up = clamp(g_plr_car + motor + gearbox + injectie - racer.car, 0, 3)`,
`racer+0x2c = up * 4.0` (extra vmax for the AI), and **multiplies the CARS.DAT vmax[] of the racer's car type** by
`{0.6,0.7,0.8,0.9,1.0,1.1,1.2,1.3,1.4,1.5}[dif_mod]` (5 or 6 gears). **[quirk]** if a racer uses the same car type
as the player, the player's vmax is scaled too (dif_mod=4 -> 1.0, harmless by default). Difficulty (0x4d9ab0) also
selects `g_4a63d0 = (difficulty == 0)` (old model), the prize money (0x4280c0) and the damage handling (section 8).

## 4. Player update

### 4.1 init_player_car 0x4256a0 (plr_car.cpp)

```c
if (g_copland) { g_plr_car = 6; *(int*)0x4850ba = 0x85; }       // police car
g_plr_obj = add_car(g_plr_car, start_chunk(0x4d9a88) - g_start_pos(0x4d9e60), 4, direction ? 0 : 1, 1,
                    handle_player_car /*0x425e20*/, plr_collision_cb /*0x427c70*/, 1);   // 0x410bc0
set_camera_car(...);  obj_set_substep_object(g_plr_obj);       // 0x441e70: g_substep_obj 0x4e65d8 = player
g_plr_steer = 30.0f;  g_plr_gas = 20.0f;  g_6e6c = 0;
plr_apply_upgrades();                                          // 0x428400
fread CARS.DAT (10 x 124, 10 x 24)
g_plr_vmax = car_calc_vmax(g_plr_obj);                         // 0x40ce10 (old model, solves drag = drive force)
engine sound samples 0x17 and (motor ? 0x55 : 0x18)
k = (100 - SENSITIVITY)*3/100 + 1;  for i in 0..30: g_steer_curve[i] = ftol(30*pow((exp(i/30.0)-1)*0.581977, k));
plr_init_lights();                                             // 0x425500
```

### 4.2 handle_player_car 0x425e20 (object update callback, returns 1)

```c
int handle_player_car(int o)
{
    Obj *c = &objs[o];
    if (c->chunk != c->prev_chunk)                              // progress in chunks (for the race order)
        g_plr_progress += (chunk_moved_forward(c->chunk, c->prev_chunk) /*0x407660*/ == 1 ? 1 : -1)
                          * rceng_chunk_distance(c->chunk, c->prev_chunk);
    g_player_speed_kmh = ftol(player->vz * 3.6f);
    g_player_chunk(0x4d9e44) = c->chunk;  g_plr_pos(0x4d9e48..50) = inst->pos;
    obj_clear_forces(c);                                        // 0x43fb90
    plr_engine_sound();                                         // 0x427070, 9.1
    brake lights on/off from g_plr_brake_light (0x41b560 / 0x42b100)
    plr_turbo_tick();                                           // 0x428090: empty 10 s timer
    damage sounds: damage >= 65 -> loop sample 37, damage >= 75 -> loop sample 38 (table 0x48b440)
    rceng_check_chunk_triggers(o);                              // 0x442590 road trigger handlers
    if (g_plr_autopilot) {                                      // AI drives the player car (after finish etc.)
        handle_basic_ai(g_plr_obj); car_update_wheels(g_plr_obj);
        inst->y = car_road_height_reset(g_plr_obj);             // 0x40ede0
        c->vx = c->vy = 0; g_plr_rpm = 0; g_plr_skid = 0; inst->roll = 0;
        return 1;
    }
    if (!g_race_started /*0x4d9e58*/) {                         // countdown: car frozen
        g_plr_rpm = 0; g_plr_skid = 0; c->vx = c->vy = c->vz = 0; c->yawrate = 0; return 1;
    }
    if (!g_player_control_enabled /*0x4d9a9c*/) return 1;
    switch (g_input_device) { 1: plr_input_keyboard(o); 2: plr_input_joypad(o); 3: plr_input_analog(o);
                              4: plr_input_ffjoy(o); }          // INPUT.md 5.x (turbo is handled in there)
    gfx_sky_set_heading(COS[ftol(inst->yaw) & 1023]);           // 0x41eac0
    switch (g_input_device) { 1,2: g_steer_k=3, g_steer_min=5, g_steer_max=25;   // old-model constants
                              3,4: g_steer_k=3, g_steer_min=3, g_steer_max=20; }
    if (g_opt_stuur_model == 0)
        car_drive_new(o, g_plr_steer, g_plr_gas, (float)g_plr_brake);          // 0x40fc30, section 5
    else {
        float E = pow(e, -(abs(ftol(c->vz)) * g_steer_k / g_plr_vmax));          // note: |int(v)|
        float angle = (g_plr_steer - 30) * ((g_steer_max - g_steer_min) * E + g_steer_min) * 0.000581776f; // deg/30 -> rad
        float accel = (g_plr_gas - 20) * 0.05f;                                  // -1..+1
        debug print "%3.3f" angle;
        car_drive_old(o, angle, accel);                                          // 0x40cfd0, section 7
    }
    if (g_input_device == 4 && g_ff_available) ff_update_springs();             // INPUT.md 7.3
    car_update_wheels(o);                                                        // 0x413900, 6.5
    return 1;
}
```

## 5. New steering model (stuur_model = 0, default) - `car_drive_new` 0x40fc30

Inputs from the keyboard (INPUT.md 5.2): gas ramps up at 70/s to 100 (0 when released); brake is an int that grows by
`ftol(brake + 40*dt)` **[quirk: stays 0 above 40 fps]**; steering moves at `200*2*(1-|s|/100)` per second with a
+-15 kick from centre, snaps to 0 when reversing direction, and self-centres at 400/s.

### 5.1 Entry, airborne test

```c
void car_drive_new(int o, float steer, float gas, float brake)
{
    if (g_plr_turbo_active) gas = 100.0f;
    Inst *in = inst_of(o);
    if (in->yaw < 0) in->yaw += 4096;  if (in->yaw >= 4096) in->yaw -= 4096;  // [quirk] 4096, angles are 1024/turn
    // forced braking for game events 3, 6, 8 (busted / finished / game over)
    if (g_plr_handbrake == 1 && g_plr_forced_brake == 1 && ev != 8 && ev != 6 && ev != 3)
        g_plr_handbrake = 0, g_plr_forced_brake = 0;
    if (ev == 8 || ev == 6 || ev == 3) { g_plr_handbrake = 1; g_plr_gas = 0; gas = 0; g_plr_forced_brake = 1; }

    float h = chunk_height_at(chunk(c->chunk), in->x, in->z);   // 0x442f00, see 6.4
    if (in->y - 5.0f <= h) {                                    // on the ground (within 0.5 m)
        car_engine_new(o, gas, brake);                           // 0x40f0d0, 5.2
        car_steer_new(o, steer, gas);                            // 0x40f3c0, 5.3
        if (g_plr_gear <= 1 && gas > 80) g_plr_skid = ftol(gas * 0.1f);   // wheel spin at the start
    } else {                                                     // airborne
        c->vx -= dt * (c->vx * 5);
        c->yawrate -= dt * (c->yawrate * 10);
        g_plr_rpm = 4000 + 60 * gas;                             // free revving
    }
    car_body_roll(o);    // 0x40ebd0  \
    car_body_pitch(o);   // 0x40ece0   > 5.6 (shared with the AI cars)
    car_body_height(o);  // 0x40ee80  /
    if (key_down(VK_TAB) && g_tweak_mode == 1) car_tweak_menu();   // 0x412e90
}
```

### 5.2 Engine, gears, brakes - `car_engine_new` 0x40f0d0 and `car_gear_accel` 0x40e9f0

```c
float car_gear_accel(int o, int gear, float *ratio)      // 0x40e9f0
{
    CarsDat *d = &g_cars_dat[g_plr_car];  int box = g_upg_gearbox;
    float vmax = d->vmax[box][gear] + (g_upg_motor ? 4.0f : 0.0f);
    *ratio = c->vz / vmax;
    float acc = (1.0f - *ratio) * d->accel[box][gear];
    g_plr_rpm = 800 + fabsf(*ratio) * 8200;
    if (g_plr_turbo_active) {
        if (c->vz > 0) c->pitch_vel(+0x78) += 500 * dt;     // nose lifts
        return acc + 0.05f;
    }
    return acc;
}

void car_engine_new(int o, float gas, float brake)        // 0x40f0d0
{
    int maxgear = 4 + (g_upg_gearbox != 0);
    CarsDat *d = &g_cars_dat[g_plr_car];
    if (gas == 0) {                                        // engine braking toward 0
        if (c->vz < 0) { c->vz += d->engine_brake * dt; if (c->vz > 0) c->vz = 0; }
        else           { c->vz -= d->engine_brake * dt; if (c->vz < 0) c->vz = 0; }
    }
    if (gas > 0) g_plr_stop_latch = 0;
    float v0 = c->vz, ratio;
    float acc = car_gear_accel(o, g_plr_gear, &ratio);
    if (c->vz > 1.0f) {                                    // automatic gearbox
        if (ratio > d->shift_up && g_plr_gear < maxgear)  acc = car_gear_accel(o, ++g_plr_gear, &ratio);
        if (ratio < d->shift_down && g_plr_gear > 0)      acc = car_gear_accel(o, --g_plr_gear, &ratio);
    }
    float brk = d->brake;  if (brk > 0) brk = -brk;        // "Remkracht" made negative
    if (g_upg_abs) brk -= 0.5f;
    if (g_plr_handbrake) { acc = 0; brk = 0; }
    c->vz = v0 + dt * (acc * gas + brk * brake);           // gas, brake 0..100
    if (brake > 0) {
        g_plr_brake_light = 1;
        if (fabs(c->vz) < 0.4 && !g_plr_stop_latch) { c->vz = 0; g_plr_stop_latch = 1; }
        float r = c->vz * brake;  if (r > 1000) r = 1000;
        c->pitch_vel(+0x78) -= dt * r * 0.5f;               // nose dive
    } else g_plr_brake_light = 0;
    if (c->vz <= 0) { g_plr_brake_light = 0; g_plr_rpm = 800; }
    if (c->vz < -13.333f) c->vz = -13.333f;                // reverse limited to 48 km/h
}
```

Notes:
* Braking at standstill continues into **reverse** (the latch only zeroes v once); the reverse speed limit is 13.333 m/s.
* The shift-down test uses the ratio of the new gear, so right after an up-shift the box shifts back if
  `v/vmax[g+1] < shift_down`; effectively gear g is left at `v >= max(shift_up*vmax[g], shift_down*vmax[g+1])`.
* With full gas the speed approaches the top-gear vmax exponentially; no air drag in the new model. Validated with
  `tools/physsim.py`: top speeds 150.8/165.5/180/195.2/210/225.4/240 km/h = garage.ini; MOTOR and 6-speed each add
  ~15 km/h (179.6..269.8 with both); 0-100 km/h 2.9..4.3 s (30 fps, no upgrades), 2.1..3.1 s with MOTOR+6-speed.

### 5.3 Steering, grip, drift, handbrake - `car_steer_new` 0x40f3c0

```c
void car_steer_new(int o, float steer, float gas)
{
    Obj *c = &objs[o];  CarsDat *d = &g_cars_dat[g_plr_car];  CarsBody *b;
    // (1) yaw-rate damping
    if (!g_plr_handbrake && g_hb_rev_time == 0) c->yawrate -= c->yawrate * 10 * dt;

    // (2) handbrake + gas > 80 at standstill: burn-out wiggle
    if (g_plr_handbrake && c->vz == 0 && gas > 80) {
        float s = sin(g_hb_rev_time * 6.28319f);
        float w = (g_hb_rev_time > 4) ? s * 0.4f : g_hb_rev_time * 0.1f * s;
        g_hb_rev_time += dt;
        c->yawrate += w;
    } else {
    // (3) release: launch
        if (g_hb_rev_time != 0 && gas > 0) {
            float v2 = d->vmax[g_upg_gearbox][1];                         // 2nd gear vmax (no MOTOR bonus)
            c->vz = g_hb_rev_time * 0.25f * v2 * gas * 0.01f;
            if (c->vz > v2) c->vz = v2;
        }
        g_hb_rev_time = 0;
    }
    // (4) lateral grip
    if (!g_plr_handbrake) {
        g_648c = 0; g_hb_heading = 0; g_hb_time = 0;
        c->vx -= c->vx * (g_upg_banden ? 10 : 5) * dt;
    }
    // (5) steering yaw increment
    float k = (fabs(c->vz) < 2 && fabs(c->vx) < 2) ? fabs(sin(fabs(3.14159f * c->vz * 0.25f))) * d->steer
                                                    : d->steer;
    float a = steer * 0.5 * 6.28319f * 0.00277778f;           // steer/2 degrees in radians
    wrap a into [0, 2pi) (if a > 2pi a -= 2pi; else if a < 0 a += 2pi)
    float dyaw = (k * dt * steer + sin(a) * (c->vz * 50) * dt) * 0.00153398f;   // 0.00153398 = 2pi/4096
    float dyaw_w = dyaw wrapped into [0, 2pi) the same way;
    float heading = (float)(abs-mod(ftol(inst->yaw), 0xfff), +0x1000 if negative) * (1/4096.f) * 6.28319f;

    if (g_plr_handbrake || g_hb_time != 0) goto handbrake;    // (7)

    // (6) normal driving
    if (g_hb_sliding && fabs(c->vx) < 3) g_hb_sliding = 0;
    float grip = 10 - fabs(c->vz) * 0.15;  if (grip < 2.5f) grip = 2.5f;
    if (dyaw_w > 0 && dyaw_w < 6.28319f) {                    // steering
        g_plr_skid = min(ftol(fabs(sin(dyaw_w) * 20)), 10);
        if (c->vz < 10 && g_plr_skid != 0) {
            g_plr_skid = 5;
            c->yawrate += (c->vz < 0) ? grip * dyaw : -grip * dyaw;
            c->vz -= c->vz * 2 * dt;                          // slow-speed turning drag
            inst->roll += dt * d->roll_gain * steer;
        } else {
            c->yawrate += (c->vz < 0) ? grip * dyaw : -grip * dyaw;
            if (fabs(steer) > 20) c->vz -= (0.25f - c->vz * 0.003f) * c->vz * dt;   // cornering drag
            inst->roll -= dt * d->roll_gain * steer;          // [quirk] opposite sign to the slow branch
        }
    } else {                                                  // dyaw exactly 0
        g_plr_skid = 0;
        if (c->vz < 0) c->yawrate += c->vz * -0.05f * grip * dyaw;  else c->yawrate -= grip * dyaw;
        inst->roll += dt * d->roll_gain * steer;
    }
    if (c->vz < 1) g_plr_skid = 0;
    {   float r = c->yawrate * dt;                            // slip: rotate only the lateral component
        float vx = c->vx * cos(r) - c->vz * sin(r);
        c->vx = g_upg_banden ? vx : vx * 0.8f;                // [quirk] x0.8 per frame without the tyre upgrade
    }
    return;

handbrake:                                                    // (7) 0x40fa08
    g_hb_sliding = 1;
    if (g_hb_heading == 0) g_hb_heading = heading;
    g_hb_time += dt;  if (g_hb_time > 1) g_hb_time = 1;
    g_hb_speed = sqrt(c->vx*c->vx + c->vz*c->vz);
    if (g_hb_speed > 1) {
        float w = sin(g_hb_speed * 3.14159f * 0.004f) * (g_hb_time * steer) * 0.27f * 0.5f;
        if (!(c->vz < 0 && g_plr_stop_latch)) w = -w;
        c->yawrate = w;                                      // set, not added
        if (steer != 0) { c->vx -= c->vx * dt;      c->vz -= c->vz * dt; }
        else            { c->vx -= c->vx * 2 * dt;  c->vz -= c->vz * 2 * dt; }
        g_plr_skid = min(ftol(g_hb_speed * 2), 10);
    } else {
        if (g_hb_rev_time == 0) g_plr_skid = 0;
        c->vx = 0; c->vz = 0; c->yawrate = 0;
    }
}
```

How it behaves: the yaw rate is a first-order lag (`-10*w*dt` per frame) driven by `-grip*dyaw`; steady state
`w = -grip * 2pi/4096 * (steer*k + 50*v*sin(steer/2 deg)) / 10` rad/s (negative steer = left gives positive w).
Grip falls from 10 at standstill to 2.5 at >= 50 m/s. Drift: each frame the lateral velocity gets
`-vz*sin(w*dt)` and is then damped (5/s or 10/s plus the x0.8 per frame without BANDEN); in addition the integrator
rotates the velocity by the *truncated* integer yaw change (6.2), so part of the heading change is not followed by
the velocity and shows up as slip.

### 5.4 Other inputs that reach the new model

* `plr_input_analog` (DEVICE 3) feeds steer = 2x-100, gas/brake from the Y axis (INPUT.md 5.3).
* **[quirk]** `plr_input_ffjoy` (DEVICE 4) always writes old-model ranges (steer 0..60, gas 0..40) even when
  stuur_model = 0 (INPUT.md 5.4).

### 5.5 Turbo - `plr_handle_turbo` 0x425cf0 (called by the input readers with the turbo key state)

```c
void plr_handle_turbo(int o, int pressed)
{
    if (pressed) {
        DWORD now = GetTickCount();
        if (now - g_turbo_last_tick < 100) return;           // keeps the current state
        g_turbo_last_tick = now;
        if (g_player_turbo > 0) {
            if ((g_turbo_snd == -1 || !sound_playing(g_turbo_snd)) && !g_turbo_snd_on)
                { g_turbo_snd = sfx_play(0x54); g_turbo_snd_on = 1; }
            g_player_turbo--;                                // 100 charges x 0.1 s = 10 s per race
            CarType *t = &g_car_types[g_plr_car];
            if (t->turbo_mul != 6.0f) g_turbo_saved = t->turbo_mul;
            t->turbo_mul = 6.0f;
            g_plr_turbo_active = 1;
            return;
        }
    } else {
        if (g_turbo_snd != -1 && sound_playing(g_turbo_snd)) sound_stop(g_turbo_snd);
        g_turbo_snd_on = 0;
    }
    g_plr_turbo_active = 0;
    g_car_types[g_plr_car].turbo_mul = g_turbo_saved;
}
```

New-model turbo effect: gas forced to 100, `acc + 0.05` (= +5 m/s^2 at full gas), nose lift. The turbo does not raise
the vmax, so it only helps acceleration. `turbo_mul` (+0xda) is used by the old model. There is no refill during a
race (`plr_turbo_tick` 0x428090 only restarts a 10 s timer); `plr_get/set_turbo` 0x428050/0x428060 clamp 0..100.
There is no fuel.

### 5.6 Body motion (visual suspension) - shared with the AI cars

All three use the **player's** CarsBody record `g_cars_body[g_plr_car]`, also when called for AI cars **[quirk]**.

```c
void car_body_roll(int o)          // 0x40ebd0   roll angle = inst+0x7c, roll velocity = obj+0x80
{   float acc = -(b->roll_damp * c->roll_vel) - b->roll_factor * inst->roll;
    c->roll_vel += dt * acc;  inst->roll += dt * c->roll_vel;
    if (rand() % 10 < 5) c->roll_vel += 4 * c->vz * dt; else c->roll_vel -= 4 * c->vz * dt;   // road vibration
}
void car_body_pitch(int o)         // 0x40ece0   pitch angle = inst+0x74, pitch velocity = obj+0x78
{   float acc = -(b->pitch_damp * c->pitch_vel) - b->roll_factor * inst->pitch;   // [quirk] roll_factor, not pitch_factor
    c->pitch_vel += dt * acc;  inst->pitch += dt * c->pitch_vel;
    same random +-4*vz*dt kick on c->pitch_vel;
}
void car_body_height(int o)        // 0x40ee80   y = inst+0x54, vy = obj+0x70
{   vec3 p = inst->pos, v = c->vel;  int i = ftol(inst->yaw) & 1023;
    float px = p.x + 10*dt*(COS[i]*v.x - SIN[i]*v.z);         // probe point ahead (sic: COS*vx - SIN*vz ...)
    float pz = p.z + 10*dt*((SIN[i] + COS[i]) * v.z);          // [quirk] (SIN+COS)*vz
    int ch = road_find_chunk(&g_road, c->chunk, px, p.y, pz);   // 0x445a10
    float h = chunk_height_at(chunk(ch), px, pz);              // 0x442f00
    c->vy += dt * (-(b->height_damp * c->vy) - (p.y - h) * b->height_factor);
    inst->y += dt * c->vy;
    if (p.y < h - 0.5f) { inst->y = h - 0.5f; c->vy = -c->vy; }        // tests use the old y
    if (h + 2.0f < p.y) { if (o == g_plr_obj) g_plr_skid = 0; c->vy -= 10 * dt; }   // > 0.2 m up: gravity
}
```

So the car's height follows the road surface with a spring/damper; pitch and roll of the body are pure visual springs
around 0 (the slope comes from the chunk orientation matrix, 3.1).

## 6. Integration, road following, wheels

### 6.1 obj_move_all 0x440ea0 (obj_main.cpp)

```c
float dt0 = dt;
for each slot {used, o} in g_physics_list (0x4e4988..0x4e592c, 8 bytes):
    Obj *c = &objs[o];
    if (!c->active || !used_flag[o] || c->type != 1) continue;
    c->vx = ftol(c->vx * 10000) * 0.0001f;  c->vz = ftol(c->vz * 10000) * 0.0001f;   // quantise
    int n; float h;
    if (o == g_substep_obj /*player*/) { h = dt0 * 0.125f; n = 8; } else { h = dt0; n = 1; }
    if (chunk in window || c->always_update) {          // window = rceng_handle_objects' chunk range
        rceng_remove_object_in_waypoint_obstacle_list(o);
        for (k = 0; k < n; k++) {
            int other = 0; float t = 1.0f; vec3 resp; int hit = 0;
            if (g_collision_enabled /*0x4e65d0*/) {
                t = coll_find_first_hit(o, &other, h);      // 0x44a0f0, section 8
                if (g_collision_enabled && coll_response(o, h, &resp, t, &other))   // 0x441490
                    hit = 1;
            }
            obj_integrate(o, h, t, other, hit, &resp);      // 0x43f900 -> 0x4404a0
            inst_build_matrix(inst);                        // 0x459360
            bbox_update(); if (bbox_test()) {               // 0x4497f0, 0x4499e0
                if (c->coll_cb && !c->in_collision) c->coll_cb(o);
                c->in_collision = 1; (also flags two other objects 0x4e6768 / 0x4e676c)
            } else c->in_collision = 0;
        }
        rceng_update_object_chunk(o);                       // 0x442020: prev = chunk; chunk = road_find_chunk(...)
        rceng_add_object_in_waypoint_obstacle_list(o, o == player);
        rceng_set_object_to_chunk_orientation(o);           // 0x442390
    }
    (second pass with the same body for objects in the second chunk range when the window wraps)
```

### 6.2 obj_integrate 0x4404a0 (thiscall on the object; `obj_integrate_idx` 0x43f900 clamps dt < 0 to 0)

```c
void obj_integrate(Obj *c, float dt, float t, int coll, int use_resp, vec3 *resp)
{
    int iy = ftol(floor(inst->yaw));
    vec3 d = use_resp ? (*resp * 10) * dt                    // collision response displacement
                      : (c->vel * t * 10) * dt;              // local displacement in world units (x10)
    int i = iy & 1023;
    inst->z += ftol((SIN[i]*d.x + COS[i]*d.z) * 10000) * 0.0001f;
    inst->x += ftol((COS[i]*d.x - SIN[i]*d.z) * 10000) * 0.0001f;
    // y is NOT integrated here (only car_body_height moves it)
    float dyaw = dt * c->yawrate * 1024 / 6.28319f;
    if (coll == 0) inst->yaw += dyaw;                        // no yaw change while hitting something
    if (!c->no_vel_rotate /*+0x38*/) {                       // player: rotate the velocity by -dyaw
        int j = ftol(-dyaw) & 1023;                          // [quirk] truncated to whole 1024ths of a turn
        float vx = c->vx, vz = c->vz;
        c->vx = COS[j]*vx - SIN[j]*vz;
        c->vz = SIN[j]*vx + COS[j]*vz;
    }
    c->vel += c->inv_mass * c->force * dt;                   // +0x60 * +0x84..+0x8c
    c->yawrate += c->torque * c->inv_inertia * dt;           // +0x90 * +0x64
}
```

With 8 sub-steps of dt/8 the per-step yaw change of a car turning at 1.7 rad/s at 30 fps is ~1.2 units, which the
truncation makes 1 (or 0 at lower rates): the velocity vector lags the body and the car drifts.

### 6.3 What physics reads from the road

* `g_road_chunk_order` (0x4e5d2c) maps chunk numbers to chunk records (`g_chunks` 0x4e65e0, 0x184 bytes).
* `chunk_height_at(ch, x, z)` 0x442f00 (thiscall, ret 8): the chunk is **one plane** n.p = d:
  `return (ch->d(+0xdc) - ch->n.x(+0x3c)*x - ch->n.z(+0x44)*z) / ch->n.y(+0x40);`
* `road_find_chunk` 0x445a10 (chunk the point has passed), `rceng_chunk_distance` 0x442700 (circular).
* The chunk rotation matrix (+0x9c) gives the car body its slope (visual; `rceng_set_object_to_chunk_orientation`).
* Chunk triggers (+0xec/+0xf0 -> `g_trigger_handlers`) fire when the chunk changes (0x442590).
* Collisions with rails and roadside: bounding boxes built from chunk rails (`bbox_add_rail` 0x449c10), section 8.

### 6.4 Height reset `car_road_height_reset` 0x40ede0

`h = chunk_height_at(chunk, x, z); if (h - 1 > inst->y) { inst->y = h - 1; c->vy = -c->vy; } return h;`
The autopilot branch of handle_player_car then stores the returned h into `inst->y`.

### 6.5 Wheels `car_update_wheels` 0x413900 (car_whl.cpp, player only)

```c
float spin = dt * (1024*10 / (3.14159f*7)) * c->vz;            // wheel diameter 0.7 m: 465.6 units per metre
float lim = 1024 * 0.497213f;  if (spin > lim) spin = lim;      // < half a turn per frame (no backwards aliasing)
for w in 0..3: wheel[w]->pitch -= spin;
wheel[0]->yaw = wheel[1]->yaw = 1024 * (1/12.f) * -0.01f * g_plr_steer;   // +-100 -> -+30 degrees (front wheels)
float d = chunk_height_at(chunk, x, z) - inst->y;  clamp d to [-1.05, 1.05];
for w in 0..3: wheel[w]->y = d + 3.5f;
```
(AI cars use `sub_413be0`.) Wheel meshes 'WIEL_ANIM'.

## 7. Old steering model (stuur_model = 1) - `car_drive_old` 0x40cfd0

(see below; filled in from the decoding of 0x40cfd0..0x40e9f0)

## 8. Collisions and damage

(see below)

## 9. Sound and effect hooks driven by the physics

### 9.1 Engine sound `plr_engine_sound` 0x427070
Two looping samples (0x17 idle, 0x18 or 0x55 with MOTOR):
```c
int r = ftol(g_plr_rpm);
sample1: freq = clamp(24000 - r, 11000, 24000); vol = clamp(-(r/8), -2000, -100) - master - 200;
sample2: freq = min(10000 + ftol(3*rpm), 40000); vol = clamp(r/2 - 2000, -2000, -100) - master - 200;
```
### 9.2 Tyre squeal / smoke `tyre_smoke_emitter` 0x425a90 -> 0x4259a0
If `g_plr_skid >= 4`: loop sample 0x27 with `vol = clamp((skid-11)*100, -1100, -100) - master - 100`,
`freq = clamp(skid*700 + 15000, 15000, 28000)`; if skid < 4 the sample is stopped. Every 100 ms (GetTickCount)
while skid >= 4, `0x425a90` emits two smoke puffs (`0x41fff0`, gfx_wolk.cpp) at the rear wheels: local offsets
x = +-9 (x0.6 velocity-dependent drift added), y = +3, z = -10 * car_types[t].cg_rear(+0x26), rotated by the yaw
(table index `ftol(yaw) & 1023`). Skid marks: `gfx_skid 0x41dd00(player, g_plr_skid)` every frame.

## 10. Open questions
