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
| angles of 3D instances (+0x74 pitch, +0x78 yaw, +0x7c roll) | **4096 units per turn** at run time: `init_main` passes 0x1000 to `d3dlib_init` 0x44c7b0 -> `math3d_init_tables` 0x450280, which overwrites `c_angle_units` 0x49b014 (1024.0f in the file) with 4096.0 and the mask 0x49b018 with 4095, and builds 4096-entry SIN/COS tables. Wherever this document says 1024 for an angle, read the run-time value 4096 (`ANGLE_UNITS` in the engine) |
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
   * then `obj_physics_step` 0x440ea0: integrates and collides every object of the physics list (6.1).
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
| +0x00 | int | physics type (1 = dynamic, integrated by obj_physics_step) |
| +0x04 | int | ? (0) |
| +0x08 | int | index of the 3D instance (`*(Inst**)0x4ea9e0`, stride **0x99** = idx*17*9) |
| +0x0c | int | AI / car-list index (-1) |
| +0x10/+0x14 | int | |
| +0x18 | int | current road chunk |
| +0x1c | int | previous road chunk |
| +0x20 | int | waypoint (driver-map) index for obstacle lists |
| +0x24 | int | -1 |
| +0x28 | int | lane/side |
| +0x2c | int | 1 |
| +0x34 | int | active (1) |
| +0x38 | int | on_track: AI drives it along its path; 1 = velocity turns with the car and rails are not collected. 0 (player, crashed AI) = velocity counter-rotated by the yaw step, rail boxes collected |
| +0x3c | int | in-collision flag (set by obj_physics_step) |
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

3D instance (stride 0x99, accessed as `base + idx*17*8 + idx*17`) fields used by physics: +0x10 4x4 matrix (built by 0x459360: roll, pitch, yaw, then
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
    g_player_chunk(0x4d9e44) = c->chunk;  g_player_pos(0x4d9e48..50) = inst->pos;
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

### 6.1 obj_physics_step 0x440ea0 (obj_main.cpp)

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
            int rotblk = 0; float t = 1.0f; vec3 resp; int hit = 0;
            if (g_collision_enabled /*0x4e65d0*/) {
                t = bbox_sweep_object(o, &rotblk, h);       // 0x44a0f0, section 8.3
                if (g_collision_enabled && obj_slide_out(o, h, &resp, t, &rotblk))   // 0x441490, 8.4
                    hit = 1;
            }
            obj_integrate(o, h, t, rotblk, hit, &resp);     // 0x43f900 -> 0x4404a0
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
void obj_integrate(Obj *c, float dt, float t, int rotblk, int use_resp, vec3 *resp)
{
    int iy = ftol(floor(inst->yaw));
    vec3 d = use_resp ? (*resp * 10) * dt                    // collision response displacement
                      : (c->vel * t * 10) * dt;              // local displacement in world units (x10)
    int i = iy & 1023;
    inst->z += ftol((SIN[i]*d.x + COS[i]*d.z) * 10000) * 0.0001f;
    inst->x += ftol((COS[i]*d.x - SIN[i]*d.z) * 10000) * 0.0001f;
    // y is NOT integrated here (only car_body_height moves it)
    float dyaw = dt * c->yawrate * 1024 / 6.28319f;
    if (!rotblk) inst->yaw += dyaw;                          // rotation blocked by an overlap (8.3)
    if (!c->on_track /*+0x38*/) {                       // player: rotate the velocity by -dyaw
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

This is the force-based "Autobahn Raser I" model: axle loads, a simplified Pacejka lateral force, a torque curve with
an automatic gearbox, drag and rolling resistance. It sums the forces into the object's force/torque accumulators,
which `obj_integrate` applies (6.2). It is called from handle_player_car with
`angle = (steer-30)*((max-min)*exp(-3*|int v|/g_plr_vmax) + min)*0.000581776` (rad) and `accel = (gas-20)*0.05`
(-1..1); see 4.2 and INPUT.md 5.

General properties:
- **Player only.** It reads `g_plr_car` 0x4d9e2c and the player globals, never the type of the object it was given.
- It has its own gear variable `g_old_gear` 0x4a63d4 (0..6, 0 = neutral), separate from `g_plr_gear`. It is never
  reset (bss 0), so the race starts in neutral.
- Units: forces in N, velocities in m/s, yaw rate in rad/s. `T` = `&g_car_types[g_plr_car]` (3.4).
- **[quirk]** The old model never calls car_body_roll/pitch/height (5.6). The player's `inst->y` is therefore never
  moved to the road height (no height spring) and the body roll is written absolutely (7.2).
- **[quirk]** The axle loads end up in `F.y` (-m*g every frame), so `vy` drifts down by g*dt per frame. Nothing
  integrates y, so it only matters through the collision response overwriting vy.

### 7.1 Car-type fields used (offsets in the 0xEE record)

| Off | Name | T0 Trabant | Use |
|---|---|---|---|
| +0x16 | mass | 600 | axle loads |
| +0x22 / +0x26 | cg_front `a` / cg_rear `b` | 1.01 / 1.01 | loads; wheelbase `L = a+b` |
| +0x2a | cg_height `h` | 0.4 | load transfer (pitch is always 0) |
| +0x2e / +0x32 / +0x42 | Cd / frontal area / air density | 0.38 / 1.8 / 1.2 | drag, vmax |
| +0x36 | old_roll_max | 13 | visual body roll (+0x3a unused) |
| +0x3e | g | 9.81 | |
| +0x46 / +0x62 | rollres_k / rollres_f0 | 0.001 / 0.02 | rolling resistance |
| +0x4a/+0x4e/+0x52 | front tyre B, C, D | 10.7 1.11 0.59 | `D*N*sin(C*atan(B*alpha))` |
| +0x56/+0x5a/+0x5e | rear tyre B, C, D | 10.7 1.11 0.65 | the same; +0x5e is also the handbrake friction |
| +0x66 | wheel radius `rw` | 0.303 | |
| +0x6a / +0x6e / +0x72 | Nmax / Tmax / N0 (rpm at Tmax) | 9000 / 45 / 3500 | |
| +0x7a | ratio[7] (ratio[0] = 0 neutral, -1 = no gear) | 0,4,2.3,1.3,1,-1,-1 | |
| +0x96 | final drive `fd` | 5 | |
| +0x9a | upshift_k | 1.2 | upshift when rpm > N0*1.2 |
| +0x9e | downshift_k | 0.65 | downshift when rpm < N0*0.65; clutch rpm = (0.65-0.05)*N0 |
| +0xa2 | drive_eff | 1 | multiplies the drive force |
| +0xa6 | front_drive_frac (**float**, 0 or 1) | 0 (T1, T2 = 1) | front/rear split of the drive force |
| +0xda | turbo_mul | 1 (6 with turbo on) | multiplies the drive force |
| +0xde | traction_k | 1.5 | wheelspin limit `traction_k*0.8*|N|` |
| +0xe2 | brake_k | 0.75 | brake force `brake_k*accel*|N|` |
| +0xe6 | lat_grip_k | 1.2 | multiplies both lateral tyre forces |

+0x9a..+0xa6 belong to the gearbox block, so the 6-speed box B (+0xaa) brings its own copies (T0: +0xca 1.1, +0xce
0.7, +0xd2 1, +0xd6 0); `plr_apply_upgrades` copies only up to +0x9e (3.5).

Data constants 0x484ab0..0x484ac8: `g_old_mu` 0.8, 0x484ab4 0.4 (x10, ftol -> 4), 0x484ab8 int 4, `g_old_v50` 13.89
(50 km/h), `g_old_vrev_min` -13.89, `g_old_hb_mu_lat` 0.8, `g_old_hb_mu_lon` 0.8.

### 7.2 Main function

```c
void car_drive_old(int o, float delta /*steer rad*/, float accel /*-1..1*/)   // 0x40cfd0
{
    Obj *c = &objs[o];
    float L = T->cg_front + T->cg_rear;
    vec3 Pf = {0,0, 0.5f*L}, Pr = {0,0,-0.5f*L}, Pcg = {0,0,0};     // force points: axle centres, CG
    vec3 Nf={0}, Nr={0};              // axle base force (load in .y)
    vec3 RRf={0}, RRr={0};            // rolling resistance (.z)
    vec3 LatF={0}, LatR={0};          // lateral tyre force (.x)
    vec3 DrF={0}, DrR={0};            // drive or brake force (.z)
    vec3 Drag={0};
    float alphaF=0, alphaR=0, vlatF=0, vlatR=0;
    float vz = c->vz, r = c->yawrate;

    timer_frame_dt(&g_game_timer);                                  // result discarded (no side effects)
    g_plr_skid = 0;
    car_old_axle_loads(o, 0.0f, &Nf, &Nr);                          // 0x40d5d0

    if (fabs(vz) <= 0.05 && fabs(r) <= 0.01) {                      // standing still
        if (g_plr_handbrake == 0) { c->vx = c->vx - c->vx; c->yawrate = c->yawrate - c->yawrate; g_plr_skid = 0; }
        // slip angles and lateral forces stay 0; the smoothing globals of 7.5 are not updated
    } else {
        car_old_slip_angles(o, delta, r, &alphaF, &alphaR, &vlatF, &vlatR);                // 0x40e3d0
        car_old_lateral_forces(o, &Nf, &Nr, delta, alphaF, alphaR, &LatF, &LatR);         // 0x40e530
    }
    vz = c->vz;                                                     // reloaded
    Drag.z = T->rho * T->Cd * T->area * vz * vz * (vz > 0 ? -0.5 : 0.5);
    car_old_rolling_resistance(o, &Nf, &Nr, &RRf, &RRr);            // 0x40d6e0
    car_old_drivetrain(o, accel, &Nf, &Nr, &DrF, &DrR);             // 0x40dd00
    if (g_plr_handbrake == 1)
        car_old_handbrake(o, alphaR, &Nr, &DrR, &LatR, vlatR);      // 0x40e870: replaces DrR.z and LatR.x
    vec3 Ff = Nf + RRf + LatF + DrF, Fr = Nr + RRr + LatR + DrR;    // car_old_sum_axle_forces 0x40e7f0

    /* visual body roll, absolute (overwrites inst->roll every frame) */
    float ay = c->yawrate * c->vz;                                  // lateral acceleration
    if (fabs(ay) > 30) ay = (ay < 0 ? -1 : 1) * 30;
    inst(c)->roll = ay * (1/30.f) * T->old_roll_max * 11.377778f;   // 11.3778 = 1024/90; [quirk] up to ~52 deg

    obj_add_force_at(c, Ff,   Pf);       // 0x4407e0: F += f; torque += P.x*f.z - P.z*f.x
    obj_add_force_at(c, Fr,   Pr);
    obj_add_force_at(c, Drag, Pcg);
}
```
- The yaw torque comes only from the lateral forces: `tau = -(L/2)*LatF.x + (L/2)*LatR.x`. The force points sit at
  +-L/2, not at a/b, but every player car has a = b.
- The force accumulators are cleared by `obj_clear_forces` at the start of handle_player_car (4.2).

### 7.3 `car_old_axle_loads` 0x40d5d0 (idx, float theta, vec3 *f, vec3 *r)
```c
W = mass*g;  L = a+b;
f->y = -(W*(b*cos(th) - h*sin(th)) / L);      r->y = -(W*(a*cos(th) + h*sin(th)) / L);
f->z = r->z = -0.5f*W*sin(th);                 // th is always 0 -> -0.0; x is never written
```

### 7.4 `car_old_slip_angles` 0x40e3d0 (idx, delta, r, *alphaF, *alphaR, *vlatF, *vlatR)
```c
vx = c->vx; vz = c->vz;  L = a+b;
*vlatF = vx - 0.5f*L*r;          // lateral velocity at the front axle
*vlatR = vx + 0.5f*L*r;          // at the rear axle
if (fabs(vz) != 0.0) { *alphaF = delta - atan(*vlatF / vz); *alphaR = -atan(*vlatR / vz); }
else                 { *alphaF = 0; *alphaR = 0; }
if (vz < 0) { *alphaF = -*alphaF; *alphaR = -*alphaR; }
if (c->vz > 0.2) {                                   // body slip angle -> squeal
    beta = -atan2(c->vx, c->vz);  if (!(fabs(beta) < 0.3)) beta = 0.3f;
    g_plr_skid = ftol(fabs(beta) * 3.33333333 * 10.0);   // 0..10
} else g_plr_skid = 0;
```
`atan` is `fpatan` with x = 1 on the pre-divided ratio, so it stays in (-pi/2, pi/2).

### 7.5 `car_old_lateral_forces` 0x40e530 (idx, *Nf, *Nr, delta, alphaF, alphaR, *LatF, *LatR)
```c
LatF->x = -Nf->y * T->tyreF_D * T->lat_grip_k * sin(T->tyreF_C * atan(T->tyreF_B * alphaF));
LatR->x = -Nr->y * T->tyreR_D * T->lat_grip_k * sin(T->tyreR_C * atan(T->tyreR_B * alphaR));

if (g_plr_easy_old /*0x4a63d0 = difficulty==0*/ && g_plr_handbrake == 0) {
    g_steer_min_deg = 7.0f;                    // dead write: handle_player_car resets it before every call
    c->vx = 0;                                 // easy mode: lateral velocity killed every frame
}
float s = fabs(c->vz * (1/7.f)) * 3.0;
if (fabs(c->vz) < 7.0) {                       // ---- below 25 km/h: kinematic yaw, per frame
    LatF->x = 0; LatR->x = 0;
    if (delta == 0) c->yawrate *= 0.5;                                     // per frame
    else {
        if (s > fabs(c->yawrate)) {
            float d = delta;
            if (!(c->vz > 0) && g_plr_brake_light /*previous frame*/) d = -delta;
            c->yawrate -= c->vz * (1/7.f) * (d < 0 ? -1 : 1) * 0.3f;          // per frame
        } else
            c->yawrate -= (c->yawrate < 0 ? -1 : 1) * 0.1f;                   // per frame
        g_plr_skid = 0;
    }
    c->vx = c->vx - c->vx;                     // no side slip at low speed
} else {                                       // ---- a force sign flip with a big jump kills side slip
    if ((!(LatF->x / g_old_prev_latF >= 0) && fabs(LatF->x - g_old_prev_latF) > 2000) ||
        (!(LatR->x / g_old_prev_latR >= 0) && fabs(LatR->x - g_old_prev_latR) > 2000))
        { c->vx = c->vx - c->vx; g_plr_skid = 0; }
}
LatF->x = (LatF->x + g_old_prev_latF) * 0.5f;   g_old_prev_latF /*0x4a63c4*/ = LatF->x;   // per-frame smoothing
LatR->x = (LatR->x + g_old_prev_latR) * 0.5f;   g_old_prev_latR /*0x4a5d4c*/ = LatR->x;
```
- The sign test is `fcomp 0; test ah,1`, which is also true for NaN. The first frame divides by prev = 0 (+-inf or
  NaN), so a port must use `!(ratio >= 0)`.
- The front test short-circuits the rear test.
- `g_plr_brake_light` is computed later in the frame (7.7), so this function sees the previous frame's value.

### 7.6 `car_old_rolling_resistance` 0x40d6e0 (idx, *Nf, *Nr, *RRf, *RRr)
```c
vz = c->vz;  f0 = T->rollres_f0;  k = T->rollres_k;
if (vz >= 0.1f)       { q = f0 + (k*vz)*(k*vz); RRf->z =  Nf->y*q;  RRr->z =  Nr->y*q; }   // Ny < 0 -> backwards
else if (vz <= -0.1f) { q = f0 + (k*vz)*(k*vz); RRf->z = -(Nf->y*q); RRr->z = -(Nr->y*q); }
```
**[quirk]** The speed term `(k*v)^2 = 1e-6*v^2` was probably meant to be `k*v^2`.

### 7.7 `car_old_drivetrain` 0x40dd00 (idx, accel, *Nf, *Nr, *outF, *outR)
```c
float v = c->vz; if (fabs(c->vz) < 0.1) v = 0;
int coast;
if (accel == 0) { g_plr_brake_light = 0; coast = 1; }
else            { g_plr_brake_light = (v / accel < 0); coast = 0; }      // "braking" = accel against the motion
car_old_gearbox(o, accel, coast, g_plr_brake_light);                      // 0x40e190
float rpm = g_plr_rpm, Tq; bool engine_brake;
if (coast) engine_brake = true;
else if (fabs(T->max_rpm * accel) < rpm) engine_brake = true;               // throttle = rpm governor
else if ((c->vz > g_plr_vmax || c->vz < -13.89f) && !g_plr_turbo_active) engine_brake = true;  // speed limiter
else engine_brake = false;
if (engine_brake && g_old_gear != 0) Tq = -0.5f * T->max_torque;
else { float x = rpm / T->torque_rpm; Tq = T->max_torque * x * (2 - x); }  // parabola, 0 at 2*N0

vec3 A={0}, B={0}, C={0}, D={0};
if (v > 0) {
    if (g_plr_brake_light == 1) { car_old_brakes(o, accel, Nf, Nr, &C, &D); *outF = C; *outR = D; return; }
    car_old_drive_force(o, accel, Tq, Nf, Nr, &A, &B); *outF = A; *outR = B;
} else if (v == 0) {
    if (accel < 0)      { car_old_drive_force(o, accel, Tq, Nf, Nr, &A, &B); *outF = -A; *outR = -B; }
    else if (accel > 0) { car_old_drive_force(o, accel, Tq, Nf, Nr, &A, &B); *outF = A;  *outR = B;  }
} else /* v < 0 */ {
    if (g_plr_brake_light == 1) { car_old_brakes(o, accel, Nf, Nr, &C, &D); *outF = C; *outR = D; }
    else if (v > -13.89f /*0x484ac0*/) { car_old_drive_force(o, accel, Tq, Nf, Nr, &A, &B); *outF = -A; *outR = -B; }
    // else: reverse limited to 50 km/h, no force
}
```
- The throttle is bang-bang: `car_old_drive_force` ignores accel. accel only sets the rpm ceiling `|accel|*Nmax`;
  above it the engine brakes at -0.5*Tmax.
- There is no reverse gear: reverse uses the current forward ratio and negates the force. Neutral (gear 0) gives no
  force because ratio[0] = 0.

### 7.8 `car_old_gearbox` 0x40e190 (idx, accel, coast, braking): automatic box and rpm
```c
int g = g_old_gear;                                          // 0x4a63d4, 0..6
float K    = T->ratio[1] * T->fd * 9.554140127 / T->rw;     // rpm per m/s in 1st (60/(2*3.14))
float Rcl  = (T->downshift_k - 0.05f) * T->torque_rpm;      // clutch-slip rpm in 1st (T0: 2100)
float Vcl  = Rcl / fabs(K);                                 // speed below which 1st gear slips
#define RPM_OF(g) max(800, fabs(c->vz * T->fd * T->ratio[g] * 9.554140127 / T->rw))
float rpm;
if (g == 0) rpm = (braking == 1 || accel == 0) ? 800 : fabs(T->max_rpm * accel);   // free revving
else if (g == 1 && !(Vcl < fabs(c->vz))) rpm = Rcl;
else rpm = RPM_OF(g);
if (rpm < T->downshift_k * T->torque_rpm) {
    if (g == 1) { if (coast == 1) g = 0; }     // 1 -> neutral only when off the gas
    else if (g > 1) g--;
} else if (rpm > T->upshift_k * T->torque_rpm && g != 6 && T->ratio[g+1] != -1.0) g++;
g_old_gear = g;
/* final rpm (sound and torque) with the new gear, same formulas: */
if (g == 0) g_plr_rpm = (braking == 1 || accel == 0) ? 800 : fabs(T->max_rpm * accel);
else if (g == 1 && !(Vcl < fabs(c->vz))) g_plr_rpm = Rcl;
else g_plr_rpm = RPM_OF(g);
```
- The rpm is not capped at Nmax.
- From neutral the box only shifts up when `|accel|*Nmax > 1.2*N0` (T0: accel > 0.47). With less throttle the car
  stays in neutral and does not move.
- Shifting is decided every frame, also while braking, with no hysteresis timer.

### 7.9 `car_old_drive_force` 0x40da90 (idx, accel /*unused*/, Tq, *Nf, *Nr, *A, *B)
```c
float F = Tq * T->ratio[g_old_gear] * T->fd * T->drive_eff / T->rw;
A->z = T->front_drive_frac * T->turbo_mul * F;   B->z = (1 - T->front_drive_frac) * T->turbo_mul * F;
float Fmax = fabs(T->traction_k * 0.8f * Nf->y);
if (fabs(A->z) > Fmax) {                                   // front wheelspin
    float e = (fabs(A->z) - Fmax) / Fmax;
    if (g_input_device == 1 || g_input_device == 2 || g_input_device == 3)
        A->z = -(T->traction_k*0.8f*Nf->y*(A->z < 0 ? -1 : 1));      // clamp to the traction limit
    g_plr_skid = ftol(min(e,1) * 6);
}
/* the same for the rear with B and Nr */
```
**[quirk]** With DEVICE 4 (force-feedback joystick) the traction clamp is skipped, so wheelspin force is unlimited;
only the squeal still plays. The turbo multiplies the drive force by 6.

### 7.10 `car_old_brakes` 0x40d7d0 (idx, accel, *Nf, *Nr, *C, *D)
```c
C->z = -Nf->y * T->brake_k * accel;  D->z = -Nr->y * T->brake_k * accel;
FmaxF = fabs(T->brake_k*0.8f*Nf->y);
if (fabs(C->z) > FmaxF && !g_plr_abs_old /*0x4a63c8*/) {   // wheel lock at |accel| > 0.8
    e = (fabs(C->z) - FmaxF) * 4 /*int 0x484ab8*/ / FmaxF;
    if (fabs(c->vz) > 13.89f) {
        float vv = (c->vz > 13.89f) ? 13.89f : c->vz;      // [quirk] vz < -13.89 is not clamped
        g_plr_skid = ftol(min(e,1) * (fabs(vv)/13.89f * ftol(0.4f*10) /*= 4*/));
    }
    C->z = -(T->brake_k*0.8f*Nf->y*(C->z < 0 ? -1 : 1));   // clamp to the lock friction
}
/* rear: identical with D and Nr */
```
**[quirk]** A locked wheel only caps the brake force and makes the tyres squeal; lateral grip is not reduced. With
ABS there is no cap at all, so ABS brakes harder (up to `brake_k*g`).

### 7.11 `car_old_handbrake` 0x40e870 (idx, alphaR, *Nr, *DrR, *LatR, vlatR)
```c
float Fl = fabs(0.8f /*0x484ac4*/ * T->tyreR_D * Nr->y);
float Fz = fabs(0.8f /*0x484ac8*/ * T->tyreR_D * Nr->y);
DrR->z  = -(c->vz < 0 ? -1 : 1) * fabs(cos(alphaR) * Fz);       // replaces the drive/brake force
LatR->x = -(vlatR  < 0 ? -1 : 1) * fabs(sin(alphaR) * Fl);      // replaces the Pacejka rear force
if (g_old_prev_hb_lat /*0x4a5dc4*/ != 0 && LatR->x / g_old_prev_hb_lat < 0 && fabs(LatR->x - g_old_prev_hb_lat) > 1000)
    c->vx = c->vx - c->vx;
g_old_prev_hb_lat = LatR->x;
g_plr_skid = (fabs(c->vz) > 13.89f) ? 10 : ftol(fabs(c->vz) / 13.89f * 10.0);
```

### 7.12 `car_calc_vmax` 0x40ce10 -> `g_plr_vmax` 0x4d9e68

init_player_car calls it once, **after** `plr_apply_upgrades` (0x425794 before 0x4257fd), so MOTOR and the 6-speed
box are included. It is cdecl and is passed `g_plr_obj`, but it ignores the argument and uses `g_plr_car`.
```c
float gt;  for (float i = 1; i < 8; i++) if (T->ratio[ftol(i)] != -1.0f) gt = T->ratio[ftol(i)];
     // [quirk] index 7 is +0x96 = the final drive, so gt = fd (T0: 5, not the top gear ratio 1)
float G  = gt * fd * 2.653927813 / rw;            // 60/(2*3.14*3.6): rpm per km/h
float b  = 2 * drive_eff * Tmax * G * fd / N0 * gt / rw;
float a  = -(drive_eff * Tmax * G*G * fd / (N0*N0) * gt / rw + 0.5f * rho * Cd * area);
float d  = b*b + 1200*a;                           // b^2 - 4ac with c = -300 N (fixed resistance)
if (d < 0) return 0;
r1 = (sqrt(d) - b) / (2*a);  r2 = (-b - sqrt(d)) / (2*a);
return (r1 > r2) ? r1 : r2;
```
This solves torque-curve force(v) = drag + 300 N with mixed units (rpm per km/h, drag in m/s); the result is compared
with `c->vz` in m/s. Values without upgrades (with the bug): T0 30.4, T1 51.5, T2 44.9, T3 79.3, T4 86.7, T5 110.3,
T6 86.5. With the real top gear T0 and T1 would give d < 0 and return 0, so a faithful port must keep the bug.
`g_plr_vmax` is used by the old-model speed limiter (7.7) and the steering falloff (4.2).

### 7.13 Frame-rate dependence (keep for a 1:1 port)

- These run once per frame and are not scaled by dt:
  - below 7 m/s: yaw `*0.5`, `-= 0.3*vz/7*sign`, `-= 0.1*sign`
  - the force smoothing `(x+prev)/2`
  - the `vx = 0` kills (low speed, easy mode, force sign flips, standing still)
  - the keyboard steering/gas steps (INPUT.md 5.2)
- The roll angle is set absolutely each frame.
- `g_plr_skid` is last-writer-wins, in this order: 0, slip angle, low-speed/kill reset, wheelspin/brake lock,
  handbrake.

## 8. Collisions and damage

Files: bbox.cpp (0x449a30..0x44b980), obj_phys.cpp (0x448e30..0x4499e0), obj_main.cpp (0x441490), plr_car.cpp
(0x427190..0x427fd0), plr_main.cpp (0x4281f0, 0x428390), obj_dmg.cpp (0x448780..0x448db0). Collisions are 2D
oriented rectangles in the XZ plane; y is ignored. Only **rails** (road edges / guard rails from the .cmp file) and
**objects in the physics list** (cars: player, racers, cops, ADAC, traffic) can be hit.

### 8.0 Conventions

* Rotation `rot(v, a)` (a in 1024ths): `x' = COS(a)*x - SIN(a)*z; z' = SIN(a)*x + COS(a)*z`.
  `vec_rot_y` 0x448e30 (out, vec3 by value, int a) keeps y; `bbox_rot_vec` 0x44ae10 (out, vec3*, a) sets y = 0.
* Point velocity of a body point r (local, metres): `w x r = (-w*r.z, 0, w*r.x)`.
* `round_away(a) = a > 0 ? ceil(a) : floor(a)` (ceil = 0x471890, floor = 0x471790).
* Box/mesh coordinates and positions are world units (1/10 m); lever arms are `point * 0.1`.
* Mesh array `*(Mesh**)0x4ea898`, stride 0xBC: +0x10 bbox corners vec3[8] (corners 0, 1, 4, 5 are used),
  +0x74 bounding radius. An object's mesh is found through the animation tables of its instance
  (`*(int*)(0x4ea420 + 16 * anim_tab[0x4ea4f8][inst.anim].frames[inst.frame])`).

### 8.1 Physics properties of the cars

`obj_init_physics` 0x4403b0 (thiscall: vec3 vel, float w0, float mass, float e, float len, float wid):
```c
c->type = 1;
if (mass < 0.1f) { c->inv_mass = 0; c->inv_inertia = 0; }                 // immovable
else { c->inv_mass = 1/mass; c->inv_inertia = 1 / ((1/12.f) * mass * (len*len + wid*wid)); }
if (e < 0 || e > 1) error("Invalid coef. of restitution");
c->restitution = e; c->vel = vel; c->yawrate = w0;
```
`add_car` 0x410bc0 passes vel 0, w0 0, **e = 0.2**, and mass/len/wid from `g_car_types` (+0x16/+0x1a/+0x1e):
- RACER_1..7: 600, 1070, 1233, 1100, 1320, 1050, 1050 kg
- MPOLIZEI 7500, ADAC 1270
- traffic: N_GOLF 1270, N_ASTRA 1070, N_BUS 15000 (11.5 x 2.5 m), N_VRTAUT 15000, N_POLO 1070, N_BESTEL 1700,
  N_CALIBR 1300
- len x wid is 4.5 x 1.7 except RACER_6 (3.995 x 1.715) and the bus.

### 8.2 Boxes (`g_bboxes` 0x4e6790, 200 x 0x4c bytes, count `g_bbox_count` 0x4ea2f0, rebuilt for every sweep)

```c
struct BBox {               // 0x4c
    vec3  corner[4];        // +0x00 local, axis aligned: c0=(xmin,zmin) c1=(xmax,zmin) c2=(xmax,zmax) c3=(xmin,zmax)
    vec3  centre;           // +0x30 world
    float radius;           // +0x3c mesh radius (0 for rails)
    int   owner;            // +0x40 object index, -1 for rails
    int   angle;            // +0x44 1024ths
    int   is_object;        // +0x48 1 object, 0 rail
};
```
* `bbox_make_own` 0x449de0 (obj, box*) fills one box; `bbox_add_object` 0x449a30 (obj) appends one, skips unused
  objects, and at 200 boxes logs "Max nb of bboxs reached!" but still writes. Both set:
  - corners = mesh.bbox[0], [1], [5], [4]
  - centre = inst.pos, radius = mesh+0x74
  - angle = round_away(inst.yaw), owner = obj, is_object = 1
* `bbox_add_rail` 0x449c10 (chunk, k): rail = `g_rails[chunk.rail_idx[k]]`.
  - Chunk fields: +0xe0 n_rails, +0xf4 int rail_idx[]. Rail fields (stride 0x60): +0 pos, +0x18 yaw, +0x5c
    horizontal length L.
  - Corners (-10,0,-1), (0,0,-1), (0,0,L+1), (-10,0,L+1): a slab 1 m thick on the rail line's -x side.
  - centre = rail.pos, radius 0, owner -1, angle = round_away(rail.yaw), is_object 0.
* `bbox_collect_static` 0x44a010 (obj):
```c
if (!objs[obj].on_track)                                  // player always; AI only while crashed
    for (d = -1; d <= 1; d++) { c = rceng_wrap_chunk(objs[obj].chunk + d);
                                for (k = 0; k < chunk(c).n_rails; k++) bbox_add_rail(c, k); }
for each physics-list entry j != obj:
    if (rceng_chunk_distance(objs[obj].chunk, objs[j].chunk) < 10) bbox_add_object(j);
```

### 8.3 Swept test - `bbox_sweep_object` 0x44a0f0 (obj, int *rotblk, float dt) -> frac

```c
float bbox_sweep_object(int obj, int *rotblk, float dt)
{
    Obj *o = &objs[obj]; float frac = 1; int rb = 0; BBox me;
    collision_clear();  g_bbox_result.hit = 0;                          // 0x4499d0
    if (o->vx == 0 && o->vz == 0 && o->yawrate == 0) { *rotblk = 1; return 0; }
    if (!o->used) { *rotblk = 0; return 1; }
    g_bbox_count = 0;  bbox_collect_static(obj);  bbox_make_own(obj, &me);
    for (each box b in g_bboxes) {
        vec3 m = bbox_rot_vec(o->vel, me.angle - b.angle) * (dt*10);    // my motion in b's frame
        g_bbox_result.hit = 0;
        float t = bbox_sweep(&b, &me, &m);                              // my corners vs b's rectangle
        if (t < frac) { frac = t; collision_record(&b, &me, &g_bbox_result); }
        g_bbox_result.hit = 0;
        vec3 m2 = -(o->vel * (dt*10));                                  // b relative to me, in my frame
        t = bbox_sweep(&me, &b, &m2);                                   // b's corners vs my rectangle
        if (t < frac) { frac = t; collision_record(&me, &b, &g_bbox_result); }
    }
    me.centre += bbox_rot_vec(o->vel, me.angle) * (frac*dt*10);
    me.angle  += round_away(dt*1024*o->yawrate/6.283185f);              // full dt, not frac
    if (o->yawrate != 0)
        for (each box b) {
            g_bbox_result.hit = 0; if (bbox_overlap(&b, &me)) rb = 1;   // (calls the empty 0x4499c0 if no collision yet)
            g_bbox_result.hit = 0; if (bbox_overlap(&me, &b)) rb = 1;
        }
    *rotblk = rb;  return frac;
}
```
- Translation stops at the first contact, so there is no penetration.
- The yaw step is cancelled (rotblk) when the turned box would overlap anything.
- The other body's own motion is not part of m2; only my motion, negated, is used.

`bbox_sweep` 0x44a520 (A, B, motion*) -> t tests B's corners against A's rectangle, in A's frame:
```c
vec3 cA[4] = A.corner;                                                    // axis aligned
vec3 cB[4] = rot(rot(B.corner, B.angle) + (B.centre - A.centre), -A.angle);
if (!bbox_broadphase(cA, cB, motion)) return 1.0f;                        // 0x44ab40
int pt, edge; float t = bbox_clip_corners(cB, cA, *motion, &pt, &edge);   // 0x44b6f0
if (pt != -1) { g_bbox_result.hit = 1; g_bbox_result.point = B.corner[pt] /*raw local corner of B*/;
                g_bbox_result.edge = edge; }
if (t > 1 || t < 0) error("Clip cannot be [%f]");
return t;
```
Helpers:
* `g_bbox_result` 0x4e6678: +0 hit, +4 point vec3, +0x20 edge.
* `bbox_broadphase` 0x44ab40: the AABB (in A's frame) of the 4 cB corners and the 4 corners + motion (initial
  min/max +-9999.9), grown by 1.0 on every side, must overlap [cA0.x, cA1.x] x [cA0.z, cA2.z].
* `bbox_clip_corners` 0x44b6f0 (cB, cA, m, &pt, &edge):
  - for i in 0..3: `t_i = bbox_seg_vs_rect(cB[i], cB[i]+m, cA[0], cA[2], &e_i)`
  - keeps the minimum (strict <, initial 1e8)
  - if min < 1: pt = i, edge = e_i; else pt = -1, edge = 0
  - returns min (1.0 = no hit)
* `bbox_seg_vs_rect` 0x44b480 (P0, P1, Rmin, Rmax, int *edge), Liang-Barsky:
```c
dx = P1.x-P0.x; dz = P1.z-P0.z;  if (dx == 0 && dz == 0) return 1;
e = bbox_point_inside_edge(P0, Rmin, Rmax);                  // 0x44b390
if (e != -1) { if (bbox_point_inside_edge(P1, Rmin, Rmax) == -1) return 1; *edge = e; return 0; }
tE = 0; tL = 1; last = 0;
if (!bbox_lb_clip( dx, Rmin.x - P0.x, &tE, &tL)) return 1;  if (tE != last) { *edge = 3; last = tE; }
if (!bbox_lb_clip(-dx, P0.x - Rmax.x, &tE, &tL)) return 1;  if (tE != last) { *edge = 1; last = tE; }
if (!bbox_lb_clip( dz, Rmin.z - P0.z, &tE, &tL)) return 1;  if (tE != last) { *edge = 0; last = tE; }
if (!bbox_lb_clip(-dz, P0.z - Rmax.z, &tE, &tL)) return 1;  if (tE != last) *edge = 2;
return (tE > 0) ? tE : 1;
```
* `bbox_lb_clip` 0x44b2b0 (p, q, &tE, &tL) returns 1 to accept, 0 to reject:
  - p > 0: r = q/p; reject if r > tL; if r > tE then tE = r
  - p < 0: r = q/p; reject if r < tE; if r < tL then tL = r
  - p == 0: reject if q > 0
* `bbox_point_inside_edge` 0x44b390: d = {z-zmin, xmax-x, zmax-z, x-xmin}. If all > 0, returns the argmin (0x44b360,
  strict <, initial 1e10); otherwise -1. Edge e runs from corner e to corner (e+1)%4: 0 zmin, 1 xmax, 2 zmax, 3 xmin.
* `bbox_overlap` 0x44acf0 (A, B): builds cB in A's frame as above. If any cB corner is strictly inside A's
  rectangle, sets `g_bbox_result = {1, that corner (A frame), edge 0}` and returns 1.
* `bbox_edge_normal` 0x44b860 (out, edge, A, int ang):
  - `d = A.corner[e+1] - A.corner[e]`, `u = d/|d|` (|d| = 0 logs "Division by zero" and continues)
  - `r = rot(u, ang)`, `out = (r.z, 0, -r.x)`
  - The result is the outward normal of A's edge, rotated by ang.

### 8.4 Collision record (`g_collision` 0x4e6760) and slide-out

```c
struct Collision { int active; int type /*0 world, 1 object-object*/; int obj1, obj2; vec3 point; vec3 normal; };

void collision_record(BBox *A /*edge box*/, BBox *B /*corner box*/, Res *res)   // 0x449840
{
    if (!res->hit) return;
    g_collision.active = 1;
    if (!A->is_object) {                       // my corner hit a rail
        if (!B->is_object) error("Two rails cannot collide");
        type = 0; obj1 = B->owner; obj2 = 0; point = res->point;
        bbox_edge_normal(&normal, res->edge, A, A->angle - B->angle);
    } else if (!B->is_object) {                // a rail corner hit my rectangle
        type = 0; obj1 = A->owner; obj2 = 0; point = res->point;      // rail corner, in the rail frame (sic)
        bbox_edge_normal(&normal, res->edge, A, 0); normal = -normal;
    } else {
        type = 1; obj1 = B->owner; obj2 = A->owner; point = res->point;
        bbox_edge_normal(&normal, res->edge, A, A->angle - B->angle);
    }
}
```
- The normal points out of A toward B and is expressed in obj1's local frame.
- The point is a raw local corner of B, in world units.
- Helpers: `collision_clear` 0x4499d0, `collision_active` 0x4499e0.

`obj_slide_out` 0x441490 (obj, dt, vec3 *out, float frac, int *rotblk) only acts when `frac == 0 &&
collision_active()`; otherwise it returns 0:
```c
Collision saved = g_collision; vec3 v0 = o->vel; vec3 n = g_collision.normal;
float vn = dot3(o->vel, n);  if (vn > 0) return 0;          // moving away: integrate with frac 0 (no motion)
o->vel = o->vel + (10 - vn) * n;                            // remove the normal component, push out at 10 m/s
int rb2; float f = bbox_sweep_object(obj, &rb2, dt);  if (rb2 == 1) *rotblk = 1;
*out = o->vel * f;  o->vel = v0;  g_collision = saved;  return 1;
```
When it returns 1, the integrator moves the body by `out*10*dt` (6.2, use_resp). The normal is in obj1's frame,
which is not always the mover's frame.

### 8.5 Impulse response - `obj_resolve_collision` 0x4497f0

It runs after every integration sub-step. Type 0 goes to `obj_world_response` 0x449570, type 1 to
`obj_object_response` 0x448eb0; any other type logs "Unknown collision type". `g_last_impulse` 0x4e6788 holds the J
of the last response.

```c
void obj_world_response(Collision *k)               // 0x449570: car vs rail
{
    Obj *o = &objs[k->obj1]; vec3 n = k->normal;
    vec3 r = k->point * 0.1f;
    vec3 vp = o->vel + (vec3){-o->yawrate*r.z, 0, o->yawrate*r.x};
    float vn = vp.x*n.x + vp.z*n.z;   float kk = r.x*n.z - r.z*n.x;
    float num = -vn * (1 + o->restitution);
    if (o->inv_mass == 0 && o->inv_inertia == 0) return;
    float j = (o->inv_mass != 0) ? num / o->inv_mass : num;   g_last_impulse = j;
    float dw = clamp(0.34f * o->inv_inertia * j * kk, -6, 6);
    o->vel += o->inv_mass * j * n;            // = -(1+e)*vn along n: normal velocity reflected (e = 0.2)
    o->yawrate += dw;
    o->pitch_vel(+0x78) += 16 * o->inv_mass * n.z * j;
    o->roll_vel(+0x80)  -= 16 * o->inv_mass * n.x * j;
    o->vy = o->inv_mass * j * 1.5f;           // overwrites vy: a small hop
}

void obj_object_response(Collision *k)              // 0x448eb0: car vs car
{
    Obj *o1 = &objs[k->obj1], *o2 = &objs[k->obj2];
    int A1 = ftol(inst(o1)->yaw), A2 = ftol(inst(o2)->yaw);       // truncation, not floor
    k->point *= 0.1f;                                            // in place: modifies g_collision
    vec3 Nw = rot(k->normal, A1), r1 = rot(k->point, A1);
    vec3 V1 = rot(o1->vel, A1), V2 = rot(o2->vel, A2);
    vec3 d = (inst(o1)->pos - inst(o2)->pos) * 0.1f;  vec3 r2 = { d.x + r1.x, 0, d.z + r1.z };
    vec3 vp1 = V1 + (vec3){-o1->yawrate*r1.z, 0, o1->yawrate*r1.x};
    vec3 vp2 = V2 + (vec3){-o2->yawrate*r2.z, 0, o2->yawrate*r2.x};
    float vn = dot3(vp1 - vp2, Nw);
    float e = (o1->restitution + o2->restitution) * 0.5f;
    float num = -vn * (1 + e);  float den = o1->inv_mass + o2->inv_mass;
    float J = (den != 0) ? num / den : num;   g_last_impulse = J;
    float K1 = r1.x*Nw.z - r1.z*Nw.x,  K2 = r2.x*Nw.z - r2.z*Nw.x;
    float dw1 = clamp(0.34f*o1->inv_inertia*J*K1, -6, 6), dw2 = clamp(0.34f*o2->inv_inertia*J*K2, -6, 6);
    o1->vel += rot(o1->inv_mass*J*Nw, -A1);   o2->vel -= rot(o2->inv_mass*J*Nw, -A2);
    o1->yawrate += dw1;  o2->yawrate -= dw2;
    o1->pitch_vel += 16*o1->inv_mass*k->normal.z*J;   o1->roll_vel -= 16*o1->inv_mass*k->normal.x*J;
    vec3 Nl2 = rot(Nw, -A2);
    o2->pitch_vel -= 16*o2->inv_mass*Nl2.z*J;          o2->roll_vel += 16*o2->inv_mass*Nl2.x*J;
    o1->vy = o2->inv_mass*J*1.5f;  o2->vy = o2->inv_mass*J*1.5f;   // [quirk] both use o2's inv_mass
}
```
Constants: 0.1 @0x47fbf8, 0.5 @0x47fc00, -1.0 (double) @0x47fc08, 0.34 @0x47fc10, +-6 @0x47fc14/18, -+16
@0x47fc1c/20, 1.5 @0x47fc24. inv_inertia does not appear in the denominator of J.

### 8.6 Order inside one sub-step (see 6.1)

1. `bbox_sweep_object`; if frac == 0, `obj_slide_out`.
2. `obj_integrate`: moves by frac, or by the slide vector; the yaw step is skipped if rotblk.
3. `inst_build_matrix`.
4. `obj_resolve_collision`: the impulse is applied on **every** sub-step that has a contact.
5. If `collision_active()`:
   - call the mover's collision callback (+0x48) **only on the rising edge** (`!o->in_collision`)
   - then set `o->in_collision = 1; objs[g_collision.obj1].in_collision = 1;` and, if type == 1,
     `objs[g_collision.obj2].in_collision = 1;`
6. Otherwise `o->in_collision = 0`.

### 8.7 Collision callbacks, damage, wrecking

**Player:** `plr_collision_cb` 0x427c70 asserts `collision_active()` ("collision not active error"). If
`obj1 == g_plr_obj || obj2 == g_plr_obj` it calls `player_car_collision(g_plr_obj)`. It returns 1.

**Other cars:** `mesh_damage_collision_func` 0x427ad0, returns 1:
- type 1:
  - if obj1's callback is 0x427ad0: `handle_mesh_damage(obj1, &g_collision)`
  - if obj2's callback is 0x427ad0: `invert_collision(&tmp, &g_collision)` 0x427730, then
    `handle_mesh_damage(obj2, &tmp)`
  - then `player_car_collision` if the player is obj1 or obj2
- type 0: `handle_mesh_damage(i, &g_collision)`, then `player_car_collision` if obj1 is the player

`invert_collision` 0x427730 (dst, src): `*dst = *src`, swap obj1/obj2,
`dst.point = rot(rot(src.point*10, ftol(o1.yaw)) + pos1 - pos2, ftol(-o2.yaw)) * 0.1`. The normal is not changed.

```c
void player_car_collision(int p)                     // 0x427de0
{
    if (g_input_device == 4 && g_ff_available && g_ff_bump_x != -1 && g_ff_bump_y != -1) {   // INPUT.md 7.3
        di_effect_set_duration(bump_x/y, 200000); di_effect_set_magnitude(bump_x/y, 10000);
        di_effect_set_direction(x = ftol(-sign(n.x)), z = ftol(sign(n.z))); di_effect_start(both);
    }
    plr_collision_sound();                           // 0x427cc0
    int J = ftol(g_last_impulse), d;
    if (J < 18000) d = 0;
    else { if (J > 35000) J = 35000;  int t = ftol(J * 0.0028571428f);          // J/350: 51..100
           int maxd = (g_difficulty == 1) ? 12 : 6;                              // HARD 12, EASY 6
           d = clamp(ftol(maxd * 0.01f * t), 0, maxd); }                          // HARD 6..12, EASY 3..6
    plr_add_damage(d);              // 0x428390: if (!g_game_event && !g_no_damage) damage = clamp(damage + d, 0, 100)
    player_spawn_crash_sparks();    // 0x4281f0
    g_player_collided(0x4d9e04) = 1;                 // written only
}

void plr_collision_sound(void)                       // 0x427cc0
{
    int J = ftol(g_last_impulse); DWORD now = GetTickCount();
    if (now - g_crash_sound_tick(0x4d9e20) < 300) {
        if (J < 18000 && g_collision.type == 0 && (g_scrape_snd(0x48b468) == -1 || !sound_playing(g_scrape_snd)))
            g_scrape_snd = sfx_play(0x52);           // rail scrape
    } else
        sfx_play(J < 18000 ? tab_48b470[rand()%2] : J < 25000 ? tab_48b478[rand()%2] : tab_48b480[rand()%2]);
    g_crash_sound_tick = now;
}

void player_spawn_crash_sparks(void)                 // 0x4281f0
{
    assert(collision_active());
    vec3 rw = rot(g_collision.point, ftol(inst(player)->yaw));  rw.y = 0;
         // rail hits: point in world units; car hits: already scaled by 0.1 in obj_object_response
    vec3 pos = { g_player_pos.x + rw.x, g_player_pos.y + 3, g_player_pos.z + rw.z };
         // g_player_pos 0x4d9e48 is copied at the start of handle_player_car
    vec3 dir = rw / sqrt(rw.x*rw.x + rw.z*rw.z);
    gfx_sparks_spawn(pos, dir);                      // 0x41f150
}
```
Notes:
- Damage is taken only on the first sub-step of a contact, and only for impulses >= 18000. For example, a 1100 kg
  car hitting a rail has J = 1.2*v_n*m, which reaches 18000 at a normal speed of 13.6 m/s.
- **Wrecking:** in handle_game 0x424be8, `if (plr_get_damage() >= 99 && g_game_event == 0) set_game_event(6)`, which
  ends the race (game over).
- `plr_set_damage` 0x4281c0 clamps to 0..100. Game.ini [PLAYER] damage carries the value between races; it is
  repaired in the garage.

### 8.8 Mesh deformation (dormant in the shipped game)

`handle_mesh_damage` 0x427190 (obj, coll*):
```c
if (o->dmg_mesh(+0x98) == -1) return;                          // always true: no .DMG files exist
if (o->dmg_frame(+0x94) == g_frame_counter(0x4d9ab8)) return;  o->dmg_frame = g_frame_counter;
from mesh[o->dmg_mesh].bbox corners 0,1,5,4: xmax/xmin (c.x > 0 / else), zmax/zmin
px = coll->point.x*10; pz = coll->point.z*10;
wz = px > 0.6*xmax ? 1 : 0; if (px < 0.6*xmin) wz = -1;   hz = pz > 0.6*zmax ? 1 : 0; if (pz < 0.6*zmin) hz = -1;
zone = 10*wz + hz;
d as in player_car_collision (18000/35000/350/difficulty); dmgf = d*0.01f;
region = {-11:5, -10:6, -9:7, -1:4, 1:0, 9:3, 10:2, 11:1}[zone]; amount = dmgf*0.5f;
// zone 0 logs "Impossible collision! (Center of object)", any other zone "Something went really wrong";
// in both cases region = obj and amount = dmgf
if (obj == g_plr_obj) { g_plr_region_dmg[region](0x4d9e74 float[8]) += amount;
     lvl = ftol(obj_damage_levels(o) * g_plr_region_dmg[region]); obj_apply_damage(o, region, lvl);
     g_plr_region_lvl[region](0x4d9e94 int[8]) = lvl; }
else { find racer slot s in g_racer_dmg 0x485b2c[3] (stride 0x58: +0 obj, +0xc float val[8], +0x2c int lvl[8])
       with s.obj == obj, else return; accumulate into s.val / s.lvl the same way }
```
- Regions run clockwise from the front: 0 front, 1 front-right, 2 right, 3 rear-right, 4 rear, 5 rear-left, 6 left,
  7 front-left.
- `obj_apply_damage` 0x43fc40 (region, level) copies the original vertices plus that level's deltas into the
  object's private mesh copy (+0x9c), so each level is absolute, not cumulative.
- `.DMG` file format (dmgdef_load 0x448ba0; the mesh name with its extension replaced by "DMG"):
  `i32 n_levels; n_levels x { i32 ignored; 8 x { i32 count; count x { i32 vertex; f32 dx, dy, dz } } }`.

The whole system is inactive in the shipped game:
- No `.DMG` file exists in RCS.IMG or on the CD, so every `g_dmg_defs[mesh]` is NULL and handle_mesh_damage always
  returns early.
- The player never calls it anyway; its callback is 0x427c70.
- DAMAGE_MESH / DAMAGE_VALUE in Game.ini are only loaded (gm_ini 0x4228b0), saved (0x422ec0) and re-applied at race
  start (0x424e30). **[quirk]** DAMAGE_VALUE is written with %f but read back as int.
- The only damage that matters in play is the 0..100 meter.

### 8.9 collisionoff

Game.ini [OPTIONS] collisionoff -> `g_collision_off` 0x4d9aa8 -> at 0x4241f5, `obj_set_collision_enabled(collisionoff
== 0)` (0x441e80 -> 0x4e65d0). When collisions are off, `bbox_sweep_object` and `obj_slide_out` are never called:
- frac = 1 and rotblk = 0
- no impulses and no callbacks, so no damage, sounds or sparks
- cars pass through rails and through each other

### 8.10 Road edges and AI recovery

The only lateral constraint is the rail slabs of chunks c-1..c+1; there is no road-width clamp. A rail hit:
- reflects the normal velocity with e = 0.2
- kicks the yaw rate (at most +-6 rad/s), the body pitch/roll rates and vy
- gives the player the scrape sound, damage (if J >= 18000) and sparks

AI cars (`g_ai` 0x4a5b40, stride 0x6c: +0x2c crashed, +0x30 crash timer, +0x50 reverse timer, +0x58 stuck counter):
- `ai_fsm` 0x402b40 sets `crashed = 1, on_track = 0` while in_collision. The car becomes a free body and also
  collides with rails. `on_track = 1` is restored 0.5 s after the contact ends.
- `ai_handle_collision` 0x4084b0: after more than 5 frames of continuous contact the car backs away (`vz = -3`, up
  to 5 s), then is put back on its path (0x402520, 0x406bf0).

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

1. **The keyboard brake does not work above 40 fps.** It is an int that grows by `ftol(40*dt)` (INPUT.md 5.2). A port
   probably wants a fixed-step option or a float brake behind a "fix" flag.
2. ~~Yaw is wrapped at +-4096 in `car_drive_new`, but angles are 1024 per turn.~~ Resolved: angles are 4096 per turn at
   run time (section 1), so the wrap and `car_steer_new`'s `&0xfff / 4096` heading are correct.
3. **Roll scale in the old model.** It uses `11.377778 = 1024/90`, which gives up to ~52 degrees of body roll.
   `1024/360` was possibly intended.
4. **The old model has no road-height spring.** It never calls `car_body_height`, so the player's y does not follow
   the road. Needs a check in the real game.
5. **AI cars use the player's CarsBody record** in `car_body_*`. Verify the AI call sites (0x4013c0, 0x409ed0, ...)
   and whether their 0x40ff90/0x410280 drive code matches the new model (AI agent's area).
6. **Sign conventions** (left/right, yaw direction) are derived from the code. They should be confirmed by running
   the game (e.g. under DOSBox-X/Wine with a debugger).
7. **calc_chunk_window** (0x445d70): the meaning of its second window (probably wrap-around at the track end) and of
   the third argument of rceng_handle_objects.
8. **AI recovery helpers:** what sub_4083d0 (recovery steering) and sub_402520 / sub_406bf0 (re-placement on the
   path) do exactly.
9. **Frame-dependent constants:** `0x40fba0` (`car_damp_all`) and `sub_413be0` (AI wheels) were not decoded in
   detail.
