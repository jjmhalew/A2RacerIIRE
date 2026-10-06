# A2 Racer II - AI and computer-controlled vehicles

Scope: everything in `spel.dat` that drives a car other than the player: traffic, the three CPU racers, the police,
the (dormant) ADAC/Wegenwacht car, and the player car while the police has it on "autopilot". Source files
(from the assert strings): `ai_cfun.cpp`, `ai_cops.cpp`, `ai_fsm.cpp`, `ai_lane.cpp`, `ai_main.cpp`, `ai_path.cpp`,
`ai_racer.cpp`, `ai_wmap.cpp`, `Car_cops.cpp`, `ev_cops.cpp`, plus the car creation code in `Car_main.cpp`.
Code range `0x401000-0x40c860`, `0x4108d0-0x411710`, `0x4150f0-0x415990`.

Names: [names/spel_ai.txt](names/spel_ai.txt). Some functions and globals that the AI uses were already named by
other areas (physics, render, road, objects); `spel_ai.txt` does not redefine them, and this document uses the
names below. When a name here differs from the one in the other names file, the address is always given, and
section 10 lists the mapping.

How to read the pseudocode:
- It is transcribed from the disassembly. Every branch, constant and field offset is in it. Float constants are
  given in decimal, with the hex value when the decimal is not exact.
- Debug `printf` calls (`sub_464ca0`, written to a debug log) are left out, but their format strings were used to
  name functions and variables. `fatal()` = `sub_464e50` (prints and continues).
- `dt()` = `sub_441cd0(this = 0x4e4970)`: the smoothed frame time in seconds. It returns 0.1 when the time is 0 or
  above 0.1, so it is never larger than 0.1.
- `rand()` = MSVC `sub_470120` (0..32767).
- Helper macros used in several sections: `OBJ(o)` (object, stride 0xa0), `O3D(o)` (object3d, stride **0x99**),
  `WP(w)` (driver-map waypoint, 0x50), `CHUNK(c)` (chunk record, 0x184), `AI(a)` (t_ai_state, 0x6c),
  `BH(b)` (t_ai_behavior, 0x2c), `COP(c)` (t_cop, 0x20), `fsm[i]` / `E` (t_fsm, 0x60).
- The AI code uses the chunk *number* directly as the chunk record index; it does not go through the
  `g_road_chunk_order` map at 0x4e5d2c. The road code fills that map with the identity, so the result is the
  same, and the pseudocode keeps the direct indexing.

## 1. Overview

There are two layers of AI. Game.ini `[OPTIONS] ai_model` (stored at `g_ai_model`, 0x4d9b10) decides which layer
drives the racers and the cops:

| car | `ai_model = 0` (default, "New (A2 Racer II)") | `ai_model = 1` ("Old (Autobahn Raser I)") |
|---|---|---|
| traffic (car list type 2) | per-object **basic AI**: HANDLE_TRAFFIC_AI 0x40b770 (triggers, change_lane, handle_basic_ai) | same |
| CPU racers (type 3) | **FSM** slots 0..2 (`g_fsm`, 0x4a55a0): ScanPath, world mapper, CalculateStearing and the car-control library. The object handler 0x40ac90 only animates the wheels. | object handler HANDLE_RACER_AI 0x40ac90: basic AI + race_lane + racer triggers (refuelling) + rubber band |
| cops (type 4) | object handler handle_cop_ai 0x402790 runs only the speeding check `ai_cop_check_speeding` 0x409060 and brakes parked cops. Pursuit and arrest run in **FSM** slots 3..14. | handle_cop_ai: 5-state `t_cop` machine (lookout, pursuit, got him, quit) using the basic AI |
| ADAC (type 5) | created at chunk 0 and switched off; never switched on (dormant) | same |
| player during an arrest | `add_ai(player, behaviour 0, ...)`, autopilot flag `0x4d9e64` (behaviour 0 has max speed 0, so the car is braked) | same |

- **Basic AI** (ai_main/ai_lane): a car follows the driver-map waypoint graph (`g_driver_map`, 0x4e6628). It
  steers to the waypoint one step ahead and sets its speed from the waypoint `lookahead` steps ahead.
  It accelerates and brakes with the per-behaviour constants (`g_ai_behaviors`, 16 entries) and respects
  `ai_max_speed_allowed`, which the speed triggers set. It changes lane with change_lane: every lane of the chunk
  is scanned for obstacles and dead ends, then a driver-type specific chooser picks the target lane.
  The car writes `speed` (+0x74) and `steer` (+0x7c) of its object directly; the engine integrates them.
- **FSM** (ai_fsm/ai_cfun/ai_path/ai_wmap): 15 state machines. Transitions come from two tables of
  `{from, to, expected, cond}`. A state handler builds a path with ScanPath (a depth-first search over the waypoint
  graph that avoids occupied waypoints), turns it into steering, throttle and brake inputs (CalculateStearing,
  `ai_speed_control`), and feeds those to the car-control library. That library also drives the player car
  (`0x40ce10..0x410a10`, documented by the physics area). Racer states cover racing, blocking the player,
  rubber-band waiting, being arrested and the finish. Cop states cover chase, target behind, arrest, depart,
  give up and park.

## 2. Per-frame flow

`handle_game` (0x424a70), in this order (AI-relevant parts only):
```c
timer_update(&g_timer);                                    // 0x441bd0, smoothed dt
rceng_handle_objects(OBJ(player).chunk, 2*g_visible_chunks, player);   // 0x440850:
    // for every object in the chunk window with on (+0x34) != 0: call OBJ.handler(+0x44)(obj)
    //   traffic -> HANDLE_TRAFFIC_AI 0x40b770, cops -> handle_cop_ai 0x402790,
    //   racers -> HANDLE_RACER_AI 0x40ac90, player -> 0x425e20 (handle_basic_ai 0x4076b0 for a racer that finished
    //   in the old model); then obj_move_all integrates speed/steer into positions
ai_check_cars_outside_handling_range(player);              // 0x40b2d0: recycle traffic around the player
... cameras, render, check_game_keys, handle_game_actions (0x4168c0: arrest sequence = game action 8), ...
if (g_ai_model == 0) {
    ai_fsm_check_all_transitions();                        // 0x403340 (+ siren light blink 0x40c860)
    ai_fsm_handle_all();                                   // 0x408ed0: state handlers, lap/finish triggers,
                                                           //   player position, racer ranking
}
```
Setup (`game_init` 0x423d20 -> `init_all_cars` 0x410b60): car types, player car, `init_traffic`,
`init_racer_cars`, `ai_fsm_init` (racer and cop transition tables, difficulty modifier, world mappers),
`init_cop_targets`, `init_adac_car`. Cops are created while the track is loaded (`.omp` objects of type 9 ->
0x40c920). `init_ai(n)` (0x405bc0) allocates the t_ai_state pool and fills the behaviour table.

## 3. Shared structures (consolidated)

### Game object (`g_objects` 0x4e48e0, stride 0xa0): fields used by the AI
| off | type | name | notes |
|---|---|---|---|
| 0x04 | int | important | 1 for player, racer and cop (car list types 1, 3, 4) |
| 0x08 | int | o3d | object3d index |
| 0x0c | int | ai | t_ai_state index (-1 = none) |
| 0x14 | int | sample | current engine/siren sound sample (40 = siren psirene1.wav) |
| 0x18 | int | chunk | current chunk number |
| 0x1c | int | prev_chunk | chunk of the previous frame (cops: start chunk) |
| 0x20 | int | waypoint | current driver-map waypoint (obstacle-list registration) |
| 0x24 | int | f24 | = 0x4d9a90 at creation |
| 0x28 | int | side | carriageway 0 / 1 |
| 0x2c | int | visible | |
| 0x34 | int | on | 0 = switched off: no handler call, ignored by respawn checks |
| 0x38 | int | ontrack | AI driving active (cleared during collisions / autopilot) |
| 0x3c | int | collision | collision this frame (engine) |
| 0x40 | int | f40 | 1 for racers and cops |
| 0x44 | fnptr | handler | per-object update callback |
| 0x6c / 0x70 / 0x74 | float | vel_lat / vel_y / speed | speed = forward speed, m/s (60 km/h trigger = 16.67) |
| 0x78 / 0x7c / 0x80 | float | pitch rate / steer = yaw rate / roll rate | basic AI writes the steering into +0x7c |

object3d (`0x4ea9e0`, stride 0x99, packed): +0x10 4x4 matrix (row 2 = forward at +0x30/+0x34/+0x38),
+0x50/+0x54/+0x58 position, +0x74 pitch, +0x78 yaw (4096 units per turn), +0x7c roll.

### Track data read by the AI (in-memory layouts, cf. names/spel_road.txt)
| struct | off | type | name |
|---|---|---|---|
| chunk (`g_chunks` 0x4e65e0, 0x184) | 0x24 | vec3 | reference position (event camera) |
| | 0x3c / 0x40 / 0x44 / 0xdc | float | road plane a, b, c, d (height = (d - a*x - c*z) / b) |
| | 0xe4 / 0xe8 | int | lane count side 0 / side 1 |
| | 0xec / 0xf0 | int | trigger_a / trigger_b (see trigger table) |
| | 0x124 / 0x154 | int[12] | waypoint per lane, side 0 / side 1 (lane 0 = left-most, fastest) |
| waypoint (`g_driver_map` 0x4e6628, 0x50) | 0x00 | int | flags (bit 8 = side-switch point, world mapper) |
| | 0x04 | int | trigger |
| | 0x08 | vec3 | position |
| | 0x14 | int | link: next waypoint, same lane |
| | 0x18 / 0x1c | int | links: next waypoint in the neighbouring lane (lane+1 / lane-1 according to change_lane) |
| | 0x20 | int | link: previous waypoint |
| | 0x24 | int | obstacle count (max 10) |
| | 0x28 | int[10] | objects on this waypoint (rceng_add/remove_object_in_waypoint_obstacle_list) |

### Trigger values (chunk trigger_a/b and waypoint +0x04), all consumers
| value | consumer | effect |
|---|---|---|
| 0 | all | NO_TRIGGER |
| 2 | old racer AI (racer_trigger 0x40b030) | TRIGGER_BENZINE_STATION: stop and refuel (`ai+0x38 = 7`) |
| 4 / 5 / 6 / 8 | basic AI (ai_apply_trigger 0x407ba0) | ai_max_speed_allowed = 16.67 / 22.22 / 33.33 / 500 m/s (60 / 80 / 120 km/h / none) |
| | old cops (reset_cop 0x402200) | cop max speed 16.67 / 22.22 / 33.33 / 2 x player top speed |
| | FSM (ai_fsm_handle_trigger 0x4088f0) | racer speed limit 12.5 / 17.5 / 25 / 30 m/s; a cop catches a racer at speed >= limit and the player at speed > limit (ai_cop_check_speeding 0x409060) |
| 7 | | ignored by the AI; reset_cop reports "ILLEGAL SPEED TRIGGER" |
| 9 | old racer AI | TRIGGER_ZERO_CHUNK_MARKER -> sub_42ae20(obj) |
| 10 | basic AI | traffic-light stop line: when `g_traffic_light_state` (0x48c484) == 0 (red): max speed 0, ai_state 1 |
| 14 | FSM / old racer AI | finish line (lap counted only after 15); on a non-city track (`g_maps[].is_city == 0`) it finishes at once, and cops go to state 9 |
| 15 | FSM / old racer AI | halfway checkpoint / TRIGGER_FINISH_VALID |
| 18..21 | basic AI, cops | 1..4 lanes on side 1 (`ai_max_lanes`) |
| 40..43 | basic AI, cops | 1..4 lanes on side 0 |
| 44 | handle_basic_ai | EINDE_WEG (end of road): the AI returns at once |
| 46 / 47 | old racer AI | TRIGGER_FUEL_APPROACH (half speed, driver type 5) / TRIGGER_FINAL_APPROACH (quarter speed, driver type 1) |
Backtracking (0x408080) walks back from the car's chunk to the most recent speed and lane trigger. When it finds
none, it uses speed 8 and 2 lanes (19 or 41).

### AI tables
- `t_ai_state` (pool "AI_States", data pointer at 0x4a5b40, stride 0x6c), `t_ai_behavior` ("AI Behaviors",
  0x4a5b88, 16 x 0x2c): section 4, "Structures". The complete behaviour table, including the values that depend
  on difficulty, is in section 4, "init_ai".
  - Corrections from cross-checking: `+0x10` (initialised to 10) **is** read by change_lane as the scan range in
    waypoints (it is accessed through a precomputed pointer, which is why the core search missed it).
  - `+0x38` is the change_lane *driver type* of the old racer AI: 1 race, 5 fuel approach, 6 at the fuel point,
    7 refuelling.
  - `+0x4c` is the second argument of `sndfx_play_object_sample` (engine sound on, initialised to 1).
- `t_lane_info` (0x20, per lane, built by change_lane): section 5.
- `t_fsm g_fsm[15]` (0x4a55a0, 0x60): section 6. Slots 0..2 are racers, 3..14 cops. Transition tables are at
  0x4a5410 (racers) and 0x4a5280 (cops).
- World mapper (`g_world_mappers` 0x4a5c08, 8 x 0x18): section 7.
- `t_cop` (pool "Cop States", data pointer at 0x4a5238, 0x20): section 8.
- Car list `g_car_list` 0x4a64a0 `{active, object, type}` (type 1 player, 2 traffic, 3 racer, 4 cop, 5 ADAC),
  racer info `g_racers` 0x485b20 (3 x 0x58: +0 car model, +4 finish time, +8 position (0 = leading),
  +0xc object, +0x10 finished): section 9.

## Contents
- 4 AI core: behaviours, add_ai, handle_basic_ai, calc_speed and steering, triggers, collisions, difficulty.
- 5 Lanes: change_lane, scan_lane, find_obstacle_*, pick_lane / race_lane / cop_lane / adac_lane, adjust_speed.
- 6 FSM: framework, both transition tables, condition functions, triggers, ranking, init.
- 7 Path: world mapper, ScanPath, steering, FSM state handlers, old racer AI, car-control interface.
- 8 Cops: placement, both cop models, siren, arrest sequence, fines (there are none).
- 9 Traffic: car types, car list, traffic generation and recycling, racer creation, ADAC.
- 10 Consolidated open questions.

### Summary of the gameplay rules
- **Traffic.** Hard: 5 cars behind and 5 ahead on the player's carriageway, plus 7 + 7 oncoming. Easy: 2 + 2 and
  3 + 3. The cars are spread 3 chunks apart, starting 5 chunks outside the visible range, and all start switched
  off. Every frame, each car outside the window `player_chunk ± (visiblechunks - 1)` has a 1/30 chance to be
  moved to the opposite edge of the window, in lane 0 of its own carriageway. This only happens if:
  - the frame rate is at least 14.5 fps;
  - the chunk is not single-lane;
  - the target waypoint and its links are empty;
  - the car does not overlap another car.
  
  The model is one of Golf, Astra, Polo, Bestel (van), Calibra (2/12 each), bus or lorry (1/12 each). The
  behaviour is drawn independently: 2 with 7/12, 3 with 5/12. Each traffic car also gets a random top-speed
  factor of 0.9..1.1. Traffic keeps right, overtakes on the left within `ai_max_lanes`, stops for red traffic
  lights and obeys the speed triggers.
- **Racers.** Three cars use the RACER_n models nearest to the player's own model, in grid slots one chunk
  apart. Their behaviours are 7..13 (model 0 -> 7, 1 -> 9, 2 -> 8, 3..6 -> 10..13). For behaviours 8..13 the
  brake, acceleration and top-speed values are multiplied by 0.98..0.88 on hard and 0.96..0.74 on easy.
  Rubber banding, new model:
  - the throttle factor 0x4014e0 depends on the progress gap to the player, the difficulty and the track;
  - FSM state 11 waits for the player when a racer is too far ahead;
  - the race leader blocks a player who is close behind (states 1 and 2).
  
  The old model limits speed by the progress gap instead. Racers can be arrested (4 s stop). Finish times of
  racers that have not finished at race end are extrapolated (0x4114a0).
- **Police** (new model):
  - Cops are parked at fixed places in the `.omp` file. A cop reacts when the player is in its chunk and faster
    than 12.5 / 17.5 / 25 / 30 m/s for speed trigger 4 / 5 / 6 / 8. On city tracks it reacts only with a small
    random chance per frame.
  - At most 1 (easy) or 3 (hard) cops chase the player at the same time, and 2 cops per racer.
  - The arrest happens when the target is slow (|vel_lat| + |speed| < 7) and within about 70.7 world units.
  - The chase is given up after 30 s (easy) or 60 s (hard), or when the cop is more than 50 chunks away.
  - **Arrests do not cost money** (the BOETE texts are unused leftovers). The 1st arrest gives a 4 s stop and
    "EERSTE WAARSCHUWING", the 2nd "LAATSTE WAARSCHUWING", and the 3rd ends the game with "GEARRESTEERD".
- **Difficulty.**
  - `Difficulty` (0x4d9ab0) sets: the racer behaviours, the rubber-band table, the traffic counts, the cop
    limit (1/3) and the cop give-up time (30/60 s). It also changes the value returned by cfun_racer_too_far,
    but no transition uses that value.
  - `dif_mod` (0x4d9b1c, 0..9) multiplies the racers' gear acceleration table by 0.6..1.5 (4 = x1.0).
  - Racer FSM `+0x5c` = 4 x clamp(player car + upgrades - racer car, 0, 3).


## 4. AI core (ai_main.cpp): behaviours, basic driving, triggers, collisions, difficulty


Conventions: `objects[o]` = 0x4e48e0 array (stride 0xa0), `obj3d[i]` = 0x4ea9e0 array (stride **0x99**, packed),
`wps[w]` = g_driver_map 0x4e6628 (stride 0x50), `chunks[c]` = g_chunks 0x4e65e0 (stride 0x184),
`ai[a]` = g_ai_states.data 0x4a5b40 (t_ai_state, stride 0x6c), `bh[b]` = g_ai_behaviors.data 0x4a5b88 (stride 0x2c).
`dt()` = sub_441cd0(ecx=0x4e4970) clamped frame time (float). Debug printf (sub_464ca0) calls omitted.
`fatal()` = sub_464e50.

IMPORTANT: everywhere in this section, chunk triggers are read with `chunks[chunk_number]` directly
(`chunk*0x184`), *not* through the 0x4e5d2c number->record map.

---------------------------------------------------------------------------------------------------------

### T_ARRAY helpers

T_ARRAY object layout (both arrays): `+0x00 T* data; +0x04 int count; +0x08 int last_used; +0x0c int initialised
(0/1); +0x10 char* used_flags; +0x14 char name[25]; +0x2d char type_name[25]`.

#### ai_behaviors_ctor (0x405960) / ai_states_ctor (0x405a90)
Static constructors for the two global T_ARRAYs. Copy the name strings (sub_470240 = strncpy, 0x19 chars), free a
previous allocation if `initialised==1`, zero the object, and register an atexit destructor (sub_4701d0) whose body
directly follows (0x405a30 / 0x405b60: if initialised==1 free(data); if initialised free(used_flags), initialised=0).
```c
// 0x405960: g_ai_behaviors @0x4a5b88, name "AI Behaviors", type "t_ai_behavior"
// 0x405a90: g_ai_states    @0x4a5b40, name "AI_States",    type "t_ai_state"
void tarray_ctor(T_ARRAY *a, const char *name, const char *type) {
    a->initialised = 0;                         // (written before strncpy)
    strncpy(a->name, name, 25); strncpy(a->type_name, type, 25);
    if (a->initialised) { if (a->initialised == 1) free(a->data); if (a->initialised) { free(a->used); a->initialised = 0; } }
    a->data = 0; a->count = 0; a->last_used = 0; a->used = 0; a->initialised = 0;
    atexit(dtor);                               // dtor = 0x405a30 / 0x405b60
}
```
T_ARRAY::get_new (inlined in add_ai and add_cop 0x4026c0):
```c
int tarray_get_new(T_ARRAY *a) {
    if (!a->initialised) fatal(" %s(%d) : T_ARRAY::get_new %s[%d] of type %s not initialized\n", ...);
    for (i = a->last_used; i < a->count; i++) if (!a->used[i]) { a->used[i] = 1; a->last_used = i; return i; }
    for (i = 0; i < a->last_used; i++)        if (!a->used[i]) { a->used[i] = 1; a->last_used = i; return i; }
    fatal(" %s(%d) : T_ARRAY::new array %s[0..%d] of type %s : not enough elements\n", ...);
    return -1;
}
```

#### init_ai (0x405bc0)
`void init_ai(int n_ai_states)` (caller sub_423d20). Allocates `n` t_ai_state (all free) and 16 t_ai_behavior
(all marked used), then fills the behaviour table. Six difficulty factors K0..K5 depend on g_difficulty (0x4d9ab0).
```c
void init_ai(int n) {
    float K0,K1,K2,K3,K4,K5;
    K0=0.98f/*3f7ae148*/; K1=0.96f/*3f75c28f*/; K2=0.94f/*3f70a3d7*/; K3=0.92f/*3f6b851f*/; K4=0.90f/*3f666666*/; K5=0.88f/*3f6147ae*/;
    if (g_difficulty == 0) {     // easy
        K0=0.96f/*3f75c290*/; K1=0.91f/*3f68f5c2*/; K2=0.87f/*3f5eb852*/; K3=0.83f/*3f547ae2*/; K4=0.78f/*3f47ae14*/; K5=0.74f/*3f3d70a4*/;
    }
    // T_ARRAY<t_ai_state> g_ai_states(n)
    if (g_ai_states.initialised) tarray_free(&g_ai_states);        // sub_405910
    if (n < 0) fatal(" %s(%d) : T_ARRAY(construction) illegal array size %s %s[0..%d]\n", "...array.hpp", 0x103, ...);
    g_ai_states.data = malloc(n*0x6c); g_ai_states.count = n; g_ai_states.last_used = 0;
    g_ai_states.used = malloc(n); g_ai_states.initialised = 1; memset(used, 0, n);
    // T_ARRAY<t_ai_behavior> g_ai_behaviors(16)
    if (g_ai_behaviors.initialised) tarray_free(&g_ai_behaviors);
    g_ai_behaviors.data = malloc(0x2c0); count = 16; last_used = 0; used = malloc(16); initialised = 1;
    memset(used, 1, 16);
    /* fill bh[0..15], see table below; steer_gain = C/1024.0f (fdiv by float 1024.0 @0x49b014),
       racer rows 8..13 are base*K computed in x87 (double) and stored as float */
}
```
`bh[b].steer_gain` constants (C, all are N*pi): 0x47f130 13571.68 (4320pi), 0x47f134 11309.73 (3600pi),
0x47f138 6031.858 (1920pi), 0x47f13c 8293.805 (2640pi), 0x47f140 7539.823 (2400pi), 0x47f144 12063.72 (3840pi),
0x47f148 10178.76 (3240pi), 0x47f14c 13948.67 (4440pi), 0x47f150 13194.69 (4200pi), 0x47f17c 13270.09 (4224pi),
0x47f188 13420.88 (4272pi).

Racer rows (difficulty-scaled): `bh[8]  = {7,1.25, 47*K1,  C=4200pi, 4.1*K1, 52*K1, 14.8, 80,75,85,2}`,
`bh[9]  = {.., 49*K0, 4200pi, 4.7*K0, 55*K0, ..}`, `bh[10] = {.., 62*K2, 4200pi, 7.5*K2, 68*K2, ..}`,
`bh[11] = {.., 72*K3, 4224pi, 9.5*K3, 80*K3, ..}`, `bh[12] = {.., 80*K4, 4272pi, 9*K4, 90*K4, ..}`,
`bh[13] = {.., 90*K5, 4320pi, 10.5*K5, 100*K5, ..}`. (Write the hex values below to be bit exact.)

##### Behaviour table (computed by emulating 0x405d56..0x4067f3; values with odd decimals given in hex)

Behaviour use: 0 = player car driven by AI (sub_4258f0: add_ai(player,0,wp,0); max_speed_allowed starts at 0),
1 = unused, 2/3 = traffic (g_traffic_types 0x484a48 only uses 2 and 3), 4/5/6 = unused by any add_ai caller
(4,5 still get the traffic trigger handling since add_ai tests 2..5), 7..13 = CPU racers (init_racer_cars:
racer model 0->7, 1->9, 2->8, 3->10, 4->11, 5->12, 6->13), 14 = cop (MPOLIZEI), 15 = ADAC.

| beh | lookahead +00 | wp_range +04 | brake_speed +08 | steer_gain +0c | accel +10 | top_speed +14 | decel +18 | rear_range +1c | follow_range +20 | front_range +24 | f28 +28 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 player | 6 | 2 | 45 | 13.2536 `41540eb9` | 2 | 50 | 12.5 | 200 | 200 | 300 | 2 |
| 1 | 5 | 2 | 15 | 11.0447 `4130b6ef` | 3.75 | 25 | 14 | 200 | 200 | 300 | 2 |
| 2 traffic | 8 | 1 | 11.5 | 5.89049 `40bc7edd` | 2.55 | 25 | 13 | 200 | 200 | 300 | 2 |
| 3 traffic | 4 | 1.2 | 21 | 8.09942 `41019738` | 3.9 | 25 | 14.5 | 200 | 200 | 300 | 2 |
| 4 | 4 | 1.3 | 25 | 7.36311 `40eb9e95` | 4.8 | 25 | 15.8 | 200 | 200 | 300 | 2 |
| 5 | 5 | 1.25 | 32 | 11.781 `413c7edd` | 9 | 40 | 14 | 200 | 200 | 400 | 2 |
| 6 | 4 | 1.25 | 32 | 9.9402 `411f0b0b` | 6.3 | 40 | 16.2 | 125 | 100 | 150 | 2 |
| 7 racer(m0) | 7 | 1.25 | 33 | 12.8854 `414e2ac2` | 2.5 | 37 | 15 | 80 | 75 | 85 | 2 |
| 8 easy | 7 | 1.25 | 42.77 `422b147a` | 12.8854 `414e2ac2` | 3.731 `406ec8b3` | 47.32 `423d47ae` | 14.8 | 80 | 75 | 85 | 2 |
| 8 hard | 7 | 1.25 | 45.12 `42347ae1` | 12.8854 `414e2ac2` | 3.936 `407be76c` | 49.92 `4247ae14` | 14.8 | 80 | 75 | 85 | 2 |
| 9 easy | 7 | 1.25 | 47.04 | 12.8854 `414e2ac2` | 4.512 `4090624e` | 52.8 `42533334` | 14.8 | 80 | 75 | 85 | 2 |
| 9 hard | 7 | 1.25 | 48.02 | 12.8854 `414e2ac2` | 4.606 `4093645a` | 53.9 `4257999a` | 14.8 | 80 | 75 | 85 | 2 |
| 10 easy | 7 | 1.25 | 53.94 `4257c28f` | 12.8854 `414e2ac2` | 6.525 `40d0cccd` | 59.16 | 14.8 | 80 | 75 | 85 | 2 |
| 10 hard | 7 | 1.25 | 58.28 `42691eb8` | 12.8854 `414e2ac2` | 7.05 | 63.92 `427fae14` | 14.8 | 80 | 75 | 85 | 2 |
| 11 easy | 7 | 1.25 | 59.76 `426f0a3e` | 12.9591 `414f585a` | 7.885 `40fc51ec` | 66.4 `4284cccd` | 14.8 | 80 | 75 | 85 | 2 |
| 11 hard | 7 | 1.25 | 66.24 `42847ae1` | 12.9591 `414f585a` | 8.74 | 73.6 `42933333` | 14.8 | 80 | 75 | 85 | 2 |
| 12 easy | 7 | 1.25 | 62.4 `42799999` | 13.1063 `4151b389` | 7.02 | 70.2 `428c6666` | 14.8 | 80 | 75 | 85 | 2 |
| 12 hard | 7 | 1.25 | 72 | 13.1063 `4151b389` | 8.1 | 81 | 14.8 | 80 | 75 | 85 | 2 |
| 13 easy | 7 | 1.25 | 66.6 `42853333` | 13.2536 `41540eb9` | 7.77 | 74 | 14.8 | 80 | 75 | 85 | 2 |
| 13 hard | 7 | 1.25 | 79.2 `429e6666` | 13.2536 `41540eb9` | 9.24 | 88 | 14.8 | 80 | 75 | 85 | 2 |
| 14 cop | 6 | 1.23 | 58 | 13.6217 `4159f2b0` | 12 | 70 | 18 | 115 | 70 | 80 | 2 |
| 15 ADAC | 4 | 1.2 | 19 | 11.0447 `4130b6ef` | 3.5 | 24 | 15 | 200 | 100 | 120 | 2 |

Bit-exact dwords of the difficulty dependent rows (+0x00..+0x28; `(float)((double)base * (double)K)`):
```
easy 8  00000007 3fa00000 422b147a 414e2ac2 406ec8b3 423d47ae 416ccccd 42a00000 42960000 42aa0000 40000000
easy 9  00000007 3fa00000 423c28f6 414e2ac2 4090624e 42533334 416ccccd 42a00000 42960000 42aa0000 40000000
easy 10 00000007 3fa00000 4257c28f 414e2ac2 40d0cccd 426ca3d7 416ccccd 42a00000 42960000 42aa0000 40000000
easy 11 00000007 3fa00000 426f0a3e 414f585a 40fc51ec 4284cccd 416ccccd 42a00000 42960000 42aa0000 40000000
easy 12 00000007 3fa00000 42799999 4151b389 40e0a3d6 428c6666 416ccccd 42a00000 42960000 42aa0000 40000000
easy 13 00000007 3fa00000 42853333 41540eb9 40f8a3d7 42940000 416ccccd 42a00000 42960000 42aa0000 40000000
hard 8  00000007 3fa00000 42347ae1 414e2ac2 407be76c 4247ae14 416ccccd 42a00000 42960000 42aa0000 40000000
hard 9  00000007 3fa00000 4240147b 414e2ac2 4093645a 4257999a 416ccccd 42a00000 42960000 42aa0000 40000000
hard 10 00000007 3fa00000 42691eb8 414e2ac2 40e1999a 427fae14 416ccccd 42a00000 42960000 42aa0000 40000000
hard 11 00000007 3fa00000 42847ae1 414f585a 410bd70a 42933333 416ccccd 42a00000 42960000 42aa0000 40000000
hard 12 00000007 3fa00000 42900000 4151b389 41019999 42a20000 416ccccd 42a00000 42960000 42aa0000 40000000
hard 13 00000007 3fa00000 429e6666 41540eb9 4113d70a 42b00000 416ccccd 42a00000 42960000 42aa0000 40000000
```
Field +0x28 (2.0 for every behaviour) is never read by any AI code found.

---------------------------------------------------------------------------------------------------------

### add_ai (0x406800)
`int add_ai(int object, int behavior, int waypoint, int lane)` (eax = 0 on return; callers ignore it). Allocates a t_ai_state, links it to the object and initialises it. The traffic behaviours 2..5 take the
current speed/lane triggers by backtracking the road; a cop (14) gets a t_cop state.
NB: callers push `lane` before computing `waypoint = rceng_get_waypoint_index(chunk, lane, side)`, hence 4 args.
```c
void add_ai(int object, int behavior, int waypoint, int lane) {
    int speed_trig = 8, lane_trig;            // locals [esp+0x14], [esp+0x10] (lane_trig uninitialised if side bad)
    if (g_finish_chunk /*0x483850*/ == -1) g_finish_chunk = trigger_find_start_position();   // sub_429a20
    int a = tarray_get_new(&g_ai_states);     // inlined
    t_ai_state *s = &ai[a];
    s->speed_factor = 1.0f;                                     // +0x5c
    OBJ *o = &objects[object];
    if (o->side != 1 && o->side != 0)
        fatal("%s(%d): obj_side(%d) = %d is illegal!.\n", "C:\\work\\A2racII\\Game\\ai_main.CPP", 0x276, object, o->side);
    if (o->side == 1) lane_trig = 0x12;
    if (o->side == 0) lane_trig = 0x28;
    s->behavior = behavior;                                     // +0x24
    s->waypoint = waypoint;                                     // +0x00
    s->waypoint2 = waypoint;                                    // +0x04
    s->max_speed_allowed = (behavior == 0) ? 0.0f : 500.0f /*43fa0000*/;   // +0x18
    s->f20 = 15;                                                // +0x20 (never read)
    s->chosen_speed = bh[behavior].top_speed;                   // +0x1c = bh+0x14
    s->chosen_lane = lane;                                      // +0x14
    s->f10 = 10;                                                // +0x10 (never read)
    s->state = 0;                                               // +0x08 ai_state
    s->lane_change = 0;                                         // +0x0c
    s->collision = 0; s->collision_timer = 0.0f; s->racer_state = 0;   // +0x2c +0x30 +0x34
    s->racer_mode = 1;                                          // +0x38
    s->finish_flag = 0;                                         // +0x40
    s->max_lanes = 2;                                           // +0x3c
    o->ai = a;                                                  // obj +0x0c
    o->ontrack = 1;                                             // obj +0x38
    if (behavior >= 2 && behavior <= 5) {
        backtrack_speed_trigger(object, &speed_trig, &lane_trig);   // 0x408080
        ai_apply_trigger(a, o->side, speed_trig);                   // 0x407ba0
        ai_apply_trigger(a, o->side, lane_trig);
        s->speed_factor = (float)rand() * 3.05185e-05f /*1/32767*/ * 0.2f /*0x47f1a0*/ - (-0.9f) /*0x47f1a4*/;  // 0.9..1.1
    }
    int r = 0;
    if (behavior == 14) {
        s->cop = add_cop(object);              // sub_4026c0 (T_ARRAY "Cop States" get_new; cop.+0=0,+4=player,+0x1c=object)
        s->max_speed_allowed = 0.0f;
        reset_cop(object, s->cop);             // 0x402200
    } else s->cop = -1;                        // +0x44
    s->old_sample = o->f14;                    // +0x48 = obj+0x14 (current engine sample)
    s->f4c = 1;                                // +0x4c
    s->stuck_timer = 0.0f; s->f54 = 0; s->stuck_count = 0;      // +0x50 +0x54 +0x58 (r==0)
    s->progress = o->chunk - g_finish_chunk;   // +0x60
    s->last_chunk = o->chunk;                  // +0x64
    s->laps = 0;                               // +0x68
}
```
`trigger_find_start_position` (sub_429a20): first chunk (in 0x4e5d2c order) whose trigger_a or trigger_b == 0xe
(finish trigger), wrapped with rceng_wrap_chunk; fatal "(trigger_find_start_position) unable to find finish trigger
in map" -> 0.

### ai_set_lane (0x406bf0)
`void ai_set_lane(int object, int lane)` (callers: ai_check_collision 0x4084b0, respawn_car 0x40b480). Re-places an
existing AI in `lane`, resets its max speed and re-reads the triggers.
```c
void ai_set_lane(int object, int lane) {
    int speed_trig = 8, lane_trig;
    OBJ *o = &objects[object];
    if (o->side != 1 && o->side != 0) fatal("%s(%d): obj_side(%d) = %d is illegal!.\n", "...ai_main.CPP", 0x2e7, object, o->side);
    if (o->side == 1) lane_trig = 0x12;
    if (o->side == 0) lane_trig = 0x28;
    int a = o->ai; int b = ai[a].behavior;
    ai[a].chosen_lane = lane;
    ai[a].max_speed_allowed = (b == 0) ? 0.0f : bh[b].top_speed;   // NB: add_ai uses 500.0 here
    o->ontrack = 1;
    if (b >= 2 && b <= 5) {
        backtrack_speed_trigger(object, &speed_trig, &lane_trig);
        ai_apply_trigger(a, o->side, speed_trig);
        ai_apply_trigger(a, o->side, lane_trig);
    }
}
```

### (not assigned, same file) ai_lane_shift_a (0x406d10) / ai_lane_shift_b (0x406d90)
`int f(int ai_idx)`: `w = wps[ai.waypoint].link[3](+0x20)`; if w==-1 -> 0; `n = wps[w].+0x18` (0x406d10) or
`wps[w].+0x1c` (0x406d90); if n==-1 or n==ai.waypoint -> 0; `if (ai.waypoint2 == ai.waypoint) ai.waypoint2 =
ai.waypoint` (no-op as compiled); `ai.waypoint = n; ai.lane_change(+0x0c) = 0; return 1`. Used by change_lane.

### check_for_new_target_waypoint (0x406e10)
`int check_for_new_target_waypoint(float p_range, int p_next_wp, float *p_pos, float p_interval)`.
Returns 1 when the object (p_pos = obj3d position) is within `p_range * factor * p_interval` of waypoint p_next_wp,
where p_interval is the distance between the current and next waypoint and factor in [1,1.5] grows as the
interval approaches 128.
```c
int check_for_new_target_waypoint(float p_range, int p_next_wp, float *p_pos, float p_interval) {
    float d[3] = { wps[p_next_wp].pos[0]-p_pos[0], wps[p_next_wp].pos[1]-p_pos[1], wps[p_next_wp].pos[2]-p_pos[2] }; // wp +8,+0xc,+0x10
    float l_delta = 128.0f /*0x47f1a8*/ - p_interval;
    if (l_delta < 0.0f) l_delta *= -1.0f;
    if (l_delta > 8.0f) l_delta = 8.0f;
    float l_factor = 1.0f - (8.0f - l_delta) * 0.125f * -0.5f;
    p_range = p_range * l_factor;
    if (p_interval * p_range > sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2])) return 1;   // "Switching to next waypoint."
    return 0;
}
```
(FPU order: sum = (dx*dx + dy*dy) + dz*dz; compare `p_interval*p_range` > sqrt(sum), strict.)

### calc_steering (0x406fd0)
`float calc_steering(int object, int waypoint, int ai_idx)`: signed y-component of cross(forward, dir_to_wp) times
`bh.steer_gain`; stored by handle_basic_ai in obj +0x7c (steering input).
```c
float calc_steering(int object, int waypoint, int ai_idx) {
    int b = ai[ai_idx].behavior;
    O3D *g = &obj3d[objects[object].obj3d];
    float d[3] = { wps[waypoint].pos[0]-g->pos[0], wps[waypoint].pos[1]-g->pos[1], wps[waypoint].pos[2]-g->pos[2] }; // obj3d +0x50
    float f[3]; get_forward(f, &g->matrix);   // sub_45e230(out, obj3d+0x10): out = obj3d[+0x30,+0x34,+0x38] (matrix row 2)
    float dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    return ((f[0]/fl) * (d[2]/dl) - (f[2]/fl) * (d[0]/dl)) * bh[b].steer_gain;   // bh +0x0c
}
```

### calc_speed (0x407140)
`float calc_speed(int p_target_wp, int p_ai_index, int p_object)` -> returns the speed DELTA to add to obj +0x74.
Desired speed = chosen speed reduced by curvature (|cross(forward, dir_to_target)|), clipped by
ai_max_speed_allowed; then accelerate/brake towards it.
```c
float calc_speed(int p_target_wp, int p_ai_index, int p_object) {
    float l_current_speed = objects[p_object].speed;            // obj +0x74
    O3D *g = &obj3d[objects[p_object].obj3d];
    t_ai_state *s = &ai[p_ai_index]; t_ai_behavior *B = &bh[s->behavior];
    float d[3] = wps[p_target_wp].pos - g->pos;
    float dl = sqrtf(vec_len2(d));                              // sub_4075b0
    float n[3] = { d[0]/dl, d[1]/dl, d[2]/dl };                // vec_set sub_407590
    float f[3]; get_forward(f, &g->matrix);
    float fl = sqrtf(vec_len2(f));
    f = { f[0]/fl, f[1]/fl, f[2]/fl };
    float cross = f[0]*n[2] - f[2]*n[0];
    if (cross < 0.0f) cross *= -1.0f;                           // "absolute cross product"
    float l_desired = s->chosen_speed - (B->brake_speed * cross * s->chosen_speed) / B->top_speed;
    //  x87 order: ((brake_speed*cross)*chosen)/top_speed, then chosen - that
    if (l_desired > s->max_speed_allowed) l_desired = s->max_speed_allowed;  // +0x18
    float l_new;
    if (l_desired == l_current_speed) l_new = l_desired;
    else if (l_desired > l_current_speed) {                     // "Accelerating"
        l_new = dt() * B->accel + l_current_speed;              // bh +0x10
        if (l_new > s->speed_factor * B->top_speed)             // ai +0x5c * bh +0x14
            l_new = s->speed_factor * B->top_speed;             // "Setting speed to top-speed."
    } else if (l_current_speed <= 0.0f) l_new = l_desired;
    else {                                                      // "Braking"
        l_new = l_current_speed - dt() * B->decel;              // bh +0x18
        if (l_new < 0.0f) l_new = 0.0f;                         // "Speed set to zero."
    }
    return l_new - l_current_speed;
}
```
Note: neither branch clamps to l_desired; acceleration only stops at top_speed*speed_factor.

#### vec_set (0x407590) / vec_len2 (0x4075b0)
`void __thiscall vec_set(float *this, float x, float y, float z)` (ret 0xc): this[0..2] = x,y,z; returns this.
`float vec_len2(const float *v)` (cdecl): returns `(v[1]*v[1] + v[2]*v[2]) + v[0]*v[0]` (x87, unrounded in ST0;
callers take fsqrt directly).

### next_waypoint (0x4075f0)
`int next_waypoint(int wp, int *unused)`. The "random" branch selector is `(int)dt()` (ftol of the clamped frame
time) which is always 0, so at a fork the +0x1c link is preferred. arg2 is never written (so handle_basic_ai's
l_lane_modifier is always 0).
```c
int next_waypoint(int wp, int *unused) {
    int r = (int)dt();                      // sub_441cd0 + _ftol (sub_4700e0); 0 in practice
    WP *w = &wps[wp];
    if (w->link0 /*+0x14*/ != -1) return w->link0;
    if (r % 2 != 0) { if (w->link1 /*+0x18*/ != -1) return w->link1; if (w->link2 /*+0x1c*/ != -1) return w->link2; return wp; }
    else            { if (w->link2 != -1) return w->link2; if (w->link1 != -1) return w->link1; return wp; }
}
```

### chunk_is_ahead (0x407660)
`int chunk_is_ahead(int a, int b)` (wrap-aware a > b with n = g_road_nchunks 0x4e5d30):
```c
int chunk_is_ahead(int a, int b) {
    int n = g_road_nchunks;
    if (a > b) { if (a - b < n/2) return 1; }      // n/2: signed truncating division
    if (a < b) { if (b - a > n/2) return 1; }
    return 0;
}
```

### handle_basic_ai (0x4076b0)
`int handle_basic_ai(int object)` -> always 1. Common per-frame driving: progress bookkeeping, EINDE_WEG,
waypoint lookahead, speed (calc_speed), steering (calc_steering), waypoint advance; at end of road brakes.
```c
int handle_basic_ai(int object) {
    OBJ *o = &objects[object];
    int a = o->ai, chunk = o->chunk;                   // obj +0x0c, +0x18
    t_ai_state *s = &ai[a]; int b = s->behavior;
    if (chunk != s->last_chunk) {                     // progress counter (laps * nchunks + chunk)
        int d = rceng_chunk_distance(chunk, s->last_chunk);   // 0x442700: |a-b| wrapped to <= n/2 (n = 0x4e65e4)
        if (chunk_is_ahead(chunk, s->last_chunk)) s->progress += d; else s->progress -= d;   // inlined, n = 0x4e5d30
    }
    s->last_chunk = chunk;
    if (chunks[chunk].trigger_a == 0x2c || chunks[chunk].trigger_b == 0x2c)   // +0xec/+0xf0, 44 = EINDE_WEG
        return 1;                                     // "Chunk %d, EINDE_WEG trigger, turning off object %d." (nothing else done)
    if (o->f34 == 0) fatal("SANITY CHECK ENTERING handle_basic_ai()\n\tCar (object %d) is off.\n", object);
    if (o->visible /*+0x2c*/ == 0 && !(b >= 7 && b <= 13))
        fatal("SANITY CHECK ... Car (object %d) is invisible.\n\tai = %d...", object, a, b, 0, o->f34);
    int cur = s->waypoint;
    int next = next_waypoint(cur, 0);
    int l_lane_modifier = 0;
    int target = cur;
    for (int i = bh[b].lookahead; i > 0; i--) target = next_waypoint(target, &l_lane_modifier);  // bh +0x00 (int)
    s->chosen_lane += l_lane_modifier;                // always +0
    if (next == -1 || target == -1) {                 // "End of road: quiting" (cannot happen: next_waypoint never returns -1)
        o->vel_x = 0; o->vel_y = 0;                   // +0x6c, +0x70 (int 0 stores)
        o->speed -= bh[b].decel;                      // NOT scaled by dt
        if (o->speed < 0.0f) o->speed = 0;
        return 1;
    }
    if (o->speed > bh[b].top_speed * 4.0f /*0x47f1d4*/) o->speed = bh[b].top_speed;
    o->vel_x = 0; o->vel_y = 0;
    o->speed += calc_speed(target, a, object);
    if (bh[b].top_speed * 4.0f < o->speed) o->speed = bh[b].top_speed;
    o->steer /*+0x7c*/ = calc_steering(object, next, a);
    if (o->speed < 1.5f /*0x47f1d8*/ || (object == g_player_object && g_game_event /*0x4d9a7c*/ == 7))
        o->steer = 0;
    float interval = |wps[s->waypoint].pos - wps[next].pos|;   // (cur-next), sqrt(dx*dx+dy*dy+dz*dz)
    if (check_for_new_target_waypoint(bh[b].wp_range /*+4*/, next, &obj3d[o->obj3d].pos, interval) == 1) {
        s->waypoint = next;
        s->waypoint2 = next_waypoint(s->waypoint2, 0);
    }
    return 1;
}
```

### ai_apply_trigger (0x407ba0)
`void ai_apply_trigger(int ai_idx, int side, int trigger)` (callers add_ai, ai_set_lane, ai_check_triggers).
Speed triggers set ai_max_speed_allowed; trigger 10 is the traffic-light stop line; lane triggers ("N-baans",
Dutch "N lanes") set ai_max_lanes for the matching carriageway.
```c
void ai_apply_trigger(int a, int side, int trigger) {
    switch (trigger) {
    case 4:  ai[a].max_speed_allowed = 16.67f /*41855c29*/; break;   // 60 km/h
    case 5:  ai[a].max_speed_allowed = 22.22f /*41b1c28f*/; break;   // 80 km/h
    case 6:  ai[a].max_speed_allowed = 33.33f /*420551ec*/; break;   // 120 km/h
    case 8:  ai[a].max_speed_allowed = 500.0f /*43fa0000*/; break;   // no limit
    case 10: if (g_traffic_light_state /*0x48c484*/ == 0) {         // light state 0 = stop (red)
                 ai[a].max_speed_allowed = 0.0f;
                 ai[a].state = 1;                                    // AI_STATE_WAIT_LIGHT
             } break;
    default /*7,9,other*/: break;
    }
    if (side == 1 && trigger >= 0x12 && trigger <= 0x15) ai[a].max_lanes = trigger - 0x11;   // 18..21 -> 1..4
    if (side == 0 && trigger >= 0x28 && trigger <= 0x2b) ai[a].max_lanes = trigger - 0x27;   // 40..43 -> 1..4
}
```
Traffic-light: HANDLE_TRAFFIC_AI releases a waiting car (`state==1 && light != 0` -> max_speed_allowed=500,
state=0). So light state 0 = red/stop; states 3 and 2 = drivable. t_ai_state.state values: 0 = AI_STATE_DRIVE,
1 = AI_STATE_WAIT_LIGHT.

### ai_limit_lanes (0x407de0)
`void ai_limit_lanes(int ai_idx, int side, int trigger)` (HANDLE_TRAFFIC_AI): like the lane part of
ai_apply_trigger, but only ever lowers max_lanes.
```c
void ai_limit_lanes(int a, int side, int trigger) {
    if (side == 1 && trigger >= 0x12 && trigger <= 0x15) { int n = trigger - 0x11; if (ai[a].max_lanes > n) ai[a].max_lanes = n; }
    if (side == 0 && trigger >= 0x28 && trigger <= 0x2b) { int n = trigger - 0x27; if (ai[a].max_lanes > n) ai[a].max_lanes = n; }
}
```

### ai_check_triggers (0x407fc0)
`void ai_check_triggers(int object)` (HANDLE_TRAFFIC_AI): applies the triggers of the object's chunk and of its
current waypoint.
```c
void ai_check_triggers(int object) {
    OBJ *o = &objects[object]; int a = o->ai;
    ai_apply_trigger(a, o->side, chunks[o->chunk].trigger_a);   // +0xec
    ai_apply_trigger(a, o->side, chunks[o->chunk].trigger_b);   // +0xf0
    ai_apply_trigger(a, o->side, wps[o->waypoint /*obj+0x20*/].trigger /*wp+0x04*/);
}
```

### backtrack_speed_trigger (0x408080)
`void backtrack_speed_trigger(int object, int *p_speed_trigger, int *p_lane_trigger)`. Walks the chunks backwards
(against the driving direction) from the object's chunk until both a speed trigger and a lane trigger for the
object's side are found. NO_TRIGGER = 0. Defaults: speed 8 (no limit), lane 0x13 (side 1) / 0x29 (side 0) = 2 lanes.
```c
void backtrack_speed_trigger(int object, int *p_speed_trigger, int *p_lane_trigger) {
    *p_speed_trigger = 0; *p_lane_trigger = 0;                // NO_TRIGGER
    OBJ *o = &objects[object];
    int c = o->chunk;
    int dir = g_direction /*0x4d9ac4*/ ? 1 : -1;
    while (c <= g_road_nchunks - 1 && c >= 0) {               // no wrap-around: stops at chunk 0 / last chunk
        for (int t = 0; t < 2; t++) {
            int v = chunks[c].trigger[t];                     // +0xec + 4*t
            if (v == 4 || v == 5 || v == 6 || v == 8) {       // speed trigger (7 is NOT accepted here)
                if (*p_speed_trigger == 0) { *p_speed_trigger = v; if (*p_lane_trigger != 0) return; }
            } else if (v >= 0x12 && v <= 0x15) {
                if (o->side == 1 && *p_lane_trigger == 0) { *p_lane_trigger = v; if (*p_speed_trigger != 0) return; }
            } else if (v >= 0x28 && v <= 0x2b) {
                if (o->side == 0 && *p_lane_trigger == 0) { *p_lane_trigger = v; if (*p_speed_trigger != 0) return; }
            }
        }
        c += dir;
    }
    // "Illegal chunk, returning defaults..."
    if (*p_speed_trigger == 0) *p_speed_trigger = 8;
    if (*p_lane_trigger == 0) {
        g_no_lane_trigger_count++;                            // 0x4a5bd0 (debug counter)
        if (o->side == 1) *p_lane_trigger = 0x13;
        if (o->side == 0) *p_lane_trigger = 0x29;
    }
}
```
Byte-index table 0x4083a4 (v-4 -> case): 4,5,6,8 -> speed; 7 and 9..17, 22..39 -> none; 18..21 -> side-1 lane;
40..43 -> side-0 lane. Waypoint triggers (wp +0x04) are NOT considered by backtracking.

#### Trigger value summary (chunk trigger_a/b +0xec/+0xf0, waypoint +0x04)
| value | meaning (AI) |
|---|---|
| 0 | NO_TRIGGER |
| 4 | speed limit 16.67 m/s (60 km/h); cop: 16.67 |
| 5 | speed limit 22.22 (80 km/h); cop: 22.22 |
| 6 | speed limit 33.33 (120 km/h); cop: 33.33 |
| 7 | ignored by AI; cop: "ILLEGAL SPEED TRIGGER" |
| 8 | no limit: AI 500.0; cop: 2 * player_top_speed (0x4d9e68) |
| 10 | traffic-light stop line (stop while g_traffic_light_state == 0) |
| 0xe (14) | finish line (trigger_find_start_position) |
| 0x12..0x15 (18..21) | 1..4 lanes on side 1 |
| 0x28..0x2b (40..43) | 1..4 lanes on side 0 |
| 0x2c (44) | EINDE_WEG (end of road): handle_basic_ai returns early |

### reset_cop (0x402200)
`void reset_cop(int object, int cop)`: initialises a cop's limits from the backtracked triggers.
cop state (0x4a5238 data, stride 0x20): +0x14 float max_speed, +0x18 float max_lanes (stored as float!).
```c
void reset_cop(int object, int cop) {
    int speed_trig, lane_trig;                     // [esp+0xc], [esp+0x8]
    backtrack_speed_trigger(object, &speed_trig, &lane_trig);
    switch (speed_trig) {                          // jump table 0x4024e0 (speed_trig-4, 0..4)
    case 4: cops[cop].max_speed = 16.67f /*41855c29*/; break;    // "Setting max speed to %f." (printed 16.67)
    case 5: cops[cop].max_speed = 22.22f /*41b1c28f*/; break;
    case 6: cops[cop].max_speed = 33.33f /*420551ec*/; break;
    case 8: cops[cop].max_speed = g_player_top_speed /*0x4d9e68*/ + g_player_top_speed; break;
    default: /* "ILLEGAL SPEED TRIGGER!!!!" */ break;            // incl. 7
    }
    if (objects[object].side == 1) switch (lane_trig) {          // table 0x4024f4
        case 0x12: cops[cop].max_lanes = 1.0f; break; case 0x13: = 2.0f; case 0x14: = 3.0f; case 0x15: = 4.0f;
        default: /* "ILLEGAL LANE TRIGGER!!!" */ ;
    }
    if (objects[object].side == 0) switch (lane_trig) {          // table 0x402504
        case 0x28: = 1.0f; case 0x29: = 2.0f; case 0x2a: = 3.0f; case 0x2b: = 4.0f;
        default: /* "ILLEGAL LANE TRIGGER!!!" */ ;
    }
}
```
g_player_top_speed (0x4d9e68) = sub_40ce10(player) result, set in init_player_car (0x425802).

### ai_damp_velocity (0x4083d0)
`void ai_damp_velocity(int object)` ("Handling collision for object"): `o->vel_x *= 0.95f; o->vel_y *= 0.95f;
o->speed *= 0.95f;` (0x47f1dc = 0x3f733333). Object +0x6c/+0x70/+0x74 is the velocity vector (x, y, forward).

### ai_check_collision (0x4084b0)
`void ai_check_collision(int object)` (callers: HANDLE_COP_AI 0x402790, HANDLE_RACER_AI 0x40aab0, HANDLE_TRAFFIC_AI
0x40b770). While the AI is in collision mode it damps velocity until the object has been collision-free for 0.5 s,
then re-places it in the lane of its waypoint. If it keeps colliding (>5 frames) it reverses at -3 m/s; after
5 s of reversing (cumulative stuck timer, never reset) it is re-placed.
```c
void ai_check_collision(int object) {
    OBJ *o = &objects[object]; t_ai_state *s = &ai[o->ai];
    int side, lane;
    if (s->collision /*+0x2c*/ != 1) return;
    if (s->stuck_count /*+0x58*/ == -1) {                  // reversing
        s->stuck_timer /*+0x50*/ += dt();
        if (s->stuck_timer > 5.0f /*0x47f1e0*/) {
            s->stuck_count = 0;
            o->vel_x = 0; o->vel_y = 0; o->speed = 0; o->steer = 0;   // int 0 stores
            ai_find_waypoint_lane(o->chunk, o->waypoint, &side, &lane);   // sub_402520
            ai_set_lane(object, lane);
        }
        return;
    }
    if (o->collision /*obj+0x3c*/ == 0) {                  // "Object collision is false."
        s->collision_timer /*+0x30*/ += dt();
        ai_damp_velocity(object);
        if (s->collision_timer > 0.5f /*0x47f1c8*/) {
            o->ontrack = 1;  s->collision = 0;  s->collision_timer = 0;
            o->vel_x = o->vel_y = o->speed = 0; o->steer = 0;
            ai_find_waypoint_lane(o->chunk, o->waypoint, &side, &lane);
            ai_set_lane(object, lane);
        }
        s->stuck_count = 0;
    } else {                                               // "collision is true. Resetting timer."
        s->collision_timer = 0;
        if (++s->stuck_count > 5) { o->ontrack = 1; o->speed = -3.0f /*c0400000*/; s->stuck_count = -1; }
    }
}
```
`ai_find_waypoint_lane` (sub_402520, ai_cops.cpp): `(chunk, wp, int *side, int *lane)`: search
`chunks[chunk].wp_side0[0..lanes0)` (+0x124, count +0xe4) -> side=0; then `wp_side1` (+0x154, count +0xe8) -> side=1;
else fatal "%s(%d): Waypoint %d not found in chunk %d." (outputs untouched).

### apply_difficulty_modifier (0x4108d0)
`void apply_difficulty_modifier(void)` (caller sub_4093b0, during init_all_cars). For each of the 3 racer FSM
entries (0x4a55a0 + i*0x60, i=0..2) computes a "car gap" vs the player and scales that racer car's gear table by
the Game.ini dif_mod (0x4d9b1c, 0..9).
```c
void apply_difficulty_modifier(void) {
    static const float mult[10] = {0.6f/*3f19999a*/,0.7f/*3f333333*/,0.8f/*3f4ccccd*/,0.9f/*3f666666*/,1.0f,
                                   1.1f/*3f8ccccd*/,1.2f/*3f99999a*/,1.3f/*3fa66666*/,1.4f/*3fb33333*/,1.5f};
    int upgrades = (g_player_motor /*0x4d9ec0*/ != 0) + (g_player_gearbox /*0x4d9ed0*/ != 0) + (g_player_injection /*0x4d9ed4*/ != 0);
    for (FSM *e = &g_fsm[0]; e < &g_fsm[3]; e++) {       // esi = &e->car (+0x30), loop end 0x4a56f0
        int gap = g_player_car /*0x4d9e2c*/ + upgrades - e->car /*+0x30*/;
        if (gap < 0) gap = 0;
        if (gap > 3) gap = 3;
        if (g_dif_mod > 9) fatal("Difficulty modifier has invalid value");
        e->skill /*+0x5c*/ = (float)gap * 4.0f /*0x47f368*/;
        if (g_player_gearbox) for (k = 0; k < 6; k++) g_car_gears[e->car][6 + k] *= mult[g_dif_mod];  // 0x4a5f28 + car*0x7c
        else                  for (k = 0; k < 5; k++) g_car_gears[e->car][k]     *= mult[g_dif_mod];  // 0x4a5f10 + car*0x7c
    }
}
```
(g_car_gears 0x4a5f10: float[31] per car type; [0..4] 5-speed ratios, [6..11] 6-speed ratios; used by the racer
engine code sub_40e9f0/sub_40eae0. dif_mod index > 9 is reported but still used.)

### ai_heading_with_road (0x410a10)
`int ai_heading_with_road(int object)` (caller sub_41a780): 1 if the object's forward axis points along the driving
direction of its chunk (x/z dot product >= 0).
```c
int ai_heading_with_road(int object) {
    OBJ *o = &objects[object];
    int w = rceng_get_waypoint_index(o->chunk, 0, g_direction == 0 ? 1 : 0);   // sub_442150(chunk, lane, side)
    int n = next_waypoint(w, 0);
    float dx = wps[n].pos[0] - wps[w].pos[0], dz = wps[n].pos[2] - wps[w].pos[2];
    float m[16]; memcpy(m, &obj3d[o->obj3d].matrix /*+0x10*/, 64);
    if (dx * m[8] + dz * m[10] < 0.0f /*0x47f448*/) return 0;                  // m[8],m[10] = obj3d +0x30,+0x38
    return 1;
}
```

---------------------------------------------------------------------------------------------------------

### Difficulty (Game.ini) - where it is used

* `Difficulty` ([OPTIONS] "difficulty", sub_421b70 @0x421e32) -> **g_difficulty 0x4d9ab0** (0 = easy, !=0 = hard).
* `dif_mod` ([OPTIONS] "dif_mod", @0x421f82) -> **g_dif_mod 0x4d9b1c** (0..9 index into mult[] above; 4 = x1.0).
* `DIRECTION` ([ETAPPE], @0x421be1) -> g_direction 0x4d9ac4.

g_difficulty uses:
| where | effect |
|---|---|
| init_ai 0x405bc3 | factors K0..K5 for racer behaviours 8..13 (brake/accel/top speed): easy .96/.91/.87/.83/.78/.74, hard .98/.96/.94/.92/.90/.88 |
| sub_4014e0 (racer_speed_factor, ai_cfun.cpp; called by racer engine sub_40eae0) | rubber band: see below |
| 0x401c42 (in sub_401b60, ai_cfun.cpp, FSM condition) | returns `g_difficulty ? 3 : 1` (else-branch value) |
| sub_401e20 (FSM condition, cop give-up timer) | timer limit 30.0 s (easy) / 60.0 s (hard) on FSM +0x10 before siren off |
| sub_4034a0 (FSM: assign cop to target) | max cops chasing the player = `g_difficulty ? 3 : 1` (other targets 2) |
| sub_423d20 0x423df7 | traffic counts g_traffic_same_side/oncoming = 10/14 (hard) vs 5/7 (easy) |
| sub_423d20 0x424360, sub_427190, sub_427de0, sub_427fd0, sub_4280c0 | non-AI game logic (timers/prices/score; e.g. 0x428131 value 5000 when easy) |

racer_speed_factor (0x4014e0) `float f(int racer)`: `if (racer >= 3) return 1.0;` d = ai[objects[g_racers[racer].object].ai].progress
- g_player_progress (0x4d9e70), clamped to [-50, 50]; T = table 0x482258[(g_difficulty*7 + track)*3 + racer] =
{ahead_thr, behind_thr, scale}. If d > 0: `d > ahead_thr ? 1 - d*scale*0.02 : (diff ? {1.02,1.01,1.0} : {0.95,0.92,0.90})[racer]`;
if d <= 0: `d < behind_thr ? 1 - d*scale*0.02 : (diff ? {1.15,1.1,1.05} : {1.0,0.98,0.95})[racer]`; result clamped
to [0.2, 2.0]. Table (same for the 3 racers), easy tracks 0..6: (25,-25,1) (25,-25,1.5) (30,-20,2) (30,-20,2.5)
(35,-15,3) (35,-15,3) (45,-10,4); hard: (35,-20,2) (35,-20,2.5) (40,-15,3) (40,-15,3.5) (45,-10,4) (45,-5,4.5) (50,-5,5).

---------------------------------------------------------------------------------------------------------

### Structures

#### t_ai_state (T_ARRAY "AI_States", data 0x4a5b40, stride 0x6c)
| off | type | name | evidence |
|---|---|---|---|
| 0x00 | int | ai_waypoint (current waypoint) | add_ai 0x406966, handle_basic_ai 0x407815/0x407aa5 |
| 0x04 | int | ai_waypoint2 (advanced in lockstep with +0) | 0x40696f, 0x407ab6 |
| 0x08 | int | ai_state (0 drive, 1 waiting at red light) | 0x407c3b, debug 0x40806f |
| 0x0c | int | ai_lane_change (reset by lane shift) | 0x406a2b, 0x406d6e |
| 0x10 | int | (=10, never read) | 0x406a13 |
| 0x14 | int | ai_chosen_lane | 0x406a04, 0x4078bf |
| 0x18 | float | ai_max_speed_allowed | 0x406986, 0x407ba0, calc_speed 0x4073bf |
| 0x1c | float | ai_chosen_speed | 0x4069da, calc_speed 0x407348 |
| 0x20 | int | (=15, never read) | 0x40699d |
| 0x24 | int | ai_behavior | 0x40695a |
| 0x28 | - | unused | |
| 0x2c | int | ai_collision (1 = in collision handling) | 0x4084f9 |
| 0x30 | float | ai_collision_timer | 0x408628 |
| 0x34 | int | racer state (2 = finished, set in 0x40b0e0) | 0x406a35 |
| 0x38 | int | racer mode (1/5, 0x40b170) | 0x406a58 |
| 0x3c | int | ai_max_lanes | 0x406a6c, 0x407c7c |
| 0x40 | int | finish-line flag (0x40b070) | 0x406a62 |
| 0x44 | int | cop index (behaviour 14) else -1 | 0x406b1c |
| 0x48 | int | ai_old_sample (engine sample, siren swap 0x40c640) | 0x406b65 |
| 0x4c | int | flag, init 1 (used by change_lane 0x403660, racer/traffic AI) | 0x406b6f |
| 0x50 | float | stuck (reverse) timer | 0x408547 |
| 0x54 | int | (=0, never read) | 0x406b87 |
| 0x58 | int | collision frame counter, -1 = reversing | 0x408732 |
| 0x5c | float | top-speed factor (1.0; traffic 0.9..1.1) | 0x4068c8, 0x406b01, 0x407491 |
| 0x60 | int | progress (chunk - finish chunk, accumulates over laps) | 0x406bad, 0x407769 |
| 0x64 | int | last chunk | 0x406bc2, 0x40778e |
| 0x68 | int | laps (compared with 0x4d9ad8 in 0x40b092) | 0x406bcc |

#### t_ai_behavior (T_ARRAY "AI Behaviors", data 0x4a5b88, stride 0x2c, 16 entries)
| off | type | name | evidence |
|---|---|---|---|
| 0x00 | int | bh_lookahead (target = current wp + N) | 0x407867 |
| 0x04 | float | bh_wp_range (waypoint switch range factor) | 0x407a7e |
| 0x08 | float | bh_brake_speed (curve slow-down) | debug 0x407370, 0x40735a |
| 0x0c | float | bh_steer_gain | 0x407134 |
| 0x10 | float | bh_acceleration | 0x40744f |
| 0x14 | float | bh_top_speed (= bh_preferred_speed) | 0x4069b2, debug 0x4069b9 |
| 0x18 | float | bh_deceleration | 0x407511, 0x407b24 |
| 0x1c | float | bh_rear_range (pick_lane) | 0x404a3c |
| 0x20 | float | bh_follow_range (adjust_speed) | 0x405655 |
| 0x24 | float | bh_front_range (pick_lane) | 0x4049fd |
| 0x28 | float | unused (2.0) | init only |

#### Object (0x4e48e0, stride 0xa0) fields used here
| off | type | name | evidence |
|---|---|---|---|
| 0x08 | int | obj3d index | 0x4071a3 |
| 0x0c | int | ai index | 0x406a7a |
| 0x14 | int | current sound sample | 0x406b5b |
| 0x18 | int | chunk | 0x4076e5 |
| 0x20 | int | waypoint | 0x408030 |
| 0x28 | int | side (0/1) | 0x4068e8 |
| 0x2c | int | visible | 0x4077de |
| 0x34 | int | on (car active) | 0x4077bf |
| 0x38 | int | ontrack | 0x406a84, 0x40866f |
| 0x3c | int | collision (this frame) | 0x4085f2 |
| 0x6c/0x70/0x74 | float | velocity x / y / forward speed | 0x408431 |
| 0x7c | float | steering input | 0x40799c |

#### Others
| struct | off | type | name | evidence |
|---|---|---|---|---|
| waypoint (0x50) | 0x04 | int | trigger | 0x40803f |
| waypoint | 0x08..0x10 | float[3] | position | 0x406e53 |
| waypoint | 0x14/0x18/0x1c/0x20 | int | links (-1 none); +0x14 straight, +0x18/+0x1c fork/side, +0x20 used by lane shift | 0x407612 |
| chunk (0x184) | 0xe4/0xe8 | int | lane count side 0/1 | 0x40253e |
| chunk | 0xec/0xf0 | int | trigger_a / trigger_b | 0x4077a2 |
| chunk | 0x124 / 0x154 | int[] | waypoint per lane side 0 / side 1 | 0x402549 |
| obj3d (0x99) | 0x10 | float[16] | matrix (row 2 = forward, +0x30..+0x38) | 0x407293, 0x410acb |
| obj3d | 0x50..0x58 | float[3] | position | 0x407212 |
| t_cop (0x20) | 0x00/0x04/0x1c | int | state 0 / target (player) / object | 0x402755 |
| t_cop | 0x14 | float | max speed | 0x402284 |
| t_cop | 0x18 | float | max lanes | 0x402376 |
| FSM entry (0x60) | 0x30 | int | racer car type | 0x41095f |
| FSM entry | 0x5c | float | skill gap * 4 | 0x4109a2 |

### Globals
| addr | type | name | meaning |
|---|---|---|---|
| 0x4a5b40 | T_ARRAY | g_ai_states | t_ai_state array (+0 data, +4 count, +8 last, +0xc init, +0x10 used, +0x14 name) |
| 0x4a5b88 | T_ARRAY | g_ai_behaviors | 16 behaviours |
| 0x483850 | int | g_finish_chunk | chunk with finish trigger 0xe, init -1, lazily set by add_ai |
| 0x4a5bd0 | int | g_no_lane_trigger_count | debug counter in backtrack |
| 0x4d9ab0 | int | g_difficulty | Game.ini difficulty 0 easy / 1 hard |
| 0x4d9b1c | int | g_dif_mod | Game.ini dif_mod 0..9 |
| 0x4d9ac4 | int | g_direction | [ETAPPE] DIRECTION, backtrack direction +1 if set else -1 |
| 0x4d9e68 | float | g_player_top_speed | from sub_40ce10 |
| 0x4d9e70 | int | g_player_progress | player chunk progress (like ai +0x60) |
| 0x4d9ab8 | int | g_frame_counter | debug "FRAME: %06d" |
| 0x48c484 | int | g_traffic_light_state | 0 = red (stop at trigger 10) |
| 0x4d9ec0/0x4d9ed0/0x4d9ed4 | int | g_player_motor / gearbox / injection | [PLAYER] motor, versnellingsbak, injectie |
| 0x4d9e2c | int | g_player_car | [PLAYER] car |
| 0x4a5f10 | float[][31] | g_car_gears | per car type gear tables (stride 0x7c) |
| 0x482258 | {float,float,float}[2][7][3] | g_rubber_band | racer catch-up table |
| 0x49b014 | float | (1024.0) | steer gain divisor |

### Open questions
* t_ai_state +0x10 (10), +0x20 (15), +0x54 are written once in add_ai and never read anywhere (checked every
  function referencing 0x4a5b40). +0x4c semantics not resolved (read in 0x403660/0x40aab0/0x40b770).
* bh +0x28 (2.0) never read.
* next_waypoint's fork selector is `(int)frame_time % 2`, effectively always 0 (prefers link +0x1c); probably meant
  to be random - keep as is for 1:1.
* handle_basic_ai's "End of road" branch is unreachable (next_waypoint never returns -1) but implemented.
* EINDE_WEG trigger only returns early; the "turning off" is presumably done by the chunk trigger handler table
  (0x48c488) elsewhere.
* Exact float rounding of the difficulty-scaled racer rows: computed with x87 in 53-bit precision; use hex values.


## 5. Lanes (ai_lane.cpp): change_lane and friends


Source file string: `C:\work\A2racII\Game\ai_lane.cpp` (0x482fb8). All debug printf calls (sub_464ca0) are omitted
from the pseudocode except where their format string names a variable. `sub_464e50` = `fatal()` (assert/print, does
NOT stop execution in the code paths below: code always continues after it).
Library calls: sub_470240 = strncpy, sub_470340 = malloc, sub_470230 = free.

Conventions used below:
```c
#define OBJ(i)    ((t_object*)(*(char**)0x4e48e0 + (i)*0xa0))
#define O3D(i)    ((char*)(*(char**)0x4ea9e0 + (i)*0x99))   /* NOTE stride 0x99 = 153 (idx*17*9), NOT 0x88 */
#define WP(i)     ((t_waypoint*)(*(char**)0x4e6628 + (i)*0x50))
#define CHUNK(i)  ((t_chunk*)(*(char**)0x4e65e0 + (i)*0x184))  /* object+0x18 is used DIRECTLY as record index */
#define AI(i)     ((t_ai*)(*(char**)0x4a5b40 + (i)*0x6c))
#define BH(i)     ((t_ai_behavior*)(*(char**)0x4a5b88 + (i)*0x2c))
#define COP(i)    ((char*)(*(char**)0x4a5238 + (i)*0x20))
#define FLT_BIG   3.4e38f   /* 0x7f7fc99e, the source literal is 3.4e38 (not FLT_MAX 0x7f7fffff) */
```

### Lane model (summary)

* Lane index 0 = left-most / fastest lane, `lane_count-1` = right-most / slowest lane (debug strings: increasing
  index = "RIGHT", traffic driver "in slowest lane" = last index; traffic keeps right whenever possible).
* `p_lanes_allowed`: number of lanes (counted from 0) a driver may use; a lane with index >= p_lanes_allowed is the
  "illegal lane" (the traffic driver must leave it to the left).
* For every lane of the object's current chunk/side a `t_lane_info` is filled (scan_lane + analyse_lane), then a
  driver-type specific chooser picks `l_chosen_lane`; `ai_chosen_lane` moves at most ONE lane per call towards it,
  and only if the waypoint graph offers a diagonal link (switch_lane_left/right). Then `ai_chosen_speed` is
  recomputed with adjust_speed().

#### t_lane_info (0x20 bytes; debug names from format at 0x482e30 / 0x4836ac)
| off | type | name | default (0x482b58) | written by |
|---|---|---|---|---|
| 0x00 | int | m_dead_end (0/1) | 0 | find_dead_end |
| 0x04 | int | m_dead_range (waypoints until dead end, -1 = none) | -1 | find_dead_end |
| 0x08 | int | m_front_obstacle (object idx, -1 none) | -1 | find_obstacle_here / find_obstacle_in_wp |
| 0x0c | float | m_front_range (3-D distance) | -1.0 | idem (3.4e38 when searched & nothing found) |
| 0x10 | int | m_front_status (t_lane_status) | 0 | analyse_lane |
| 0x14 | int | m_rear_obstacle | -1 | find_obstacle_here / find_obstacle_in_wp |
| 0x18 | float | m_rear_range | -1.0 | idem |
| 0x1c | int | m_rear_status | 0 | analyse_lane |

#### lane status enum (m_front_status; names from pick_lane / adjust_speed debug strings)
| value | name | meaning (set in analyse_lane 0x404720) |
|---|---|---|
| 0 | LANE_FREE | no front obstacle, no dead end within range |
| 1 | LANE_FASTER | front obstacle drives same direction and is FASTER than me (speed > mine) |
| 2 | LANE_SLOWER | front obstacle same direction, speed <= mine |
| 3 | LANE_BRAKE | no front obstacle, dead end with m_dead_range >= 2 |
| 4 | LANE_STOP | no front obstacle, dead end with m_dead_range < 2 |
| 5 | LANE_GHOST | front obstacle drives in the opposite direction ("spookrijder") |

("illegal" is not a status value; it is the `p_in_illegal_lane` flag of pick_lane.)
m_rear_status: 0 = no rear obstacle OR rear obstacle drives opposite direction; 1 = rear obstacle same direction and
speed <= mine; 2 = rear obstacle same direction and faster than me (closing in).

#### Driver types (CHANGE_LANE p_driver_type, jump table 0x403f34)
0 Traffic, 1 Racer, 2 Cop, 3 Adac, 4 Dead, 5 Racer coming up on a refuelling point, 6 Racer at refuelling point,
7 Racer refuelling, other = "Illegal driver type." Callers: traffic AI sub_40b770 `change_lane(obj, 0, ai+0x3c)`;
racer AI sub_40aab0 `change_lane(obj, ai+0x38, -1)`; cop AI sub_4026c0 `change_lane(obj, 2, -1)` or
`change_lane(obj, 4, -1)` after "Quiting chase!"; sub_402b40 `change_lane(obj, 2, -1)`. No caller passes 3 (ADAC).

---

#### change_lane (0x403660)
`void change_lane(int p_object, int p_driver_type, int p_lanes_allowed)` (debug: `CHANGE_LANE(p_object, p_lanes_allowed)`)

Builds a local `T_ARRAY<t_lane_info> l_lane_status[lane_count]` for the object's chunk+side, finds the lane index of the
object's waypoint, scans/analyses all lanes, lets the driver-type chooser select a lane, steps ai_chosen_lane one
lane towards it and recomputes ai_chosen_speed. Finally restores a sound sample for non-cop cars.
```c
void change_lane(int p_object, int p_driver_type, int p_lanes_allowed)
{
    T_ARRAY l_lane_status;               /* esp+0x30: data,count,last,init,flags; name "l_lane_status", type "t_lane_info" */
    int l_lane;                          /* esp+0x10: NOT initialised (garbage if waypoint not found on own side) */
    int l_ghost_seen;                    /* esp+0x28: out-param of analyse_lane, otherwise unused */
    t_object *o = OBJ(p_object);
    int l_ai = o->ai_index /*+0x0c*/, l_chunk = o->chunk /*+0x18*/, l_wp = o->waypoint /*+0x20*/, l_side = o->side /*+0x28*/;
    int l_lane_count, l_other_count, *l_ch_waypoints, *l_other_waypoints, l_lanes_to_scan, l_chosen_lane, i;

    l_lane_status.init = 0; T_ARRAY_init(&l_lane_status);        /* sub_405890 */
    /* "Determine side." */
    if (l_side == 0) { l_lane_count = CHUNK(l_chunk)->lanes[0] /*+0xe4*/; l_other_count = ->lanes[1] /*+0xe8*/;
                       l_ch_waypoints = ->wp_side0 /*+0x124*/;      l_other_waypoints = ->wp_side1 /*+0x154*/; }
    else             { l_lane_count = ->lanes[1] /*+0xe8*/;  l_other_count = ->lanes[0] /*+0xe4*/;
                       l_ch_waypoints = ->wp_side1 /*+0x154*/;      l_other_waypoints = ->wp_side0 /*+0x124*/; }

    /* inline T_ARRAY<t_lane_info>::alloc(l_lane_count) (array.hpp line 0x11d) */
    if (l_lane_status.init) T_ARRAY_free(&l_lane_status);        /* sub_405910 */
    if (l_lane_count < 0) fatal("T_ARRAY(construction) illegal array size ...");
    l_lane_status.data = malloc(l_lane_count*0x20); l_lane_status.count = l_lane_count; l_lane_status.last = 0;
    l_lane_status.flags = malloc(l_lane_count); l_lane_status.init = 1;
    for (i = 0; i < l_lane_count; i++) l_lane_status.flags[i] = 1;
    for (i = 0; i < l_lane_count; i++) l_lane_status.data[i] = DEFAULT_LANE_INFO;   /* 32 bytes copied from 0x482b58 */

    /* "Checking current waypoint lane." */
    for (i = 0; i < l_lane_count; i++) if (l_wp == l_ch_waypoints[i]) { l_lane = i; break; }
    if (p_lanes_allowed > l_lane_count) p_lanes_allowed = l_lane_count;      /* "Clamping p_lanes_allowed" */
    if (i == l_lane_count /*not found*/ || p_lanes_allowed > l_lane_count /*never true here*/) {
        if (l_wp != 0) {
            debug_set_channel(0, 1);                                          /* sub_464c80, debug only */
            /* "Scanning other side:" */
            for (i = 0; i < l_other_count; i++) if (l_wp == l_other_waypoints[i]) { l_lane = i; break; }
            fatal("%s(%d): Waypoint not found in current chunk!", "ai_lane.cpp", 156);   /* always, even if found */
        } else {
            drmap_find_closest_waypoint(p_object, -1);       /* sub_443da0, pure function, result DISCARDED; l_lane stays garbage */
        }
    }
    if (p_lanes_allowed == -1) p_lanes_allowed = l_lane_count;
    l_lanes_to_scan = (l_lane < p_lanes_allowed) ? p_lanes_allowed : l_lane_count;

    /* "Scanning lanes:" */
    for (i = 0; i < l_lane_count; i++)
        scan_lane(&l_lane_status.data[i], l_ch_waypoints[i], AI(l_ai)->scan_range /*+0x10*/, p_object, l_lane == i);
    /* "Analyzig lanes:" */
    for (i = 0; i < l_lane_count; i++)
        analyse_lane(&l_lane_status.data[i], p_object, &l_ghost_seen);

    switch (p_driver_type) {
    case 0: /* "Traffic driver." */
        l_chosen_lane = pick_lane(p_object, l_lanes_to_scan, &l_lane_status, l_lane, l_lane >= p_lanes_allowed); break;
    case 1: /* "Racer driver." */  l_chosen_lane = race_lane(p_object, l_lanes_to_scan, &l_lane_status, l_lane); break;
    case 2: /* "Cop driver." */    l_chosen_lane = cop_lane (p_object, l_lanes_to_scan, &l_lane_status, l_lane); break;
    case 3: /* "Adac driver." */   l_chosen_lane = adac_lane(p_object, l_lanes_to_scan, &l_lane_status, l_lane); break;
    case 4: /* "Dead driver." */   l_chosen_lane = l_lanes_to_scan - 1; break;
    case 5: /* "Racer coming up on a refuelling point." */ l_chosen_lane = l_lanes_to_scan - 1; break;
    case 6: /* "Racer at refuelling point." */ l_chosen_lane = race_lane(p_object, l_lanes_to_scan, &l_lane_status, l_lane); break;
    case 7: /* "Racer refuelling." */ l_chosen_lane = p_object; break;     /* sic: binary loads p_object (bug) */
    default: fatal("Illegal driver type.\n"); l_chosen_lane = p_object; break;   /* same code as case 7 */
    }

    t_ai *ai = AI(l_ai);
    if (l_chosen_lane != ai->chosen_lane /*+0x14*/) {             /* "Lane changed. Switching..." */
        if (l_chosen_lane < ai->chosen_lane) {                     /* "LEFT" */
            /* both arms of `if (g_direction /*0x4d9ac4*/ == 0)` are identical: */
            if (switch_lane_left(l_ai) == 1) ai->chosen_lane--;    /* sub_406d90, "SWITCH SUCCESSFUL." */
        }
        if (l_chosen_lane > ai->chosen_lane) {                     /* "RIGHT" (re-read after possible decrement) */
            if (switch_lane_right(l_ai) == 1) ai->chosen_lane++;   /* sub_406d10 (same 0x4d9ac4 no-op test) */
        }
        if (ai->chosen_lane < 0) ai->chosen_lane = 0;               /* "Clamping to zero." */
        if (ai->chosen_lane > l_lane_count - 1) ai->chosen_lane = l_lane_count - 1;   /* "Clamping to high-value" */
    }
    /* "Adjusting speed." */
    if (ai->chosen_lane == l_lane) {
        ai->chosen_speed /*+0x1c*/ = adjust_speed(p_object, &l_lane_status.data[ai->chosen_lane], ai->chosen_speed);
    } else {
        float s = ai->chosen_speed;
        float a = adjust_speed(p_object, &l_lane_status.data[l_lane], s);
        float b = adjust_speed(p_object, &l_lane_status.data[ai->chosen_lane], s);
        ai->chosen_speed = (b + a) * 0.5f;                          /* 0x47f0e8 */
    }
    l_ghost_seen = 0;
    if (ai->cop_index /*+0x44*/ == -1 && o->sample /*+0x14*/ == 41 /*0x29*/) {
        sndfx_set_object_sample(p_object, ai->old_sample /*+0x48*/);   /* sub_4289e0: stops current, sets obj+0x14 */
        if (o->sample != 41) ai->sample_restored /*+0x4c*/ = 1;
    }
    /* "FINISCH -> CHANGE_LANE()" ; inline ~T_ARRAY */
    if (l_lane_status.init == 1) free(l_lane_status.data);
    if (l_lane_status.init) free(l_lane_status.flags);
}
```

#### scan_lane (0x403f60)
`void scan_lane(t_lane_info *p_li, int p_wp, int p_range, int p_object, int p_is_my_lane)`

Fills dead-end info, then obstacles in the lane's own waypoint, then (if the lane continues for >1 waypoint)
searches up to p_range waypoints ahead for a front obstacle and 2 waypoints back for a rear obstacle.
```c
void scan_lane(t_lane_info *li, int p_wp, int p_range, int p_object, int p_is_my_lane)
{
    find_dead_end(li, p_wp, p_range);                                   /* "Dead End = %d, Range = %d." */
    if (li->dead_end == 0 || (li->dead_end == 1 && li->dead_range > 0))
        find_obstacle_here(li, p_wp, p_object, p_is_my_lane);
    if (li->dead_end == 0 || (li->dead_end == 1 && li->dead_range > 1)) {
        /* 'find_obstacle_front' */
        int wp = p_wp, n = p_range;
        if (li->front_obstacle == -1) {
            if (li->dead_end == 1) n = li->dead_range;
            if (n != 0) do {
                wp = WP(wp)->next /*+0x14*/;
                if (wp == -1) break;
                if (find_obstacle_in_wp(&li->front_obstacle, &li->front_range, wp, p_object) == 1) break;
            } while (--n);
        }
        /* 'find_obstacle_rear' */
        wp = p_wp; n = 2;
        if (li->rear_obstacle == -1) do {
            wp = WP(wp)->prev /*+0x20*/;
            if (wp == -1) break;
            if (find_obstacle_in_wp(&li->rear_obstacle, &li->rear_range, wp, p_object) == 1) break;
        } while (--n);
    }
}
```
Note: the front/rear searches only run when find_obstacle_here left the field at -1 (it always writes -1 or an
object). A searched-but-empty waypoint overwrites the field with -1 / 3.4e38 again.

#### find_dead_end (0x4040b0)
`void find_dead_end(t_lane_info *p_lane_info, int p_wp, int p_range)` (debug name/format at 0x4832d0)

Walks up to p_range waypoints forward, preferring link +0x14 (straight), then +0x1c (diagonal left), then +0x18
(diagonal right). If the chain ends (-1) before p_range steps: dead end at distance = steps taken.
```c
void find_dead_end(t_lane_info *li, int p_wp, int p_range)
{
    int wp = p_wp, left = p_range;
    if (p_range == 0) { li->dead_end = 0; li->dead_range = -1; return; }
    do {
        /* "Checking l_wp = %d." */
        if (wp == -1) { li->dead_end = 1; li->dead_range = p_range - left; return; }
        t_waypoint *w = WP(wp);
        wp = w->next /*+0x14*/;
        if (wp == -1) { wp = w->left /*+0x1c*/;
            if (wp == -1) { wp = w->right /*+0x18*/;
                if (wp == -1) continue;    /* NO decrement: next iteration hits the wp==-1 test */ } }
        left--;
    } while (left != 0);
    li->dead_end = 0;   /* (= left, which is 0) */
    li->dead_range = -1;
}
```
(`continue` in a do/while re-tests `left != 0`, which matches the binary: jump to the `test edi,edi`.)

#### find_obstacle_here (0x404150)
`void find_obstacle_here(t_lane_info *li, int p_wp, int p_object, int p_is_my_lane)`

Looks at the obstacle list of the lane's current waypoint and classifies each other object as in front of / behind
me (is_in_front), keeping a "nearest" front and rear obstacle. FAITHFULNESS NOTE: the source compares a stale
distance variable *before* computing the new distance; the compiler put that variable in the stack slot of the
p_object argument (p_object itself lives in a register), so its initial value is p_object's int bits read as a float
(a denormal >= 0, effectively 0). Port with `float l_dist = 0.0f` (equivalent, since all ranges compared are >= 0
real distances or 3.4e38) and keep the stale-compare order.
```c
void find_obstacle_here(t_lane_info *li, int p_wp, int p_object, int p_is_my_lane)
{
    int   l_front = -1, l_rear = -1;
    float l_front_range = FLT_BIG, l_rear_range = FLT_BIG;
    float l_dist = *(float*)&p_object;        /* stale variable, see note */
    t_waypoint *w = WP(p_wp);
    int l_count = w->obstacle_count /*+0x24*/, i, obj;
    vec3 me;

    if (p_is_my_lane == 1) {
        if (l_count == 0) { fatal("%s(%d): Zero obstacles in my waypoint is impossible!\n", "ai_lane.cpp", 572); goto out; }
        if (l_count == 1) {
            if (p_object != waypoint_get_object(w, 0))    /* sub_444260 */
                fatal("%s(%d): Obstacle found in my waypoint,\nbut it isn't me!\n", "ai_lane.cpp", 580);
            goto out;
        }
        me = *(vec3*)(O3D(OBJ(p_object)->o3d /*+0x08*/) + 0x50);
        for (i = 0; i < l_count; i++) {                    /* (count < 0 -> loop skipped) */
            obj = waypoint_get_object(w, i);
            if (obj == p_object) continue;
            if (is_in_front(obj, p_object) == 1) {          /* sub_404510 */
                if (l_dist < l_front_range) { l_front = obj; l_dist = dist3(obj, me); l_front_range = l_dist; }
            } else {
                if (l_dist < l_rear_range)  { l_rear  = obj; l_dist = dist3(obj, me); l_rear_range  = l_dist; }
            }
        }
    } else {
        me = position of p_object (as above);
        for (i = 0; i < l_count; i++) {
            obj = waypoint_get_object(w, i);
            if (obj == p_object)
                fatal("%s(%d): Found myself (object %d) in other lane!\n", "ai_lane.cpp", 651, p_object);  /* and continue processing it! */
            if (is_in_front(obj, p_object) == 1) {
                if (l_dist < l_front_range) { l_front = obj; l_dist = dist3(obj, me); l_front_range = l_dist; }
            } else {
                if (l_dist < l_rear_range)  { l_rear  = obj; l_dist = dist3(obj, me); l_rear_range  = l_dist; }
            }
        }
    }
out:
    li->front_obstacle = l_front; li->front_range = l_front_range;
    li->rear_obstacle  = l_rear;  li->rear_range  = l_rear_range;
}
/* dist3(obj, me): p = O3D(OBJ(obj)->o3d)+0x50; dx=p.x-me.x; dy=p.y-me.y; dz=p.z-me.z; sqrtf((dx*dx + dy*dy) + dz*dz) (x87, float stores) */
```

#### is_in_front (0x404510)
`int is_in_front(int p_other, int p_me)` (no debug name; called "find_obstacle..." helper)

Returns 1 when p_other lies in the half-plane ahead of p_me, using only x and z (ground plane) and p_me's forward
axis (row 2 of its object3d matrix).
```c
int is_in_front(int p_other, int p_me)
{
    char *m = O3D(OBJ(p_me)->o3d), *o = O3D(OBJ(p_other)->o3d);
    vec3 fwd = matrix_get_row2(m + 0x10);         /* sub_45e230 -> (m+0x30, m+0x34, m+0x38) */
    float d = (o[0x50] - m[0x50]) * fwd.x + (o[0x58] - m[0x58]) * fwd.z;   /* floats at +0x50 x, +0x58 z */
    return (d < 0.0f) ? 0 : 1;                     /* 0x47f0f0 = 0.0 */
}
```

#### find_obstacle_in_wp (0x404600)  ("find_obstacle_front / _rear" worker)
`int find_obstacle_in_wp(int *p_obstacle, float *p_range, int p_wp, int p_object)`

Nearest object (3-D distance) in a waypoint's obstacle list. Does not skip p_object itself.
```c
int find_obstacle_in_wp(int *p_obstacle, float *p_range, int p_wp, int p_object)
{
    int best = -1, i; float best_d = FLT_BIG;
    vec3 me = pos(p_object);
    t_waypoint *w = WP(p_wp);
    for (i = 0; i < w->obstacle_count /*+0x24*/; i++) {
        int obj = waypoint_get_object(w, i);
        float d = dist3(obj, me);
        if (d < best_d) { best_d = d; best = obj; }
    }
    *p_obstacle = best; *p_range = best_d;
    return best != -1;
}
```

#### analyse_lane (0x404720)
`void analyse_lane(t_lane_info *p_li, int p_object, int *p_ghost)` (prints the "l_lane_status.*" block in change_lane)

Derives m_front_status / m_rear_status (see enum). Direction test: dot product (x,z only) of the two cars'
forward axes; speeds are object+0x74.
```c
void analyse_lane(t_lane_info *li, int p_object, int *p_ghost)
{
    t_object *me = OBJ(p_object);
    *p_ghost = 0;
    if (li->front_obstacle != -1) {
        vec3 a = matrix_get_row2(O3D(me->o3d)+0x10), b = matrix_get_row2(O3D(OBJ(li->front_obstacle)->o3d)+0x10);
        if (b.x*a.x + b.z*a.z < 0.0f) { li->front_status = 5; *p_ghost = 1; }
        else li->front_status = (OBJ(li->front_obstacle)->speed > me->speed) ? 1 : 2;
    } else if (li->dead_end == 1) {
        li->front_status = (li->dead_range < 2) ? 4 : 3;
    } else li->front_status = 0;

    if (li->rear_obstacle != -1) {
        vec3 a = row2(me), b = row2(OBJ(li->rear_obstacle));
        if (b.x*a.x + b.z*a.z < 0.0f) li->rear_status = 0;
        else li->rear_status = (OBJ(li->rear_obstacle)->speed > me->speed) ? 2 : 1;
    } else li->rear_status = 0;
}
```

#### pick_lane (0x404960)  (traffic)
`int pick_lane(int p_object, int p_lane_count, T_ARRAY *p_lanes, int p_lane, int p_in_illegal_lane)`

Rule-based traffic lane choice ("keep right, overtake left"). `B` = BH(AI(OBJ(p_object)->ai)->behavior /*+0x24*/).
Thresholds: `B->front_clear` (+0x24, behavior 0: 300.0) minimum free distance ahead in the target lane,
`B->rear_clear` (+0x1c, behavior 0: 200.0) minimum distance to a same-direction car behind in the target lane.
Helper (inlined many times):
```c
#define REAR_OK(L)  ((L).rear_status == 0 || B->rear_clear < (L).rear_range)   /* strict < */
#define FRONT_OK(L) (B->front_clear <= (L).front_range)                        /* <= */
int pick_lane(int p_object, int p_lane_count, T_ARRAY *p_lanes, int cur, int p_in_illegal_lane)
{
    t_lane_info *L = p_lanes->data;
    if (p_in_illegal_lane == 1) {                          /* "In illegal lane..." ; uses L[cur-1] unchecked */
        switch (L[cur-1].front_status) {
        case 1: /* faster */ if (!FRONT_OK(L[cur-1])) return cur;      /* fall into free */
        case 0: /* free   */ return REAR_OK(L[cur-1]) ? cur-1 : cur;
        case 2: /* slower */ if (!FRONT_OK(L[cur-1])) return cur; return REAR_OK(L[cur-1]) ? cur-1 : cur;
        case 3: /* brake  */ return (L[cur].front_status >= 3) ? cur : cur-1;
        case 4: /* stop   */ return (L[cur].front_status >= 4) ? cur : cur-1;
        case 5: /* ghost  */ return (L[cur].front_status != 5) ? cur : cur-2;   /* sic: cur-2 */
        default: break;                                    /* fall through to the legal-lane logic */
        }
    }
    if (cur == p_lane_count - 1) {                         /* "Not in illegal lane -> in slowest lane." */
        switch (L[cur].front_status) {
        case 0: case 1: return cur;                        /* "free/faster, staying" */
        case 2: {                                          /* "Lane is slower, checking." */
            if (cur == 0) return 0;
            float l_front_speed = OBJ(L[cur].front_obstacle)->speed;
            switch (L[cur-1].front_status) {
            case 1: if (!FRONT_OK(L[cur-1])) return cur;   /* fall into 0 */
            case 0: return REAR_OK(L[cur-1]) ? cur-1 : cur;
            case 2: if (OBJ(L[cur-1].front_obstacle)->speed <= l_front_speed) return cur;
                    if (!FRONT_OK(L[cur-1])) return cur;
                    return REAR_OK(L[cur-1]) ? cur-1 : cur;
            case 3: case 4: case 5: return cur;
            default: return cur;                           /* "STAYING -> END." */
            } }
        case 3: case 4: case 5:                            /* "Lane is brake, stop or ghost, switching left." */
            if (cur == 0) return 0;
            switch (L[cur-1].front_status) {
            case 1: if (!FRONT_OK(L[cur-1])) return cur;   /* fall into 0 */
            case 0: return REAR_OK(L[cur-1]) ? cur-1 : cur;
            case 2: if (!FRONT_OK(L[cur-1])) return cur; return REAR_OK(L[cur-1]) ? cur-1 : cur;
            default: return cur;
            }
        default: return cur;                               /* "STAYING -> END." */
        }
    }
    /* normal (not slowest) lane */
    switch (L[cur].front_status) {
    case 2: case 3: case 4: case 5:                        /* try overtaking on the left */
        if (cur - 1 >= 0) {
            int s = L[cur-1].front_status;
            if (s == 0 || (s == 1 && FRONT_OK(L[cur-1])))
                if (REAR_OK(L[cur-1])) return cur - 1;
        }
        /* fall into try-right */
    case 0: case 1:                                        /* keep right */
        if (cur + 1 >= p_lane_count) fatal("SANITY_CHECK:\n%s(%d): Lanes fucked up!\n", "ai_lane.cpp", 1267);  /* continues */
        switch (L[cur+1].front_status) {
        case 1: if (!FRONT_OK(L[cur+1])) return cur;       /* fall into 0 */
        case 0: return REAR_OK(L[cur+1]) ? cur+1 : cur;
        default: return cur;                               /* 2..5 stay; >5 "STAYING -> END." */
        }
    default: return cur;                                   /* "STAYING -> END." */
    }
}
```

#### race_lane (0x404ee0)  (undetected function start; racers, also "at refuelling point")
`int race_lane(int p_object, int p_lane_count, T_ARRAY *p_lanes, int p_lane)`

Builds float copies of statuses/ranges, keeps lanes whose rear is safe and whose front is free or faster/slower with
enough room, then picks the lowest front status (ties between faster/slower lanes broken by larger front range).
```c
int race_lane(int p_object, int n, T_ARRAY *p_lanes, int p_lane)
{
    int b = AI(OBJ(p_object)->ai)->behavior; t_ai_behavior *B = BH(b);
    t_lane_info *L = p_lanes->data;
    if (n < 1) fatal("No LANES!!!");
    float *fs = malloc(n*4), *fr = malloc(n*4), *rs = malloc(n*4), *rr = malloc(n*4); int *cand = malloc(n*4);
    int i, k, nc = 0, nc2 = 0, best;
    for (i = 0; i < n; i++) { fs[i] = (float)L[i].front_status; fr[i] = L[i].front_range; }   /* "Lane %d, value = %d." */
    for (i = 0; i < n; i++) { rs[i] = (float)L[i].rear_status;  rr[i] = L[i].rear_range; }
    for (i = 0; i < n; i++) {
        if (rs[i] == 0.0f) cand[nc++] = i;
        if (rs[i] >  0.0f && B->rear_clear /*+0x1c*/ < rr[i]) cand[nc++] = i;
    }
    for (k = 0; k < nc; k++) {                    /* filtered in place */
        int c = cand[k];
        if (fs[c] == 0.0f) cand[nc2++] = c;
        if (fs[c] > 0.0f && fs[c] < 3.0f /*0x47f0f4*/ && B->front_clear /*+0x24*/ < fr[c]) cand[nc2++] = c;
    }
    if (nc2 == 0) best = p_lane;
    else {
        best = cand[nc2-1];
        for (k = 0; k < nc2-1; k++) {
            int c = cand[k];
            if (fs[best] == 1.0f || fs[best] == 2.0f)            /* bit compare 0x3f800000 / 0x40000000 */
                if (fs[c] == fs[best] && fr[c] > fr[best]) best = c;
            if (fs[c] < fs[best]) best = c;
        }
    }
    /* "Picked lane %d." */
    free(fs); free(fr); free(rs); free(rr); free(cand);
    return best;
}
```

#### cop_lane (0x4051f0)
`int cop_lane(int p_object, int n, T_ARRAY *p_lanes, int p_lane)` (p_lane unused)

Starts from the lane of the chased object (cop state +0x04) if it is on my side, else the right-most lane, then
prefers any lane 0..n-2 with a lower front status (tie-break as race_lane). No distance thresholds.
```c
int cop_lane(int p_object, int n, T_ARRAY *p_lanes, int p_lane)
{
    t_object *me = OBJ(p_object);
    int l_target = *(int*)(COP(AI(me->ai)->cop_index /*+0x44*/) + 4);
    t_lane_info *L = p_lanes->data; int l_side, l_lane, best, i;
    if (n < 1) fatal("No LANES!!!");
    float *fs = malloc(n*4), *fr = malloc(n*4);
    for (i = 0; i < n; i++) { fs[i] = (float)L[i].front_status; fr[i] = L[i].front_range; }
    find_waypoint_side_lane(OBJ(l_target)->chunk, OBJ(l_target)->waypoint, &l_side, &l_lane);   /* sub_402520 */
    if (l_side == me->side) best = rceng_get_valid_lane(me->chunk, me->side, l_lane);          /* sub_442730 */
    else                    best = n - 1;
    for (i = 0; i < n - 1; i++) {                                 /* last lane never iterated */
        if (fs[best] == 1.0f || fs[best] == 2.0f)
            if (fs[i] == fs[best] && fr[i] > fr[best]) best = i;
        if (fs[i] < fs[best]) best = i;
    }
    free(fs); free(fr);
    return best;
}
```
(If sub_402520 fails it leaves l_side/l_lane uninitialised.)

#### adac_lane (0x4053f0)
`int adac_lane(int p_object, int n, T_ARRAY *p_lanes, int p_lane)` - identical to cop_lane but the target is the
player object `g_player_object` (0x4d9e28) instead of the cop's chased object. Not reachable through any known
change_lane caller (no caller passes driver type 3).

#### find_waypoint_side_lane (0x402520, ai_cops.cpp, used by cop/adac lane)
`void (int p_chunk, int p_wp, int *p_side, int *p_lane)`: searches CHUNK(p_chunk)->wp_side0[0..lanes0) -> side 0,
then wp_side1[0..lanes1) -> side 1, writes side and lane index; if not found
`fatal("%s(%d): Waypoint %d not found in chunk %d.\n", "ai_cops.cpp", 163, p_wp, p_chunk)` and leaves outputs untouched.

#### adjust_speed (0x4055c0)
`float adjust_speed(int p_object, t_lane_info *p_li, float p_speed)`

Desired speed for a lane. Thresholds from the behavior: `B->too_close` (+0x20, behavior 0: 200.0) and
`B->front_clear` (+0x24, behavior 0: 300.0); top speed `B->top_speed` (+0x14).
```c
float adjust_speed(int p_object, t_lane_info *li, float p_speed)
{
    t_ai_behavior *B = BH(AI(OBJ(p_object)->ai)->behavior);
    switch (li->front_status) {
    case 0: return B->top_speed;                                       /* "Lane is free." */
    case 1:                                                            /* "Lane is faster." */
    case 2:                                                            /* "Lane is slower." (same logic) */
        if (li->front_range < B->too_close)                            /* !(too_close <= range) */
            return OBJ(li->front_obstacle)->speed * 0.75f;             /* "Too close, braking a little." 0x47f0f8 */
        if (li->front_range < B->front_clear)
            return OBJ(li->front_obstacle)->speed;                     /* "Close, matching speed." */
        return B->top_speed;                                           /* "Far away" */
    case 3: { float s = p_speed * 0.75f; if (s < 2.0f) s = 2.0f; return s; }   /* "Lane is brake, slowing down to %f." 0x47f0fc */
    case 4: return 2.0f;                                               /* "Lane is stop, halting returning 0." (returns 2.0!) */
    default: return B->top_speed;                                      /* status 5 (ghost) etc: "ERROR! Returning %f." */
    }
}
```

#### switch_lane_right (0x406d10) / switch_lane_left (0x406d90)
`int switch_lane_right(int p_ai)` / `int switch_lane_left(int p_ai)` - re-target the AI's waypoint to the parallel
waypoint in the neighbouring lane: previous waypoint (+0x20) of the current target, then its diagonal link
(+0x18 right / +0x1c left).
```c
int switch_lane_right(int p_ai)            /* left: identical with ->left (+0x1c) */
{
    t_ai *ai = AI(p_ai);
    int wp = ai->wp /*+0x00*/;
    int prev = WP(wp)->prev /*+0x20*/;            if (prev == -1) return 0;
    int nb = WP(prev)->right /*+0x18*/;           if (nb == -1)   return 0;
    if (wp == nb) return 0;
    if (ai->wp2 /*+0x04*/ == wp) ai->wp2 = wp;   /* sic: no-op in the binary */
    ai->wp = nb;
    ai->field_0c = 0;
    return 1;
}
```

#### T_ARRAY_init (0x405890) / T_ARRAY_free (0x405910) - thiscall on a T_ARRAY
```c
void T_ARRAY_init(T_ARRAY *a) {           /* "T_ARRAY : init %s %s[%d] (no alloc)" */
    if (a->init) { if (a->init == 1) free(a->data); if (a->init) { free(a->flags); a->init = 0; } }
    a->data = 0; a->count = 0; a->last = 0; a->flags = 0; a->init = 0;
}
void T_ARRAY_free(T_ARRAY *a) {           /* "T_ARRAY : deleting ..." */
    if (a->init == 1) free(a->data);
    if (a->init) { free(a->flags); a->init = 0; }
}
```
T_ARRAY layout: +0x00 data, +0x04 count, +0x08 last used, +0x0c init flag (1 = owns data), +0x10 flags (byte per
element), +0x14 char name[25], +0x2d char type_name[25] (struct size 0x46 -> 0x48).

---

### Waypoint obstacle lists
* `sub_444260` = `waypoint_get_object(int i)` (road naming; thiscall) (thiscall, ecx = waypoint, rd_drmap.cpp): if `i < 0 || i > 10`
  `fatal("%s(%d) - obstacle (%d) waypoint overflow (max %d)\n", "rd_drmap.cpp", 0xec, i, 10)`; returns
  `*(int*)(wp + 0x28 + i*4)` = object index. Count at wp+0x24. Room for 10 ints (+0x28..+0x4f); the check
  admits i == 10 (would read the next waypoint's +0x00 - off-by-one, never hit when count <= 10).
* `sub_45e230` = `matrix_get_row2(out, m)`: returns `*(vec3*)(m + 0x20)` (sret). Called with `object3d + 0x10`
  (the object's 4x4 matrix), so it yields floats at object3d+0x30/+0x34/+0x38 = the Z (forward) axis. Siblings:
  sub_45e1b0 row0 (m+0x00), sub_45e1f0 row1 (m+0x10).

### Thresholds / constants
| value | where | meaning |
|---|---|---|
| ai+0x10 (init 10) | change_lane -> scan_lane p_range | waypoints scanned ahead (dead end + front obstacle) |
| 2 | scan_lane | waypoints scanned behind for a rear obstacle |
| dead_range < 2 | analyse_lane | STOP (4) else BRAKE (3) |
| dead_range > 0 / > 1 | scan_lane | do find_obstacle_here / front+rear search |
| 0.0 (0x47f0f0) | is_in_front, analyse_lane, race_lane | direction sign / status compares |
| 3.4e38 (0x7f7fc99e) | find_obstacle_here, find_obstacle_in_wp | "no obstacle" range |
| bh+0x1c (beh0 200.0) | pick_lane, race_lane | rear clearance (strict `<` rear_range) |
| bh+0x24 (beh0 300.0) | pick_lane (`<=`), race_lane (`<`), adjust_speed | front clearance / "close" distance |
| bh+0x20 (beh0 200.0) | adjust_speed | "too close" distance |
| bh+0x14 (beh0 50.0) | adjust_speed | bh_top_speed |
| 0.75 (0x47f0f8) | adjust_speed | brake factor |
| 2.0 (0x47f0fc) | adjust_speed | min speed when brake / speed when stop |
| 3.0 (0x47f0f4) | race_lane | status < 3 (faster/slower) acceptable |
| 0.5 (0x47f0e8) | change_lane | averaging of speeds of current and chosen lane |
| 41 (0x29) | change_lane | object sound sample checked before restoring ai_old_sample |
| 10 | sub_444260 | max obstacles per waypoint |

### Structures
| struct | off | type | name | evidence |
|---|---|---|---|---|
| t_object (0xa0) | 0x08 | int | o3d index | 0x4041bc, 0x404524 |
| t_object | 0x0c | int | ai index | 0x4036d5, 0x404987 |
| t_object | 0x14 | int | current sound sample (set by sub_4289e0) | 0x403ea9, 0x428a5e |
| t_object | 0x18 | int | chunk record index (used directly *0x184) | 0x4036d1, 0x402533 |
| t_object | 0x20 | int | waypoint | 0x4036cd, 0x4052fc |
| t_object | 0x28 | int | side | 0x4036c9, 0x405316 |
| t_object | 0x74 | float | speed | 0x4047ff, 0x4056cf |
| object3d (stride 0x99!) | 0x10 | float[4][4] | matrix (row2 = +0x30 forward) | 0x404560, 0x45e230 |
| object3d | 0x50/0x54/0x58 | float | position x,y,z | 0x4041d2 |
| t_chunk (0x184) | 0xe4/0xe8 | int | lane count side0/side1 | 0x403711 |
| t_chunk | 0x124 / 0x154 | int[] | waypoint per lane side0 / side1 | 0x403721 |
| t_waypoint (0x50) | 0x14 | int | next (straight) | 0x4040f7, 0x404024 |
| t_waypoint | 0x18 | int | diagonal next, right lane (lane+1) | 0x404107, 0x406d44 |
| t_waypoint | 0x1c | int | diagonal next, left lane (lane-1) | 0x4040ff, 0x406dc4 |
| t_waypoint | 0x20 | int | previous | 0x40407c, 0x406d35 |
| t_waypoint | 0x24 | int | obstacle count | 0x404181 |
| t_waypoint | 0x28 | int[10] | obstacle object indices | 0x44428b |
| t_ai (0x6c) | 0x00 | int | ai_wp: current target waypoint | 0x406d2c |
| t_ai | 0x04 | int | second waypoint (compared/no-op assign in switch) | 0x406d51 |
| t_ai | 0x0c | int | reset to 0 on lane switch | 0x406d6e |
| t_ai | 0x10 | int | scan range in waypoints (init 10) | 0x4039fb |
| t_ai | 0x14 | int | ai_chosen_lane | 0x403c1f |
| t_ai | 0x1c | float | ai_chosen_speed | 0x403da3 |
| t_ai | 0x24 | int | behavior index | 0x404991 |
| t_ai | 0x38 | int | racer driver type (1/5/6/7) passed by sub_40aab0 | 0x40adff |
| t_ai | 0x3c | int | traffic lanes allowed (init 2) passed by sub_40b770 | 0x40b8df |
| t_ai | 0x44 | int | cop index (-1 = not a cop) | 0x403e93, 0x40521f |
| t_ai | 0x48 | int | ai_old_sample | 0x403eaf |
| t_ai | 0x4c | int | set 1 when old sample restored | 0x403ece |
| t_ai_behavior (0x2c) | 0x14 | float | bh_top_speed | 0x405634 |
| t_ai_behavior | 0x1c | float | rear clearance (beh0 200) | 0x404a3c |
| t_ai_behavior | 0x20 | float | too-close distance (beh0 200) | 0x405655 |
| t_ai_behavior | 0x24 | float | front clearance / close distance (beh0 300) | 0x4049fd, 0x4056dd |
| t_cop (0x20) | 0x04 | int | chased (target) object | 0x40522c |
| t_lane_info (0x20) | see table above | | | 0x482e30 |
| T_ARRAY | 0x00..0x2d | | see T_ARRAY_init | 0x405890 |

### Globals
| addr | type | name | meaning |
|---|---|---|---|
| 0x482b58 | t_lane_info | g_default_lane_info | {0,-1,-1,-1.0f,0,-1,-1.0f,0} copied into each new element |
| 0x4d9ac4 | int | g_direction | Game.ini [ETAPPE] DIRECTION; tested in change_lane but both branches identical |
| 0x4d9e28 | int | g_player_object | target of adac_lane |
| 0x4a5238 | t_cop* | cop states | +4 target object |
| 0x4eac0c / 0x4eac10 | int | debug output channel/level | set by sub_464c80, read by debug printf |
| 0x47f0e8/0x47f0f0/0x47f0f4/0x47f0f8/0x47f0fc | float | 0.5 / 0.0 / 3.0 / 0.75 / 2.0 | constants |

### Open questions
* change_lane case 7 ("Racer refuelling") and default use `l_chosen_lane = p_object` - reproduced as-is (source bug?).
  Effect: ai_chosen_lane is stepped towards the object index and clamped to [0, lane_count-1].
* l_lane is uninitialised when the waypoint is not on the object's own side (other side / drmap_find_closest_waypoint path);
  a port must pick something (e.g. 0) - the original uses stack garbage.
* find_obstacle_here compares a stale distance before computing the new one (see note) - keep this order.
* pick_lane illegal-lane branch reads L[cur-1] without checking cur > 0 and can return cur-2 (ghost case);
  change_lane's clamping (and one-step switching) makes these harmless.
* Units: ranges are 3-D world distances between object3d positions; speeds are object+0x74.
* Behavior-table values for behaviors other than 0 are filled in sub_405bc0 (core section).


## 6. FSM framework (ai_fsm.cpp, ai_cfun.cpp): transitions, conditions, triggers, ranking


Conventions used below:
- `OBJ(o)` = `((char*)g_objects /*[0x4e48e0]*/ + o*0xa0)` (game object, stride 0xa0).
- `O3D(o)` = `((char*)g_object3d /*[0x4ea9e0]*/ + OBJ(o)->o3d_index(+0x08) * 0x99)` (object3d, stride 0x99 = 153).
  Fields: +0x50 x, +0x54 y, +0x58 z, +0x78 heading (float, 4096 units per turn), +0x10..+0x4f matrix (16 floats; +0x30/+0x38 = forward x/z).
- `WP(w)` = `g_driver_map /*[0x4e6628]*/ + w*0x50` (waypoint). +0x04 trigger, +0x08/+0x0c/+0x10 pos x/y/z, +0x14 +0x18 +0x1c +0x20 links, +0x24 obstacle count.
- `CHUNK(c)` = `g_chunks /*[0x4e65e0]*/ + c*0x184` (NOTE: indexed directly by chunk number here, no 0x4e5d2c map). +0xec trigger_a, +0xf0 trigger_b.
- `fsm[i]` = `t_fsm g_fsm[15]` at 0x4a55a0, stride 0x60. Entries 0..2 racers, 3..14 cops.
- `rinfo[i]` = racer info at **0x485b20**, stride 0x58, 3 entries (field +0x08 = 0x485b28 etc.).
- `dt()` = `sub_441cd0(this=0x4e4970)` clamped frame time (s). `rand()` = sub_470120. `ftol` = sub_4700e0 (truncation).
- `dist2(a,b)` below always means `(ax-bx)^2 + (az-bz)^2` on object3d +0x50/+0x58 (y ignored), computed in x87 (80-bit) and compared against a float constant.
- Float compares: `fcomp; test ah,0x41; jne` = "jump if st0 <= m"; `test ah,1; je` = "jump if st0 >= m". NaN not considered.

### Structures

#### t_fsm (0x4a55a0, 15 x 0x60)
| off | type | name | evidence |
|---|---|---|---|
| +0x00 | int | object (-1 = slot unused) | 403340, 4093b0 (cops := -1), 4034a0, 4035b0 |
| +0x04 | int | state (0..17, see state table) | 403380, 408a50, 4088f0, 408ed0, 40a190, 40a390 |
| +0x08 | int | target object (-1 none). Cops: chased car. Racers: player (set by cond 401780) | 403320, 4034a0, 401960.. |
| +0x0c | int | laps completed (incremented at finish trigger 14 after halfway trigger 15) | 4088f0 0x408956, 403320 |
| +0x10 | float | state timer (s) (4018b0/401780: <1 s, 401b00: 4 s, 401e20: 30/60 s, state 2 handler, state 17 handler) | 401780, 401b00, 401e20, 409e00, 40a390, 40a8a0 |
| +0x14 | float | time in current chunk (s) (stuck detection); reset to 0 when chunk changes | 409540, 402050, 401ff0, 40a190, 40a6a0, 40aab0 |
| +0x18 | float | reverse timer (state 10) | 40a190 |
| +0x1c | int | twp: target waypoint for the world mapper (-1 = none) | 409500, 409d10 ("...world mapper with twp %d") |
| +0x20 | int | current chunk (copy of object +0x18) | 409500, 409540 |
| +0x24 | int | previous chunk (-1 initially) | 409500, 409540 |
| +0x28 | int | last lap trigger seen (0 / 14 / 15) | 409500, 4088f0 |
| +0x2c | float | speed limit from last speed trigger (12.5 / 17.5 / 25 / 30) | 409500, 4088f0, 409060 |
| +0x30 | int | car model (racers: set by sub_411170; cops: player car model [0x4d9e2c] at chase start). Also start of the "driving control" sub-struct cleared by sub_401200 (this=fsm+0x30 clears +0x34..+0x58) | 4034a0, 4093b0, 4108d0 |
| +0x34 | float | throttle-like value 0..100 (state 9: += 75*dt capped 100; state 10: := 0; used as factor in sub_410280) | 409ed0, 40a190, 410280 |
| +0x38 | float | control value (set 0 together with +0x3c) | 409e00, 409f60, 40a610, 40a6a0 |
| +0x3c | float | brake-like value 0..100 (state 4: 50 / 100; states 14/16: += 400*dt capped 100) | 409f60, 40a6a0, 40aab0 |
| +0x40 | int | flag (1 in states 9, 15; 0 at start of 409d10) | 409d10, 409ed0, 40a9d0 |
| +0x44 | ? | (used by sub_40eae0 / sub_40ff90, other part) | |
| +0x48,+0x4c,+0x50 | float | used by sub_410280 (other part) | |
| +0x58 | ? | used by sub_40ff90 | |
| +0x5c | float | difficulty bonus = 4.0*clamp(player_model + upgrades - car_model, 0, 3) (sub_4108d0, racers only) | 4108d0 0x4109a2 |

Two helper "constructors" (thiscall):
- `sub_403320(this=&fsm, state)`: `+0x08=-1; +0x04=state; +0x10=0; +0x14=0; +0x0c=0;`
- `sub_409500(this=&fsm+0x1c, i)`: `+0x1c=-1; +0x20=OBJ(fsm[i].object)->chunk; +0x24=-1; +0x28=0; +0x2c=0 (int 0 = 0.0f)`.
- `sub_401200(this=&fsm+0x30)`: zeroes this+0x18,+0x1c,+0x20,+0x24,+0x28,+0x04,+0x08,+0x0c,+0x14,+0x10 (i.e. fsm+0x34..+0x58), not +0x30 / +0x5c.

#### racer info (0x485b20, 3 x 0x58) - fields used here
| off | abs (entry 0) | type | name | evidence |
|---|---|---|---|---|
| +0x08 | 0x485b28 | int | race position, 0 = leading (computed by sub_408cb0) | 408cb0, 401780, 4018b0, 401b60 |
| +0x0c | 0x485b2c | int | object index of the racer | 408c00, 408cb0, 409540 |
| +0x10 | 0x485b30 | int | finished flag | 4088f0, sub_411650 |
(also: +0x00 car model, +0x04 finish time, see section 9.)

#### t_fsm_transition (16 bytes) - racers 0x4a5410[25], cops 0x4a5280[25]
| off | type | name |
|---|---|---|
| +0x00 | int | from_state (-1 = unused entry) |
| +0x04 | int | to_state |
| +0x08 | int | expected result of cond |
| +0x0c | int (*)(int fsm_index) | cond |
Both tables are cleared by static initialisers (CRT init table 0x482008 -> sub_4032b0 racers, 0x48200c -> sub_403300 cops): `from=-1, to=-1, expected=0` for all 25 (cond stays NULL from BSS).

#### other
- t_ai (+0x60 int): "chunk progress" (number of chunks driven, signed) for racer cars; updated in 409540, zeroed in 4093b0, compared in 408c00/408cb0 against g_player_progress 0x4d9e70.
- world mapper slots 0x4a5c08, 8 x 0x18 (sub_40be40 init: +0,+4,+8 = -1, +0x10,+0x14 = 0). sub_40bf70(h) returns slot[h].+8 (waypoint) (-1 if h invalid).

### FSM states (names from transitions / handlers)

| st | name | handler (408a50) | who | meaning |
|---|---|---|---|---|
| 0 | FSM_RACE | 0x409d10 | racer | normal racing along the path (world mapper from own pos; overtake check sub_409c10) |
| 1 | FSM_RACE_PLAYER_BEHIND | 0x409d10 | racer | leading racer with player close behind (same driving as 0, armed for 2) |
| 2 | FSM_BLOCK_PLAYER | 0x409e00 | racer | world mapper aimed at target (player): cut off / block, lasts ~1 s (+0x10) |
| 3 | FSM_COP_CHASE | 0x409df0 (=409d10) | cop | chase: follow road behind target; initial cop state (sub_4034a0) |
| 4 | FSM_COP_TARGET_BEHIND | 0x409f60 | cop | target behind cop: cop brakes (+0x3c 50/100) to let target close in / block |
| 5 | FSM_COP_ARREST | 0x40a390 | cop | one-shot arrest: siren off; player -> caught sequence sub_424d20(8); racer -> state 7; all cops on that target -> 6 |
| 6 | FSM_COP_ARRESTED_WAIT | 0x40a480 | cop | stand still (speed,+0x6c,+0x7c = 0) for 4 s |
| 7 | FSM_RACER_ARRESTED | 0x40a610 | racer | stopped by police, 4 s, then back to 0 |
| 8 | FSM_COP_PARKED | 0x40a4f0 | cop | decelerate (v -= 3*v*dt); released (sub_4035b0) when >30 chunks from the player |
| 9 | FSM_FINISHED | 0x409ed0 | both | race over (finish trigger) / cop at finish of a non-lap track: coast, +0x34 ramps to 100 |
| 10 | FSM_STUCK_REVERSE | 0x40a190 | both | stuck >3 s (2 s) in a chunk: speed := -15 for 1 s, then state 0 (racer) / 3 (cop) |
| 11 | FSM_RACER_WAIT | 0x40a290 | racer | rubber band: racer far ahead of player, waits until dist^2 < 1.5e7 |
| 12,13 | (illegal) | 'Illegal state. Function: HandleAIObject' | | |
| 14 | FSM_COP_TARGET_WRONG_WAY | 0x40aab0 | cop | target reversing / driving against track direction: follow, brake if ahead and faster |
| 15 | FSM_COP_GIVE_UP | 0x40a9d0 | cop | chase abandoned (30/60 s or >50 chunks): drive on, then stop (->8) |
| 16 | FSM_COP_TARGET_OTHER_SIDE | 0x40a6a0 | cop | target drives along track but against its waypoint links (other carriageway) |
| 17 | FSM_COP_DEPART | 0x40a8a0 | cop | after arrest: drive away at speed 10 while target behind, 3 s, then stop (->8) |

Every handler call is followed by `sub_409540(i)` (also after the illegal-state message).

### Transition tables (complete)

#### Racers 0x4a5410 (filled by sub_4093b0 via sub_403420)
| idx | from | to | expected | cond | note |
|---|---|---|---|---|---|
| 0 | 0 | 11 | 2 | 0x401b60 cfun_racer_too_far | only if `!g_demo` (0x4d9b20) |
| 1 | 11 | 0 | 1 | 0x401cf0 cfun_racer_near_player | only if `!g_demo` |
| 2 | 7 | 0 | 1 | 0x401b00 cfun_wait_4s | |
| 3 | 0 | 10 | 1 | 0x402050 cfun_stuck_3s | |
| 4 | 0 | 1 | 1 | 0x401780 cfun_player_behind_leader | |
| 5 | 1 | 2 | 1 | 0x4018b0 cfun_random_block | |
| 6 | 2 | 0 | 0 | 0x401780 cfun_player_behind_leader | |
| 7..24 | -1 | -1 | 0 | NULL | unused (in demo mode also 0,1) |

#### Cops 0x4a5280 (filled by sub_409200 via sub_403460)
| idx | from | to | expected | cond |
|---|---|---|---|---|
| 0 | 3 | 4 | 1 | 0x401960 cfun_target_behind_cop |
| 1 | 4 | 3 | 0 | 0x401960 cfun_target_behind_cop |
| 2 | 4 | 5 | 1 | 0x4019f0 cfun_can_arrest |
| 3 | 3 | 5 | 1 | 0x4019f0 cfun_can_arrest |
| 4 | 5 | 6 | 1 | 0x434140 return_true (`return 1;`) |
| 5 | 6 | 17 | 1 | 0x401b00 cfun_wait_4s |
| 6 | 17 | 8 | 1 | 0x401ef0 cfun_stopped |
| 7 | 3 | 16 | 1 | 0x401f60 cfun_target_other_side |
| 8 | 4 | 16 | 1 | 0x401f60 cfun_target_other_side |
| 9 | 16 | 3 | 1 | 0x402080 cfun_target_back_on_side |
| 10 | 4 | 14 | 1 | 0x401d70 cfun_target_not_forward |
| 11 | 16 | 14 | 1 | 0x401d70 cfun_target_not_forward |
| 12 | -1 | -1 | 0 | NULL (never set) |
| 13 | 14 | 3 | 1 | 0x401d90 cfun_target_forward_near |
| 14 | 16 | 10 | 1 | 0x401ff0 cfun_cop_stuck |
| 15 | 3 | 10 | 1 | 0x402050 cfun_stuck_3s |
| 16 | 3 | 15 | 1 | 0x401e20 cfun_give_up |
| 17 | 15 | 8 | 1 | 0x401ef0 cfun_stopped |
| 18 | 3 | 14 | 1 | 0x401fb0 cfun_target_reversing |
| 19 | 4 | 14 | 1 | 0x401fb0 cfun_target_reversing |
| 20 | 16 | 14 | 1 | 0x401fb0 cfun_target_reversing |
| 21..24 | -1 | -1 | 0 | NULL |

Evaluation order matters: entries are scanned 0..24, the first entry with `from==state` whose `cond(i)==expected`
wins; every cond with matching `from` that precedes it is CALLED (side effects: timers, siren, speech). Out-of-table
state changes: 4034a0 (->3), 4088f0 (->9), 408ed0 (cops ->9), 40a190 (10->0/3), 40a390 (racer ->7, cops ->6).

---

### FSM framework (ai_fsm.cpp)

#### sub_4032b0 / sub_403300 - static init of the transition tables (0x4032b0, 0x403300)
```c
void ai_fsm_clear_racer_transitions(void) /*0x4032b0*/ { for (k=0;k<25;k++){ g_racer_trans[k].from=-1; .to=-1; .expected=0; } }
void ai_fsm_clear_cop_transitions(void)   /*0x403300*/ { for (k=0;k<25;k++){ g_cop_trans[k].from=-1;   .to=-1; .expected=0; } }
```

#### sub_4032d0 - array constructor helper (0x4032d0)
`void __stdcall vector_ctor(void *p, int size, int count, void (__thiscall *ctor)(void*))` - calls ctor(p), p+=size, count times (MSVC `eh vector constructor iterator`-like; ret 0x10).

#### sub_403420 / sub_403460 - set transition (0x403420 racers, 0x403460 cops)
```c
int ai_fsm_set_racer_transition(int idx, int from, int to, int expected, int (*cond)(int)) /*0x403420*/
{
    if (idx >= 25) return 0;
    g_racer_trans[idx].from = from; .to = to; .expected = expected; .cond = cond;   /* 0x4a5410 + idx*16 */
    return 1;
}
/* 0x403460 identical on g_cop_trans (0x4a5280) */
```

#### sub_403340 - check transitions of all FSMs (0x403340)
Called each frame from the main loop when `ai_model == 0`, before sub_408ed0.
```c
void ai_fsm_check_all_transitions(void)
{
    int siren_done = 0;
    for (i = 0; i < 15; i++) {
        if (g_fsm[i].object == -1) continue;
        ai_fsm_check_transitions(i);                 /* 0x403380 */
        if (i >= 3 && !siren_done) {                 /* first active cop slot */
            cop_siren_light_update();                /* sub_40c860 */
            siren_done = 1;
        }
    }
}
```
sub_40c860 (not mine): if `[0x4a5d30]` (a siren is on) and GetTickCount()-[0x4a5d40] >= 200 ms: [0x4a5d40]=now; sub_42ae90(14, [0x4a5d3c]); [0x4a5d3c] = ![0x4a5d3c].

#### sub_403380 - evaluate the transition table for one FSM (0x403380)
```c
void ai_fsm_check_transitions(int i)
{
    t_fsm *f = &g_fsm[i];
    if (i < 3)              sndfx_play_object_sample(f->object, 1);    /* sub_428bc0 */
    else if (f->state == 9) sndfx_play_object_sample(f->object, 0);
    else                    sndfx_play_object_sample(f->object, 1);

    t_fsm_transition *tab = (i < 3) ? g_racer_trans /*0x4a5410*/ : g_cop_trans /*0x4a5280*/;
    for (k = 0; k < 25; k++) {
        if (tab[k].from == f->state && tab[k].cond(i) == tab[k].expected) {
            f->state = tab[k].to;
            break;                               /* k = 25 */
        }
    }
}
```
Note: `cond` is only called when `from == state`, so NULL entries (from=-1) are never called.

#### sub_4034a0 - cop starts chasing a car (0x4034a0)
Called from sub_409060. Allocates a free cop FSM slot (3..14), limited cops per target.
```c
int ai_cop_start_chase(int cop_obj, int target_obj)
{
    int max_cops;
    if (target_obj == g_player_object /*0x4d9e28*/) max_cops = g_difficulty /*0x4d9ab0*/ ? 3 : 1;
    else                                             max_cops = 2;

    if (g_maps[g_track].is_city /* *(int*)(0x4d8e9c + track*0x89) */ == 1) {
        float p = dt() * 1000.0f;
        int r = rand() % 1000;
        if ((float)r > p) return 0;          /* in city: chance dt per call */
    }
    int s = 3;
    while (s < 15 && g_fsm[s].object != -1) s++;      /* first free cop slot */
    if (g_fsm[s].object != -1) return 0;               /* s==15 reads 0x4a5b40 (t_ai data ptr) != -1 -> 0 */
    int n = 0;
    for (k = 3; k < 15; k++) if (g_fsm[k].target == target_obj) n++;
    if (n >= max_cops) return 0;

    g_fsm[s].object = cop_obj;
    g_fsm[s].state  = 3;                       /* FSM_COP_CHASE */
    g_fsm[s].target = target_obj;
    g_fsm[s].car_model = g_player_car_model;   /* [0x4d9e2c] -> +0x30 */
    cop_speech_start_chase(target_obj);        /* sub_4150f0: random of 13 samples, always for player, 1/3 otherwise */
    cop_siren(cop_obj, 1);                     /* sub_40c640 */
    return 1;
}
```

#### sub_4035b0 - release cop FSM (0x4035b0) (called by state 8)
```c
void ai_fsm_release_cop(int i)
{
    if (i < 3) { fatal("Illegal cop, this is a racer: %s %d", "ai_fsm.cpp", 0x18c); return; }
    for (k = 0; k < 20; k++) if (g_cop_list_4a5ce0[k] == g_fsm[i].object) { g_cop_list_4a5ce0[k] = -1; break; }
    g_fsm[i].object = -1; g_fsm[i].state = -1; g_fsm[i].target = -1;
    g_fsm[i].timer = 0; g_fsm[i].time_in_chunk = 0; g_fsm[i].laps = 0;
}
```

---

### Condition functions (ai_cfun.cpp, 0x401690..0x4020c5)

0x401690 is NOT one 2624-byte function: it is the angle helper followed by the condition functions listed below
(the tool merged them). All conds take the FSM index `i`; `me = g_fsm[i].object`, `tgt = g_fsm[i].target`.

#### sub_401690 - angle of a point relative to an object's heading (0x401690)
Returns 0..4096 (int, truncated): 0 = straight ahead, 2048 = directly behind (consistent with sub_408780).
```c
int ai_get_angle_to_point(int obj, float *pos /* x at [0], z at [2] */)
{
    char *o = O3D(obj);
    float dz = pos[2] - o[+0x58];               /* stored as float */
    long double dx = pos[0] - o[+0x50];
    if (dx > 0 && dx < 0.1f)  dx = 0.1f;
    if (dx < 0 && dx > -0.1f) dx = -0.1f;      /* dx == 0 stays 0 -> dz/0 = +-inf -> atan = +-pi/2 */
    long double a = atan(dz / dx) * 0.15915494f /*1/2pi, 0x3e22f983*/ * 4096.0f;
    if (dx > 0) a += 3072.0f; else a += 1024.0f;   /* (fsub -3072 / fsub -1024) */
    a -= o[+0x78];                                 /* heading */
    if (a > 4096.0f) a -= 4096.0f;
    if (a < 0.0f)    a += 4096.0f;   /* fsub -4096 */
    return ftol(a);
}
```

#### 0x401780 cfun_player_behind_leader (racer 0->1 exp 1; 2->0 exp 0)
```c
int cfun_player_behind_leader(int i)
{
    if (g_fsm[i].timer > 1.0f) { g_fsm[i].timer = 0; return 0; }
    float p[3] = player o3d x,y,z;
    if (dist2(me, player) < 10000.0f) {
        int a = ai_get_angle_to_point(me, p);
        if (a > 1500 && a < 2500) {               /* 0x5dc, 0x9c4: player behind */
            g_fsm[i].target = g_player_object;
            if (rinfo[i].position == 0) return 1;  /* only the leader */
        }
    }
    return 0;
}
```

#### 0x4018b0 cfun_random_block (racer 1->2 exp 1)
```c
int cfun_random_block(int i)
{
    if (rinfo[i].position != 0) return 0;
    if (OBJ(me)->speed /*+0x74*/ < 30.0f) return 0;
    float p = dt() * 50.0f;
    int r = rand() % 100;
    if ((float)r < p && g_fsm[i].timer < 1.0f) return 1;
    g_fsm[i].timer = 0;
    return 0;
}
```

#### 0x401960 cfun_target_behind_cop (cop 3->4 exp 1, 4->3 exp 0)
```c
int cfun_target_behind_cop(int i)
{
    float p[3]; p[0] = O3D(tgt)->x; p[2] = O3D(tgt)->z;   /* p[1] uninitialised, unused */
    int a = ai_get_angle_to_point(me, p);
    return (a > 1700 && a < 2300);              /* 0x6a4, 0x8fc */
}
```

#### 0x4019f0 cfun_can_arrest (cop 3->5, 4->5 exp 1)
```c
int cfun_can_arrest(int i)
{
    if (tgt == g_player_object) {
        if (g_game_mode /*0x4d9a7c*/ == 3) return 0;
    } else {
        int k; for (k = 0; k < 3 && g_fsm[k].object != tgt; k++) ;
        if (k == 3) fatal("Racer not found! File:%s Line:%d", "ai_cfun.cpp", 0x134);  /* falls through */
        else if (g_fsm[k].state == 9) return 0;          /* finished racers can't be arrested */
    }
    if (fabs(OBJ(tgt)->speed /*+0x74*/) + fabs(OBJ(tgt)->+0x6c) < 7.0f
        && dist2(me, tgt) < 5000.0f) return 1;
    return 0;
}
```

#### 0x401b00 cfun_wait_4s (racer 7->0, cop 6->17; exp 1)
```c
int cfun_wait_4s(int i)
{
    if (g_fsm[i].timer < 4.0f) { g_fsm[i].timer += dt(); return 0; }
    g_fsm[i].timer = 0;
    cop_siren(me, 0);                            /* sub_40c640 (also called for racers) */
    return 1;
}
```

#### 0x401b60 cfun_racer_too_far (racer 0->11 exp 2)
```c
int cfun_racer_too_far(int i)
{
    float thr[4] = { 2.0e7f, 2.0e7f, 1.75e7f, 1.5e7f };   /* 0x4b989680,0x4b989680,0x4b8583b0,0x4b64e1c0 */
    if (i < 0) fatal("%s (%d) Invalid racer index (%d).", "ai_cfun.cpp", 0x1a2, i);
    if (i < 3) {
        int pos = rinfo[i].position;
        if (dist2(me, player) > thr[pos]) {
            if (pos < g_player_position /*0x4d9e60*/) return 2;   /* racer ahead of player */
            return g_difficulty ? 3 : 1;
        }
        return 0;
    }
    /* cop branch (not used by any table) */
    if (dist2(me, tgt) > 2.0e7f) { cop_siren(me, 0); return 1; }
    return 0;
}
```

#### 0x401cf0 cfun_racer_near_player (racer 11->0 exp 1)
`return dist2(me, player) < 1.5e7f /*0x47f0ac*/ ? 1 : 0;`

#### 0x401d70 cfun_target_not_forward (cop 4->14, 16->14 exp 1)
`return ai_object_drives_forward(tgt) /*sub_410a10*/ == 0;`

#### 0x401d90 cfun_target_forward_near (cop 14->3 exp 1)
```c
int cfun_target_forward_near(int i)
{
    if (!ai_object_drives_forward(tgt)) return 0;
    if (abs(rceng_chunk_distance(OBJ(me)->chunk, OBJ(tgt)->chunk) /*sub_442700*/) >= 2) return 0;
    return OBJ(tgt)->speed > 0.0f;
}
```

#### 0x401e20 cfun_give_up (cop 3->15 exp 1)
```c
int cfun_give_up(int i)
{
    float limit = g_difficulty ? 60.0f : 30.0f;
    g_fsm[i].timer += dt();
    if (g_fsm[i].timer > limit) {
        g_fsm[i].timer = 0;
        cop_siren(me, 0);
        cop_speech_give_up(tgt);                 /* sub_415170: 5 samples, player always, others 1/3 */
        return 1;
    }
    if (rceng_chunk_distance(OBJ(me)->chunk, OBJ(tgt)->chunk) > 50) {   /* signed compare */
        cop_speech_give_up(tgt);                 /* NOTE: siren stays on, timer not reset */
        return 1;
    }
    return 0;
}
```
Note: the timer +0x10 accumulates during all of state 3 (shared with nothing else in state 3).

#### 0x401ef0 cfun_stopped (cop 17->8, 15->8 exp 1)
```c
int cfun_stopped(int i)
{
    if (OBJ(me)->speed > 5.0f) return 0;
    OBJ(me)->speed = 0; OBJ(me)->+0x6c = 0; OBJ(me)->+0x7c = 0;
    return 1;
}
```

#### 0x401f60 cfun_target_other_side (cop 3->16, 4->16 exp 1)
`return ai_get_drive_direction(tgt, OBJ(tgt)->waypoint /*+0x20*/) == -1 && ai_object_drives_forward(tgt);`

#### 0x401fb0 cfun_target_reversing (cop 3/4/16 -> 14 exp 1)
`return OBJ(tgt)->speed < 0.0f;`

#### 0x401ff0 cfun_cop_stuck (cop 16->10 exp 1)
```c
int cfun_cop_stuck(int i)
{
    if (g_fsm[i].time_in_chunk > 2.0f)
        return OBJ(tgt)->speed > 10.0f;
    g_fsm[i].time_in_chunk = 0;      /* resets every frame while <= 2 s, so effectively never fires
                                        (409540 adds only one dt per frame) - keep for 1:1 */
    return 0;
}
```

#### 0x402050 cfun_stuck_3s (racer 0->10, cop 3->10 exp 1)
`return g_fsm[i].time_in_chunk > 3.0f;`

#### 0x402080 cfun_target_back_on_side (cop 16->3 exp 1)
```c
int cfun_target_back_on_side(int i)
{
    int wp = OBJ(tgt)->waypoint;
    if (ai_object_drives_forward(tgt) && ai_get_drive_direction(tgt, wp) == 1) return 1;
    return 0;
}
```

#### 0x434140 return_true
`int return_true(int) { return 1; }` (shared stub elsewhere in the exe)

#### Helpers used by the conds (other parts document fully)
- sub_410a10 `ai_object_drives_forward(obj)`: wp = rceng_get_waypoint_index(obj.chunk, 0, g_direction?0:1); wp2 = sub_4075f0(wp, 0); d = WP(wp2).pos - WP(wp).pos; return (d.x*fwd.x + d.z*fwd.z) >= 0 (fwd = o3d +0x30/+0x38). Returns 0 if `< 0.0f`.
- sub_442700 rceng_chunk_distance(a, b) (shortest wrapped). sub_407660 `ai_chunk_ahead(a, b)`: N=[0x4e5d30]; `if (a>b && a-b < N/2) return 1; if (a<b && b-a > N/2) return 1; return 0;` (N/2 = signed div).

---

### sub_40a100 - drive direction relative to waypoint links (0x40a100)
```c
int ai_get_drive_direction(int obj, int wp)
{
    int reverse = 0, link;
    if (wp < 0) return 1;
    char *w = WP(wp);
    if      ((link = w[+0x14]) >= 0) ;
    else if ((link = w[+0x1c]) >= 0) ;
    else if ((link = w[+0x18]) >= 0) ;
    else { link = w[+0x20]; if (link < 0) return 1; reverse = 1; }
    float p[3]; waypoint_get_pos(link, p);        /* sub_40c350 */
    int a = ai_get_angle_to_point(obj, p);
    if (reverse) { a -= 0x800; if (a < 0) a += 0x1000; }
    if (a >= 0x400 && a <= 0xc00) return -1;      /* link lies behind: driving against the links */
    return 1;
}
```
sub_40c350 `waypoint_get_pos(wp, float out[3])`: copies WP(wp)+0x08..+0x10, returns 1.

### sub_409c10 - slower car ahead on the waypoint (0x409c10)
```c
int ai_slower_car_ahead(int wp, int obj)
{
    int n = waypoint_get_obstacles(wp, g_obst_list /*0x4a5bd8, int[], -1 terminated*/);  /* sub_40c3a0 */
    for (k = 0; k < n; k++) {
        int o = g_obst_list[k];
        if (o == obj) continue;
        float p[3]; p[0] = O3D(o)->x; p[2] = O3D(o)->z;
        int a = ai_get_angle_to_point(obj, p);
        if (a >= 0x400 && a <= 0xc00) continue;          /* behind */
        if (OBJ(obj)->speed > OBJ(o)->speed) return 1;
    }
    return 0;
}
```

---

### Triggers, chunk progress

#### sub_408080 - BACKTRACK_SPEED_TRIGGER (0x408080)
Searches backwards (in `g_direction` sense) from the object's chunk for the nearest speed trigger and lane trigger.
(debug strings: 'BACKTRACK_SPEED_TRIGGER', 'Speed trigger found: %d.', '*p_lane_trigger = %d.', ...)
```c
void ai_backtrack_triggers(int obj, int *p_speed_trigger, int *p_lane_trigger)
{
    *p_speed_trigger = 0; *p_lane_trigger = 0;      /* NO_TRIGGER = 0 */
    int chunk = OBJ(obj)->chunk;
    int step = g_direction /*0x4d9ac4*/ ? 1 : -1;
    while (chunk <= g_num_chunks /*0x4e5d30*/ - 1 && chunk >= 0) {
        for (t = 0; t < 2; t++) {
            int trig = CHUNK(chunk)->trigger[t];          /* +0xec, +0xf0 */
            switch (trig) {
            case 4: case 5: case 6: case 8:               /* speed triggers */
                if (*p_speed_trigger == 0) {
                    *p_speed_trigger = trig;
                    if (*p_lane_trigger != 0) return;     /* 'All triggers found' */
                }
                break;
            case 18: case 19: case 20: case 21:           /* lane triggers, side 1 */
                if (OBJ(obj)->side /*+0x28*/ == 1 && *p_lane_trigger == 0) {
                    *p_lane_trigger = trig;
                    if (*p_speed_trigger != 0) return;
                }
                break;
            case 40: case 41: case 42: case 43:           /* lane triggers, side 0 */
                if (OBJ(obj)->side == 0 && *p_lane_trigger == 0) {
                    *p_lane_trigger = trig;
                    if (*p_speed_trigger != 0) return;
                }
                break;
            default: break;                                /* 7, 9..17, 22..39, others */
            }
        }
        chunk += step;
    }
    /* 'Illegal chunk, returning defaults...' */
    if (*p_speed_trigger == 0) *p_speed_trigger = 8;
    if (*p_lane_trigger == 0) {
        g_no_lane_trigger_count /*0x4a5bd0*/++;            /* '(%04d) No lane trigger found.' */
        if (OBJ(obj)->side == 1) *p_lane_trigger = 19;
        if (OBJ(obj)->side == 0) *p_lane_trigger = 41;
    }
}
```

#### sub_4088f0 - handle trigger for an FSM (0x4088f0)
Speed triggers set the speed limit; 14 = finish line, 15 = halfway/lap checkpoint.
```c
void ai_fsm_handle_trigger(int i, int trigger)
{
    t_fsm *f = &g_fsm[i];
    switch (trigger) {
    case 4: f->speed_limit = 12.5f; break;   /* 0x41480000 */
    case 5: f->speed_limit = 17.5f; break;   /* 0x418c0000 */
    case 6: f->speed_limit = 25.0f; break;   /* 0x41c80000 */
    case 8: f->speed_limit = 30.0f; break;   /* 0x41f00000 */
    case 14:
        if (g_maps[g_track].is_city == 0) {          /* track without laps: finish at once */
            rinfo[i].finished = 1;                    /* 0x485b30 + i*0x58 */
            racer_finished(f->object);                /* sub_411650 */
            f->state = 9;
        } else if (f->lap_trigger == 15) {
            f->laps++;
            f->lap_trigger = 14;
            if (f->laps >= g_nr_laps /*0x4d9ad8*/) {
                rinfo[i].finished = 1;
                racer_finished(f->object);
                f->state = 9;
            }
        }
        break;
    case 15: f->lap_trigger = 15; break;
    default: break;                              /* 7, 9..13, <4, >15 */
    }
}
```
(Only called for i < 3 from 408ed0 and 4093b0, so `rinfo[i]` is valid.)

#### sub_409540 - chunk bookkeeping (0x409540)
Called after every state handler. Tracks time spent in the current chunk and, for racers, the chunk progress used for ranking.
```c
void ai_fsm_update_chunk(int i)
{
    t_fsm *f = &g_fsm[i];
    int c = OBJ(f->object)->chunk;
    if (c == f->chunk) { f->time_in_chunk += dt(); return; }
    f->prev_chunk = f->chunk;
    f->chunk = c;
    f->time_in_chunk = 0;
    if (i >= 3) return;
    int *progress = &t_ai[ OBJ(rinfo[i].object)->ai_index /*+0x0c*/ ].progress;   /* t_ai +0x60 */
    if (ai_chunk_ahead(f->chunk, f->prev_chunk) == 1)                              /* sub_407660(cur, prev) */
        *progress += rceng_chunk_distance(f->prev_chunk, f->chunk);                /* sub_442700(prev, cur) */
    else
        *progress -= rceng_chunk_distance(f->prev_chunk, f->chunk);
}
```

---

### Per-frame driver and dispatcher

#### sub_408a50 - HandleAIObject (0x408a50)
```c
void ai_fsm_handle_object(int i)
{
    switch (g_fsm[i].state) {
    case 0: case 1: ai_state_race(i);            break; /* 0x409d10 */
    case 2:  ai_state_block_player(i);           break; /* 0x409e00 */
    case 3:  ai_state_cop_chase(i);              break; /* 0x409df0 -> 0x409d10 */
    case 4:  ai_state_cop_target_behind(i);      break; /* 0x409f60 */
    case 5:  ai_state_cop_arrest(i);             break; /* 0x40a390 */
    case 6:  ai_state_cop_arrested_wait(i);      break; /* 0x40a480 */
    case 7:  ai_state_racer_arrested(i);         break; /* 0x40a610 */
    case 8:  ai_state_cop_parked(i);             break; /* 0x40a4f0 */
    case 9:  ai_state_finished(i);               break; /* 0x409ed0 */
    case 10: ai_state_stuck_reverse(i);          break; /* 0x40a190 */
    case 11: ai_state_racer_wait(i);             break; /* 0x40a290 */
    case 14: ai_state_cop_target_wrong_way(i);   break; /* 0x40aab0 */
    case 15: ai_state_cop_give_up(i);            break; /* 0x40a9d0 */
    case 16: ai_state_cop_target_other_side(i);  break; /* 0x40a6a0 */
    case 17: ai_state_cop_depart(i);             break; /* 0x40a8a0 */
    default: /* 12, 13, <0, >17 (unsigned compare) */
        fatal("Illegal state. Function: HandleAIObject");
    }
    ai_fsm_update_chunk(i);                             /* 0x409540, always */
}
```

#### sub_408ed0 - per-frame FSM driver (0x408ed0)
```c
void ai_fsm_handle_all(void)
{
    if (!g_4d9a9c || !g_4d9a78) return;              /* game running flags */
    for (i = 0; i < 15; i++) {
        t_fsm *f = &g_fsm[i];
        if (f->object == -1) continue;
        ai_fsm_handle_object(i);
        if (i < 3) {
            int c = OBJ(f->object)->chunk;
            for (t = 0; t < 2; t++) ai_fsm_handle_trigger(i, CHUNK(c)->trigger[t]);   /* +0xec,+0xf0 */
            ai_fsm_handle_trigger(i, WP(OBJ(f->object)->waypoint /*+0x20*/)->trigger /*+4*/);
        } else {
            if (g_game_mode == 3 || g_game_mode == 6) cop_siren(f->object, 0);
            int c = OBJ(f->object)->chunk;
            for (t = 0; t < 2; t++) {
                int trig = CHUNK(c)->trigger[t];
                if (trig >= 14 && trig <= 15 && g_maps[g_track].is_city == 0) f->state = 9;
            }
            int trig = WP(OBJ(f->object)->waypoint)->trigger;
            if (trig >= 14 && trig <= 15 && g_maps[g_track].is_city == 0) f->state = 9;
        }
    }
    if (!g_4d9e54) g_player_position /*0x4d9e60*/ = ai_get_player_position();   /* 0x408c00 */
    ai_rank_racers();                                                            /* 0x408cb0 */
}
```
Note the object fields are re-read after the handler (the handler may change chunk/waypoint? no - it reads OBJ again, same frame values).

---

### Race position ranking

#### sub_408780 - is point behind object (0x408780)
```c
int ai_is_point_behind(int obj, float *pos /* [0]=x, [2]=z */)
{
    int w = OBJ(obj)->waypoint;                      /* +0x20 */
    char *wp = WP(w);
    int link;
    if      ((link = wp[+0x14]) >= 0) ;
    else if ((link = wp[+0x1c]) >= 0) ;
    else if ((link = wp[+0x18]) >= 0) ;
    else if ((link = wp[+0x20]) >= 0) ;
    else {                                           /* no links: use heading angle */
        int a = ai_get_angle_to_point(obj, pos);
        return (a > 0x400 && a < 0xc00);
    }
    float A[3], B[3];
    waypoint_get_pos(link, A);                       /* only A.x, A.z kept */
    waypoint_get_pos(w, B);
    float dx = A[0] - B[0], dz = A[2] - B[2];        /* track direction (stored as floats) */
    float px = pos[0] - O3D(obj)->x;                 /* stored float */
    long double pz = pos[2] - O3D(obj)->z;
    long double ld = sqrt(dx*dx + dz*dz);            /* dx,dz products use the stored floats */
    long double lp = sqrt(px*px + pz*pz);
    float sum = (float)ld + (float)lp;               /* NB: original adds the lengths (fadd), not multiplies */
    long double r = (px*dx + pz*dz) / sum;
    return r < 0.0f;
}
```
Precision detail: `ld` and `lp` are each spilled to a float temp (fst/fstp [esp+0x24]) and `sum` is spilled to float before the divide; `px*dx` uses stored floats dx, px; `pz*dz` uses 80-bit pz times stored dz. Only the sign matters in practice.

#### sub_408c00 - player race position (0x408c00)
Number of racers ahead of the player (0 = player leads). Result stored in g_player_position 0x4d9e60.
```c
int ai_get_player_position(void)
{
    int n = 0;
    for (i = 0; i < 3; i++) {
        int prog = t_ai[OBJ(rinfo[i].object)->ai_index].progress;   /* +0x60 */
        if (g_player_progress /*0x4d9e70*/ < prog) { n++; continue; }
        if (g_player_progress != prog) continue;
        float p[3]; p[0] = O3D(g_fsm[i].object)->x; p[2] = O3D(g_fsm[i].object)->z;
        if (!ai_is_point_behind(g_player_object, p)) n++;          /* racer not behind player */
    }
    return n;
}
```

#### sub_408cb0 - rank racers (0x408cb0)
```c
void ai_rank_racers(void)
{
    for (i = 0; i < 3; i++) {
        int pos = 0;
        int prog_i = t_ai[OBJ(rinfo[i].object)->ai_index].progress;
        if (g_player_progress > prog_i) pos = 1;
        else if (g_player_progress == prog_i) {
            float p[3] = player o3d x,_,z;
            if (!ai_is_point_behind(g_fsm[i].object, p)) pos = 1;   /* player ahead of racer i */
        }
        for (j = 0; j < 3; j++) {
            if (j == i) continue;
            int prog_j = t_ai[OBJ(rinfo[j].object)->ai_index].progress;
            if (prog_j > prog_i) pos++;
            else if (prog_j == prog_i) {
                float p[3]; p[0] = O3D(g_fsm[j].object)->x; p[2] = O3D(g_fsm[j].object)->z;
                if (!ai_is_point_behind(g_fsm[i].object, p)) pos++;
            }
        }
        rinfo[i].position = pos;                                  /* +0x08 */
    }
    /* resolve ties (positions 0..3 incl. the player) */
    int ppos = g_player_position;
    for (p = 0; p < 4; p++) {
        int cnt = 0;
        for (k = 0; k < 3; k++) if (rinfo[k].position == p) cnt++;
        if (p == ppos) cnt++;
        cnt = (cnt <= 1) ? 0 : cnt - 1;
        /* bump the first `cnt` racers at position p to p+1, p+2, ...; NOTE: no bound on k
           (original walks the 0x58-stride array until cnt matches are found) */
        for (n = 0, k = 0; n < cnt; k++)
            if (rinfo[k].position == p) { rinfo[k].position = p + n + 1; n++; }
    }
}
```

---

### Init

#### sub_4093b0 - FSM init for racers and cops (0x4093b0), called from sub_410b60
```c
void ai_fsm_init(void)
{
    for (i = 0; i < 3; i++) {
        ai_ctl_clear(&g_fsm[i].car_model /*+0x30*/);       /* sub_401200 */
        ai_fsm_ctor(&g_fsm[i], 0);                         /* sub_403320: state 0 */
        ai_fsm_reset_chunk(&g_fsm[i].twp /*+0x1c*/, i);    /* sub_409500 */
        t_ai[i].progress /*+0x60*/ = 0;                    /* NB indexed by fsm index i (t_ai stride 0x6c) */
        int speed_trig, lane_trig;
        ai_backtrack_triggers(g_fsm[i].object, &speed_trig, &lane_trig);   /* sub_408080 */
        ai_fsm_handle_trigger(i, speed_trig);              /* initial speed limit */
    }
    for (i = 3; i < 15; i++) {
        g_fsm[i].object = -1;
        ai_ctl_clear(&g_fsm[i].car_model);
        ai_fsm_reset_chunk(&g_fsm[i].twp, i);   /* reads OBJ(-1)->chunk: out-of-bounds read, value irrelevant */
    }
    if (!g_demo /*0x4d9b20*/) {
        ai_fsm_set_racer_transition(0, 0, 11, 2, cfun_racer_too_far);      /* 0x401b60 */
        ai_fsm_set_racer_transition(1, 11, 0, 1, cfun_racer_near_player);  /* 0x401cf0 */
    }
    ai_fsm_set_racer_transition(2, 7, 0, 1, cfun_wait_4s);                 /* 0x401b00 */
    ai_fsm_set_racer_transition(3, 0, 10, 1, cfun_stuck_3s);               /* 0x402050 */
    ai_fsm_set_racer_transition(4, 0, 1, 1, cfun_player_behind_leader);    /* 0x401780 */
    ai_fsm_set_racer_transition(5, 1, 2, 1, cfun_random_block);            /* 0x4018b0 */
    ai_fsm_set_racer_transition(6, 2, 0, 0, cfun_player_behind_leader);    /* 0x401780 */
    ai_fsm_init_cop_transitions();                                         /* 0x409200 */
    ai_difficulty_init();                                                  /* 0x4108d0 */
    ai_world_mapper_init();                                                /* 0x40be40 */
}
```

#### sub_409200 - cop transitions (0x409200)
```c
void ai_fsm_init_cop_transitions(void)
{
    S(0,  3, 4, 1, 0x401960);  S(1,  4, 3, 0, 0x401960);  S(2,  4, 5, 1, 0x4019f0);  S(3,  3, 5, 1, 0x4019f0);
    S(4,  5, 6, 1, 0x434140);  S(5,  6,17, 1, 0x401b00);  S(6, 17, 8, 1, 0x401ef0);  S(7,  3,16, 1, 0x401f60);
    S(8,  4,16, 1, 0x401f60);  S(9, 16, 3, 1, 0x402080);  S(10, 4,14, 1, 0x401d70);  S(11,16,14, 1, 0x401d70);
    S(13,14, 3, 1, 0x401d90);  S(14,16,10, 1, 0x401ff0);  S(15, 3,10, 1, 0x402050);  S(16, 3,15, 1, 0x401e20);
    S(17,15, 8, 1, 0x401ef0);  S(18, 3,14, 1, 0x401fb0);  S(19, 4,14, 1, 0x401fb0);  S(20,16,14, 1, 0x401fb0);
}   /* S = ai_fsm_set_cop_transition (0x403460)(idx, from, to, expected, cond); idx 12 never set */
```

#### sub_409060 - cop checks for speeding cars (0x409060), called from HANDLE_COP_AI sub_4026c0
```c
void ai_cop_check_speeding(int cop_obj)
{
    int chunk = OBJ(cop_obj)->chunk;
    for (k = 3; k < 15; k++) if (g_fsm[k].object == cop_obj) return;   /* already chasing */
    for (i = 0; i < 3; i++) {
        int r = g_fsm[i].object;
        if (OBJ(r)->chunk == chunk && g_fsm[i].speed_limit <= OBJ(r)->speed) {
            ai_cop_start_chase(cop_obj, g_fsm[i].object);  /* 0x4034a0 */
            return;
        }
    }
    if (chunk != g_player_chunk /*0x4d9e44*/) return;
    int speed_trig, lane_trig;
    ai_backtrack_triggers(cop_obj, &speed_trig, &lane_trig);   /* 0x408080 */
    float v = OBJ(g_player_object)->speed;
    switch (speed_trig) {
    case 4: if (v > 12.5f) ai_cop_start_chase(cop_obj, g_player_object); break;
    case 5: if (v > 17.5f) ai_cop_start_chase(cop_obj, g_player_object); break;
    case 6: if (v > 25.0f) ai_cop_start_chase(cop_obj, g_player_object); break;
    case 8: if (v > 30.0f) ai_cop_start_chase(cop_obj, g_player_object); break;
    default: break;   /* 0, 7 */
    }
}
```
Note asymmetry: racers are caught at `speed >= limit`, the player at `speed > threshold`; the player's threshold comes
from the backtracked trigger at the cop's chunk, racers use their own fsm speed_limit.

---

### State handlers (summary only; full transcription in section 7)
- 0x409d10 (0,1,3 via 0x409df0): +0x40=0; mapper = sub_40be70(obj,1,-1,5,3); wp=sub_40bf70(mapper); twp=sub_409670(i,mapper,5,wp,..); if twp>-1 { if !ai_slower_car_ahead(wp,obj) sub_4012c0(i,..) } else twp=old; sub_4013c0(i,..); sub_40bf20(mapper).
- 0x409e00 (2): same with mapper on the target (sub_40be70(target,1,twp,5,3)); on failure +0x38=+0x3c=0; timer +0x10 += dt.
- 0x409f60 (4): mapper on own object with own waypoint; if target slower: +0x3c=50,+0x38=0; else sub_4012c0 and if target angle in (1800,2200): +0x3c=100.
- 0x40a390 (5): timer=0; siren off; player target: if g_game_mode==0 sub_424d20(8) (caught sequence); racer target: its fsm state=7 ('An illegal racer was arrested in %s %d' if not found); all cops (3..14) with this target -> state 6.
- 0x40a480 (6) / 0x40a610 (7): speed,+0x6c,+0x7c = 0 (7 also +0x38=+0x3c=0); physics calls 40ee80/40ece0/40ebd0.
- 0x40a4f0 (8): if chunk_distance(obj.chunk, player.chunk) > 30: OBJ+0x34=0, ai_fsm_release_cop(i); else v,+0x6c,+0x7c -= 3*x*dt.
- 0x409ed0 (9): +0x40=1; +0x34 += 75*dt, cap 100; +0x38=+0x3c=0; sub_410280.
- 0x40a190 (10): if time_in_chunk >= 2: +0x34=0, speed=-15, time_in_chunk=0, +0x18=0; else +0x18 += dt, if +0x18 > 1: state = i<3 ? 0 : 3, +0x18=0.
- 0x40aab0 (14) / 0x40a6a0 (16): follow path; if cop ahead of target (sub_407660) and faster: +0x3c += 400*dt cap 100, +0x38=0; stop if speed<0; 14 always zeroes time_in_chunk, 16 only when target speed <= 0.
- 0x40a9d0 (15): like 0x409d10 with +0x40=1.
- 0x40a8a0 (17): OBJ+0x38=0; if target angle in (1024,3072): race-drive (409d10), speed=10, timer+=dt, timer>3 -> speed=0.

### Globals
| addr | type | name | meaning |
|---|---|---|---|
| 0x4a55a0 | t_fsm[15] | g_fsm | FSMs: 0..2 racers, 3..14 cops |
| 0x4a5410 | t_fsm_transition[25] | g_racer_transitions | |
| 0x4a5280 | t_fsm_transition[25] | g_cop_transitions | |
| 0x485b20 | t_racer_info[3] (0x58) | g_racer_info | +0x08 position, +0x0c object, +0x10 finished |
| 0x4a5bd8 | int[] | g_obstacle_list | -1 terminated objects on a waypoint (sub_40c3a0) |
| 0x4a5bd0 | int | g_no_lane_trigger_count | debug counter in 408080 |
| 0x4a5c08 | 8 x 0x18 | g_world_mappers | sub_40be40 init, sub_40bf70 get waypoint |
| 0x4a5ce0 | int[20] | g_cop_list | cop objects; entry := -1 on release |
| 0x4d9e60 | int | g_player_position | 0 = player leads (racers ahead of player) |
| 0x4d9e70 | int | g_player_progress | player's chunk progress (cf. t_ai+0x60) |
| 0x4d9e44 | int | g_player_chunk | |
| 0x4d9e28 | int | g_player_object | |
| 0x4d9e2c | int | g_player_car_model | |
| 0x4d9e54 | int | g_4d9e54 | when set, player position no longer updated (set at end of cop "caught" sequence in ev_cops sub_4151f0; race over) |
| 0x4d9ab0 | int | g_difficulty | Game.ini [OPTIONS] difficulty |
| 0x4d9ad8 | int | g_nr_laps | [OPTIONS] NRLAPS (forced 1 if !is_city) |
| 0x4d9ac4 | int | g_direction | etappe DIRECTION |
| 0x4d9b20 | int | g_demo | [OPTIONS] demo; disables racer rubber band transitions |
| 0x4d9a7c | int | g_game_mode | 3/6 = no cop siren / no player arrest (3) |
| 0x4d9a9c, 0x4d9a78 | int | game running flags | |
| 0x4d8e9c | int (stride 0x89) | g_maps[].is_city | 0 => single-lap track: finish trigger 14 ends race, cops -> 9 |
| 0x4e5d30 | int | g_num_chunks | used by 408080, 407660 |
| 0x4a5d30/3c/40 | int | siren light state/toggle/tick | sub_40c860 |

### Open questions
- Exact semantics of fsm +0x34/+0x38/+0x3c/+0x40/+0x44..+0x58 (driving control block) - consumer sub_410280/sub_40ff90/sub_4012c0/sub_4013c0 (see section 7: steer, throttle, brake, coast flag, gear, skid state, stopped latch).
- sub_428bc0 is called every frame for every active FSM with sample 1 (0 for a cop in state 9); check whether this is really "play sample" or "set engine sound on/off".
- cfun_cop_stuck (0x401ff0) can practically never fire (resets time_in_chunk each frame while <= 2 s); faithful port keeps it.
- In 408cb0's tie fix the inner loop has no array bound; with consistent data it terminates within 3 entries.
- sub_408780 uses `dot / (|d| + |p|)` (sum of lengths) - only the sign is used, so harmless.
- 4093b0 zeroes `t_ai[i].progress` for i = fsm index (assumes racer i has ai index i), while 409540/408c00 index t_ai through the object's ai index.


## 7. Path, world mapper, steering, FSM state handlers, old racer AI (ai_path/ai_wmap/ai_racer.cpp)


Conventions used below (same as sec_fsm.md):

- `OBJ(o)` = `g_objects /*[0x4e48e0]*/ + o*0xa0`; `O3D(o)` = `g_object3d /*[0x4ea9e0]*/ + OBJ(o)->o3d*0x99` (stride 0x99!).
- `WP(w)` = `g_driver_map /*[0x4e6628]*/ + w*0x50`; `CHUNK(c)` = `g_chunks /*[0x4e65e0]*/ + c*0x184`.
- `E` = `&g_fsm[fsm]` (0x4a55a0 + fsm*0x60). `AI(a)` = `t_ai.data /*[0x4a5b40]*/ + a*0x6c`. `BH(b)` = `[0x4a5b88] + b*0x2c`.
- `dt()` = `sub_441cd0(this=0x4e4970)` clamped frame time. `fatal()` = sub_464e50. Debug prints (sub_464ca0) omitted.
- Floats: decimal, hex given when not exact. `(a > b)` on floats = x87 `fcom`; NaN never occurs.
- IMPORTANT for porting: several functions use a raw chunk *number* as chunk *record index* (no
  `g_road_chunk_order[0x4e5d2c]` lookup). The road code fills that map with the identity, so this is harmless; kept literal.

---------------------------------------------------------------------------------------------------

### 1. World mapper (ai_wmap.cpp)

8 slots `t_world_mapper g_world_mappers[8]` at **0x4a5c08**, stride 0x18 (see Structures). A handler allocates a
slot at the start of each frame (`ai_wmap_add`), scans with it, and frees it at the end (`ai_wmap_remove`).
The two direction fields +0x10/+0x14 are only ever written as 1 (add) or 0 (init/remove) -> at run time only the
"forward, normal side" branch (case A) of SIDESWITCH executes; cases B/C/D are dead code but transcribed.

#### ai_wmap_init (0x40be40)
`int ai_wmap_init(void)` - called from ai_fsm_init 0x4093b0.
```c
for (i = 0; i < 8; i++) {
    wm[i].object = -1; wm[i].chunk = -1; wm[i].waypoint = -1;   /* +0x00 +0x04 +0x08 */
    wm[i].dir_fwd = 0; wm[i].dir_side = 0;                       /* +0x10 +0x14 */
}                                                                /* +0x0c untouched */
return 1;
```

#### ai_wmap_add (0x40be70)
`int ai_wmap_add(int object, ...)` - callers push 5 args `(object, 1, -1 | twp, 5, 3)`; only `object` is read
(cdecl, extra args ignored). Returns slot index, -2 if the object already has a slot, -1 if all 8 are used.
```c
for (i = 0; i < 8 && wm[i].object != object; i++) ;
if (i == 8) i = -1;
if (i >= 0) return -2;
for (i = 0; i < 8 && wm[i].object >= 0; i++) ;          /* first free (object < 0) */
slot = (i == 8) ? -1 : i;
if (slot >= 0) {
    wm[slot].object   = object;
    wm[slot].chunk    = OBJ(object)->chunk;              /* obj +0x18 */
    wm[slot].dir_fwd  = 1;                                /* +0x10 */
    wm[slot].dir_side = 1;                                /* +0x14 */
    wm[slot].waypoint = drmap_find_closest_waypoint(wm[slot].object, 1);   /* sub_443da0(obj, side 1) */
}
return slot;
```

#### ai_wmap_remove (0x40bf20)
```c
int ai_wmap_remove(int h) {
    if (h >= 8 || h < 0) return 0;
    wm[h].object = -1; wm[h].waypoint = -1; wm[h].dir_fwd = 0;
    wm[h].chunk = -1;  wm[h].field_0c = -1; wm[h].dir_side = 0;
    return 1;
}
```

#### ai_wmap_get_waypoint (0x40bf70)
```c
int ai_wmap_get_waypoint(int h) {
    if (h >= 8 && h < 0) return -1;      /* sic: condition can never be true */
    return wm[h].waypoint;               /* +0x08 */
}
```

#### ai_wmap_closest_waypoint (0x40b970)
`int ai_wmap_closest_waypoint(int wp, int side, int chunk, int range)` (ai_wmap.cpp). Only caller: SIDESWITCH
with `range = 10`. Brute-force nearest lane waypoint (3D distance to WP(wp).pos) on the given side(s).
BUG kept: the loop walks `c = chunk-range .. chunk+range-1` and calls rceng_wrap_chunk(c) but ignores the
result, so it scans the *same* chunk `chunk` 2*range times (result is still the nearest lane waypoint of `chunk`).
```c
int   best  = -1;
float bestd = 3.4e38f;                       /* 0x7f7fc99e */
if (side != 1 && side != 0 && side != -1)
    fatal("%s(%d): Illegal side %d.\n", "C:\\work\\A2racII\\Game\\ai_wmap.cpp", 126, side);
vec3 P = WP(wp)->pos;                        /* +0x08,+0x0c,+0x10 */
if (side == 1 || side == -1) {               /* RIGHT */
    rec = CHUNK(chunk);                      /* raw chunk number as record index */
    if (-range < range)
        for (c = chunk - range, n = 2*range; n != 0; c++, n--) {
            rceng_wrap_chunk(c);             /* sub_4426e0, result discarded */
            for (i = 0; i < rec->lanes_right /*+0xe8*/; i++) {
                w = rec->wp_right[i];        /* +0x154 */
                d = sqrtf(sq(WP(w)->pos.x-P.x) + sq(WP(w)->pos.y-P.y) + sq(WP(w)->pos.z-P.z));
                if (d < bestd) { best = w; bestd = d; }
            }
        }
}
if (side == 0 || side == -1) {               /* LEFT: identical with +0xe4 / +0x124 */
    ... same loop over rec->lanes_left (+0xe4), rec->wp_left[i] (+0x124) ...
}
return best;
```

#### ai_wmap_sideswitch (0x40bf90) - "SIDESWITCH"
`int ai_wmap_sideswitch(int h, int wp, int dir)` -> next waypoint following link `dir` (0..3) from `wp`, taking
the world mapper's travel direction into account. Jump table at 0x40c308 (16 entries = 4 cases x 4 dirs).
Waypoint flag bit `WP(w)->flags (+0x00) & 8` marks a carriageway switch point.
```c
m = &wm[h];
if (m->dir_side /*+0x14*/ == 1 && m->dir_fwd /*+0x10*/ == 1) {          /* case A: the only live case */
    switch (dir) {
    case 0: return WP(wp)->link18;      /* +0x18 */
    case 1: return WP(wp)->link14;      /* +0x14 */
    case 2: return WP(wp)->link1c;      /* +0x1c */
    case 3: return WP(wp)->link20;      /* +0x20 */
    default: return -1;
    }
}
if (m->dir_side == -1 && m->dir_fwd == -1) {                            /* case B */
    c = rceng_wrap_chunk(m->chunk + 1);                                  /* used raw as record index */
    switch (dir) {
    case 0: for (i = 0; i < CHUNK(c)->lanes_left; i++)
                if (WP(CHUNK(c)->wp_left[i])->link18 == wp) return wp;      /* sic: returns wp itself */
            return -1;
    case 1: return WP(wp)->link20;
    case 2: if (WP(wp)->flags & 8) {
                /* '%d: SIDESWITCH FROM LEFT SIDE BACKWARD TO RIGHT SIDE FORWARD' via sub_466420(0,10,50,fmt,0) (on-screen debug) */
                return WP(ai_wmap_closest_waypoint(wp, 1, m->chunk, 10))->link14;
            }
            for (i = 0; i < CHUNK(c)->lanes_left; i++)
                if (WP(CHUNK(c)->wp_left[i])->link1c == wp) return wp;
            return -1;
    case 3: return WP(wp)->link14;
    default: return -1;
    }
}
if (m->dir_side == 1 && m->dir_fwd == -1) {                             /* case C */
    c = rceng_wrap_chunk(m->chunk - 1);
    switch (dir) {
    case 0: for (i = 0; i < CHUNK(c)->lanes_right; i++)
                if (WP(CHUNK(c)->wp_right[i])->link18 == wp) return wp;
            return -1;
    case 1: return WP(wp)->link20;
    case 2: if (WP(wp)->flags & 8) {
                /* '%d: SIDESWITCH FROM RIGHT SIDE BACKWARD TO LEFT SIDE FORWARD' */
                return WP(ai_wmap_closest_waypoint(wp, 0, m->chunk, 10))->link14;
            }
            for (i = 0; i < CHUNK(c)->lanes_right; i++)
                if (WP(CHUNK(c)->wp_right[i])->link1c == wp) return wp;
            return -1;
    case 3: return WP(wp)->link14;
    default: return -1;
    }
}
if (m->dir_side == -1 && m->dir_fwd == 1) {                             /* case D */
    switch (dir) {
    case 0: if (WP(wp)->flags & 8) {
                /* '%d: SIDESWITCH FROM RIGHT SIDE FORWARD TO LEFT SIDE BACKWARD' */
                return WP(ai_wmap_closest_waypoint(wp, 1, m->chunk, 10))->link20;
            }
            return WP(wp)->link18;
    case 1: return WP(wp)->link14;
    case 2: return WP(wp)->link1c;
    case 3: return WP(wp)->link20;
    default: return -1;
    }
}
return -1;
```

#### waypoint helpers (ai_wmap.cpp)
```c
/* 0x40c350 */ int waypoint_get_pos(int wp, float out[3]) { out = WP(wp)->pos (+0x08,+0x0c,+0x10); return 1; }
/* 0x40c3a0 */ int waypoint_get_obstacles(int wp, int *out) {
    for (i = 0; i < WP(wp)->n_obstacles /*+0x24*/; i++) out[i] = waypoint_get_object(this=WP(wp), i); /* sub_444260 */
    out[i] = -1;
    return WP(wp)->n_obstacles;
}
/* 0x40c400 */ int chunk_get_lane_counts(int chunk, int *left, int *right) {
    *right = CHUNK(chunk)->lanes_right /*+0xe8*/; *left = CHUNK(chunk)->lanes_left /*+0xe4*/; return 1;
}
/* 0x40c440 */ int chunk_find_lane_of_waypoint(int chunk, int wp) {   /* raw chunk number */
    for (i = 0; i < CHUNK(chunk)->lanes_left;  i++) if (CHUNK(chunk)->wp_left[i]  == wp) return i;
    for (i = 0; i < CHUNK(chunk)->lanes_right; i++) if (CHUNK(chunk)->wp_right[i] == wp) return i;
    return -1;                                                     /* note: index restarts at 0 for the right side */
}
```

---------------------------------------------------------------------------------------------------

### 2. ScanPath (0x409670, ai_path.cpp)

`int ScanPath(int fsm, int wm, int depth, int wp, float out_near[3], float out_far[3])`

Depth-first search along waypoint links (via SIDESWITCH) for `depth` steps, skipping waypoints that have
objects on them (obstacle list non-empty). At each level three link directions are tried in a preference order
`o0, o1, o2`; when all three fail the search backtracks one level. Result (all callers use depth 5):
returns `path[1]` (the next waypoint) and writes `WP(path[2]).pos -> out_near`, `WP(path[4]).pos -> out_far`.
If the search backtracks to level 0 with nothing found, a fallback walks two steps ignoring obstacles.

Direction preference: default `o0=1 (link14), o1=2 (link1c), o2=0 (link18)`. Only in FSM state 4
(FSM_COP_TARGET_BEHIND) the cop prefers the lane of its target: compares lane indices
`a = lane of target's current waypoint` and `b = lane of own current waypoint` (chunk_find_lane_of_waypoint):
`a > b -> (0,1,2)`, `a < b -> (2,1,0)`, `a == b -> (1,2,0)`.
(So link14 = straight on in the same lane, link18 / link1c = diagonal to the neighbouring lane, link20 = backwards.)

Local stack array (esp+0x50): 25 packed 7-byte nodes `{ u8 tried[3]; int wp; }`.
```c
struct node { uint8_t tried[3]; int32_t wp; } st[25];     /* packed, 7 bytes */
int  n = 0;
bool advanced_inner = 0;                                   /* [esp+0x13] */
int  my_chunk;                                             /* [esp+0x30], only set when fsm < 15 */

if (depth > 25) return -2;
if (wp < 0) fatal("Illegal waypoint %d. Function ScanPath: %d", wp, 0x112);
for (i = 0; i < depth; i++) { st[i].tried[0] = st[i].tried[1] = st[i].tried[2] = 0; st[i].wp = -1; }
int chunk_rec = g_road_chunk_order[ OBJ(E->object)->chunk ];   /* [0x4e5d2c], used only in the fallback */

if (depth > 0) {
  outer:                                                   /* 0x40972f */
    st[n].wp = wp;
    if (fsm < 15) my_chunk = OBJ(E->object)->chunk;
    if (E->state == 4) {
        int a = chunk_find_lane_of_waypoint(OBJ(E->target)->chunk, OBJ(E->target)->waypoint /*+0x20*/);
        int b = chunk_find_lane_of_waypoint(my_chunk,               OBJ(E->object)->waypoint);
        if      (a > b) { o0 = 0; o1 = 1; o2 = 2; }
        else if (a < b) { o0 = 2; o1 = 1; o2 = 0; }
        else            { o0 = 1; o1 = 2; o2 = 0; }
    } else              { o0 = 1; o1 = 2; o2 = 0; }

    /* try o0 */
    if (st[n].tried[0]) next = -1;
    else { next = ai_wmap_sideswitch(wm, wp, o0); st[n].tried[0] = 1; }
    if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list /*0x4a5bd8*/) != 0) next = -1;
    if (next >= 0) {                                       /* 0x4099a5 */
        wp = next; n++;
        st[n].tried[0] = st[n].tried[2] = st[n].tried[1] = 0;
        goto cond;
    }
    for (;;) {                                             /* 0x409893: inner loop at level n */
        if (st[n].tried[1]) next = -1;
        else { next = ai_wmap_sideswitch(wm, wp, o1); st[n].tried[1] = 1; }
        if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list) != 0) next = -1;
        if (next < 0) {
            if (st[n].tried[2]) next = -1;
            else { next = ai_wmap_sideswitch(wm, wp, o2); st[n].tried[2] = 1; }
            if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list) != 0) next = -1;
        }
        if (next >= 0) {                                   /* 0x409925: advance */
            n++; advanced_inner = 1;
            st[n].tried[0] = st[n].tried[2] = st[n].tried[1] = 0;
            wp = next; st[n].wp = next;
        } else {                                           /* 0x40995b: backtrack */
            if (n == 0) goto fallback;
            wp = st[n-1].wp; n--;
        }
        if (advanced_inner) break;                         /* after backtrack: retry o1/o2 of the parent */
    }
    advanced_inner = 0;
  cond:                                                    /* 0x4099cb */
    if (n < depth) goto outer;
}
/* 0x4099d8 */
if (st[2].wp < 0 || st[4].wp < 0 || st[1].wp < 0)
    fatal("Illegal waypoint in het laatste stukkie van ScanPath");
waypoint_get_pos(st[2].wp, out_near);
waypoint_get_pos(st[4].wp, out_far);
return st[1].wp;

fallback:                                                  /* 0x409a2c: no obstacle-free path at all */
w0 = st[0].wp;
w1 = ai_wmap_sideswitch(wm, w0, o0);
if (w1 < 0) w1 = ai_wmap_sideswitch(wm, w0, o1);
if (w1 < 0) w1 = ai_wmap_sideswitch(wm, w0, o2);
if (w1 < 0) {
    if (w0 > 0) { waypoint_get_pos(w0, out_near); return -1; }
    fatal("Invalid waypoint: %d File: %s Line: %d", w0, "C:\\work\\A2racII\\Game\\ai_path.cpp", 472);
    return -1;
}
float p1[3]; waypoint_get_pos(w1, p1);                     /* [esp+0x44] */
w2 = ai_wmap_sideswitch(wm, w1, o0);
if (w2 < 0) w2 = ai_wmap_sideswitch(wm, w1, o1);
if (w2 < 0) w2 = ai_wmap_sideswitch(wm, w1, o2);
if (w2 <= 0) { waypoint_get_pos(w1, out_near); return -1; }   /* note: <= 0, waypoint 0 rejected */
waypoint_get_pos(w2, out_near);
int left, right;
chunk_get_lane_counts(chunk_rec, &left, &right);           /* writes [esp+0x34]=left, [esp+0x28]=right */
if (right == 1) {
    /* single lane: shift the target point sideways (rotate the step w1->w2 by "1024 rad" and scale) */
    double c = cos(1024.0), s = sin(1024.0);               /* constant 0x47f200 = 1024.0 (double), radians! */
    float dx = (out_near[0] - p1[0]) * 0.46511626f;        /* 0x3eee23b8 = 20/43 */
    float dy = (out_near[1] - p1[1]) * 0.46511626f;        /* sic: component [1] */
    float rx = dx*c - dy*s;
    float ry = dy*c + dx*s;
    out_near[0] += rx;
    out_near[2] += ry;                                     /* sic: added to component [2] */
    out_far[0] = out_near[0];
    out_far[2] = out_near[2];                              /* out_far[1] NOT written */
}
return w2;                                                 /* out_far untouched if right != 1 */
```
Note: when depth reaches `n == depth` the tried bytes of `st[depth]` are cleared (within the 25-node array for depth<=24).

#### ai_slower_car_ahead (0x409c10)
`int ai_slower_car_ahead(int wp, int self)` - 1 if an object (other than self) on waypoint `wp` is in front
(relative angle outside 0x400..0xc00) and self is faster than it.
```c
n = waypoint_get_obstacles(wp, g_obstacle_list);
for (i = 0; i < n; i++) {
    o = g_obstacle_list[i];
    if (o == self) continue;
    float p[3]; p[0] = O3D(o)->x; p[2] = O3D(o)->z;        /* p[1] uninitialised (unused by callee) */
    ang = ai_get_angle_to_point(self, p);                  /* sub_401690, 0..4095, 0 = ahead */
    if (ang >= 0x400 && ang <= 0xc00) continue;            /* behind */
    if (OBJ(self)->speed > OBJ(o)->speed) return 1;        /* +0x74 */
}
return 0;
```

#### ai_get_drive_direction (0x40a100)
`int ai_get_drive_direction(int obj, int wp)` (called from ai_cfun sub_401690's family) - +1 when the object
faces along the waypoint links, -1 when against.
```c
rev = 0;
if (wp < 0) return 1;
if      ((l = WP(wp)->link14) >= 0) ;
else if ((l = WP(wp)->link1c) >= 0) ;
else if ((l = WP(wp)->link18) >= 0) ;
else if ((l = WP(wp)->link20) >= 0) rev = 1;
else return 1;
float p[3]; waypoint_get_pos(l, p);
a = ai_get_angle_to_point(obj, p);
if (rev) { a -= 0x800; if (a < 0) a += 0x1000; }
if (a >= 0x400 && a <= 0xc00) return -1;
return 1;
```

---------------------------------------------------------------------------------------------------

### 3. Steering / speed helpers (ai_cfun area 0x401200-0x401690)

FSM entry driving-control fields: `steer` +0x34 (-100..100), `throttle` +0x38 (0..100), `brake` +0x3c (0..100;
brake * negative accel also drives the car backwards), `coast` +0x40 (flag: no engine force, skid steering),
`gear` +0x44, `skid_t` +0x48, `skid_yaw` +0x4c, `skid_heading` +0x50, +0x54 (cleared, unused here),
`stopped` +0x58.

#### ai_ctl_clear (0x401200)
`void __thiscall ai_ctl_clear(char *this /* = &g_fsm[i] + 0x30 */)`: zeroes this+0x18,+0x1c,+0x20,+0x24,+0x28,
+0x04,+0x08,+0x0c,+0x14,+0x10, i.e. fsm +0x48,+0x4c,+0x50,+0x54,+0x58,+0x34,+0x38,+0x3c,+0x44,+0x40. Returns nothing.

#### ai_set_steering (0x401230)
`void ai_set_steering(int fsm, const float target[3])` - proportional steering only (no physics update).
```c
obj = (fsm < 15) ? E->object : (fatal("Out of FSM array index. Function: CalculateStearing"), fsm /*sic*/);
a = ai_heading_diff(obj, target);                /* sub_40fe90, int -2048..2048 */
if      (a >  200) E->steer =  100.0f;
else if (a < -200) E->steer = -100.0f;
else               E->steer = (float)a * 0.5f;
```

#### ai_speed_control (0x4012c0)
`void ai_speed_control(int fsm, const float target[3])` - target speed from the bend angle to `target`:
`v = 70 - |angle|/15` (|angle| > 1000 treated as 500). Below v: throttle up; above: racers brake, cops are
clamped to v directly.
```c
a = abs(ai_heading_diff(E->object, target));
if (a > 1000) a = 500;
v = 70 - a / 15;                                  /* integer division (magic 0x88888889) */
if ((float)v > OBJ(E->object)->speed) {
    E->throttle += 75.0f * dt();                  /* written as throttle - dt*(-75) */
    if (E->throttle > 100.0f) E->throttle = 100.0f;
    E->brake = 0.0f;
} else if (fsm < 3) {                             /* racers */
    E->brake += 400.0f * dt();
    if (E->brake > 100.0f) E->brake = 100.0f;
} else {                                          /* cops */
    OBJ(E->object)->speed = (float)v;
}
```

#### CalculateStearing (0x4013c0)
`void CalculateStearing(int fsm, const float target[3])` - normalises the heading, runs the drive model, sets
steering as ai_set_steering, then runs steering physics and the three body-spring updates.
```c
obj = E->object;
if (O3D(obj)->yaw /*+0x78*/ < 0.0f)     O3D(obj)->yaw -= -4096.0f;
if (!(O3D(obj)->yaw < 4096.0f))         O3D(obj)->yaw -=  4096.0f;
ai_drive(fsm);                                    /* sub_40ff90 */
o = (fsm < 15) ? E->object : (fatal("Out of FSM array index. Function: CalculateStearing"), fsm);
a = ai_heading_diff(o, target);
E->steer = a > 200 ? 100.0f : a < -200 ? -100.0f : (float)a * 0.5f;
ai_steer_physics(fsm);                            /* sub_410280 */
car_roll_spring(obj);                             /* sub_40ebd0 */
car_pitch_spring(obj);                            /* sub_40ece0 */
car_suspension(obj);                              /* sub_40ee80 */
```

#### ai_rubberband_factor (0x4014e0)
`float ai_rubberband_factor(int fsm)` - multiplier on the AI's gear top speeds (used by car_gear_accel).
For racers (fsm < 3) it depends on how many chunks the racer is ahead of the player
(`d = t_ai.progress(+0x60) - g_player_progress[0x4d9e70]`), difficulty (0x4d9ab0) and track (0x4d9a74).
```c
float A[3] = {0.95f, 0.92f, 0.90f};   /* ahead, difficulty 0  (0x3f733333 0x3f6b851f 0x3f666666) */
float B[3] = {1.02f, 1.01f, 1.00f};   /* ahead, difficulty !0 (0x3f828f5c 0x3f8147ae 0x3f800000) */
float C[3] = {1.00f, 0.98f, 0.95f};   /* behind, difficulty 0 (0x3f800000 0x3f7ae148 0x3f733333) */
float D[3] = {1.15f, 1.10f, 1.05f};   /* behind, difficulty !0 (0x3f933333 0x3f8ccccd 0x3f866666) */
if (fsm >= 3) f = 1.0f;
else {
    a = OBJ(g_racer_info[fsm].object /*0x485b2c + fsm*0x58*/)->ai_index;
    d = (float)(AI(a)->progress /*+0x60*/ - g_player_progress);
    T = &g_rubber_tab[(g_difficulty*7 + g_cur_map)*3 + fsm];   /* 0x482258, 12-byte {ahead, behind, gain} */
    if (d > 0.0f) {
        if (d > 50.0f) d = 50.0f;
        if (d > T->ahead) f = 1.0f - d * T->gain * 0.02f;          /* 0x3ca3d70a */
        else              f = g_difficulty == 0 ? A[fsm] : B[fsm];
    } else {
        if (d < -50.0f) d = -50.0f;
        if (d < T->behind) f = 1.0f - d * T->gain * 0.02f;
        else               f = g_difficulty == 0 ? C[fsm] : D[fsm];
    }
}
if (f < 0.2f) f = 0.2f;              /* 0x3e4ccccd */
if (f > 2.0f) f = 2.0f;
return f;
```
Table 0x482258 (same for all 3 racers):

| diff \ track | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| 0 (ahead,behind,gain) | 25,-25,1 | 25,-25,1.5 | 30,-20,2 | 30,-20,2.5 | 35,-15,3 | 35,-15,3 | 45,-10,4 |
| 1 | 35,-20,2 | 35,-20,2.5 | 40,-15,3 | 40,-15,3.5 | 45,-10,4 | 45,-5,4.5 | 50,-5,5 |

---------------------------------------------------------------------------------------------------

### 4. FSM state handlers (ai_path.cpp)

All are `void handler(int fsm)`. State names from sec_fsm.md. Common pattern "path frame":
```c
h = ai_wmap_add(E->object /*,1,-1,5,3*/);
if (h < 0) fatal("Could not initialize the world mapper with twp %d: Error value %d", E->twp, h);
wp = ai_wmap_get_waypoint(h);
saved = E->twp;                                  /* +0x1c */
E->twp = ScanPath(fsm, h, 5, wp, NEAR, FAR);
... if (E->twp > -1) {...} else E->twp = saved;
... ai_wmap_remove(h);
```
NEAR = position of path[2] (steering target), FAR = path[4] (speed target) - but note which local each
handler passes (some swap them, transcribed exactly).

#### ai_state_race (0x409d10) - states 0, 1 (and 3 via 0x409df0)
```c
h = ai_wmap_add(E->object); if (h < 0) fatal("Could not initialize the world mapper with twp %d: Error value %d", E->twp, h);
wp = ai_wmap_get_waypoint(h);
E->coast = 0;                                    /* +0x40 */
saved = E->twp;
E->twp = ScanPath(fsm, h, 5, wp, near, far);
if (E->twp > -1) {
    if (!ai_slower_car_ahead(wp, E->object)) ai_speed_control(fsm, far);
} else E->twp = saved;
CalculateStearing(fsm, near);
ai_wmap_remove(h);
```

#### ai_state_cop_chase (0x409df0) - state 3
`ai_state_race(fsm);`

#### ai_state_block_player (0x409e00) - state 2
World mapper placed on the *target* (player) object; ScanPath from its waypoint.
```c
h = ai_wmap_add(E->target /*+0x08*/); if (h < 0) fatal("Could not initialize the world mapper: Error value %d", h);
wp = ai_wmap_get_waypoint(h);
saved = E->twp;
E->twp = ScanPath(fsm, h, 5, wp, near /*[esp+0x10]*/, far /*[esp+0x1c]*/);
if (E->twp > -1) { ai_speed_control(fsm, far); CalculateStearing(fsm, near); }
else { E->brake = 0; E->throttle = 0; E->twp = saved; CalculateStearing(fsm, near); }
E->timer /*+0x10*/ += dt();
ai_wmap_remove(h);
```

#### ai_state_cop_target_behind (0x409f60) - state 4
```c
h = ai_wmap_add(E->object); if (h < 0) fatal("Could not initialize the world mapper: Error value %d", h);
saved = E->twp;
E->twp = ScanPath(fsm, h, 5, OBJ(E->object)->waypoint /*+0x20*/, near, far);
if (E->twp > -1) {
    if (OBJ(E->target)->speed < OBJ(E->object)->speed) {
        E->brake = 50.0f; E->throttle = 0.0f;
    } else {
        ai_speed_control(fsm, far);
        float p[3] = { O3D(E->target)->x, O3D(E->target)->y, O3D(E->target)->z };
        a = ai_get_angle_to_point(E->object, p);           /* sub_401690 */
        if (a > 1800 && a < 2200) E->brake = 100.0f;        /* target straight behind */
    }
} else { E->brake = 0; E->throttle = 0; E->twp = saved; }
CalculateStearing(fsm, near);
ai_wmap_remove(h);
```

#### ai_state_finished (0x409ed0) - state 9
```c
E->coast = 1;
E->steer += 75.0f * dt();  if (E->steer > 100.0f) E->steer = 100.0f;
E->throttle = 0; E->brake = 0;
ai_steer_physics(fsm); car_suspension(E->object); car_pitch_spring(E->object); car_roll_spring(E->object);
```

#### ai_state_stuck_reverse (0x40a190) - state 10
```c
if (!(E->stuck_time /*+0x14*/ < 2.0f)) {
    E->steer = 0; OBJ(E->object)->speed = -15.0f;
    E->stuck_time = 0; E->reverse_t /*+0x18*/ = 0;
    car_suspension(obj); car_pitch_spring(obj); car_roll_spring(obj);
    return;
}
E->reverse_t += dt();
car_roll_spring(obj); car_pitch_spring(obj); car_suspension(obj);
if (E->reverse_t > 1.0f) {
    E->state = (fsm < 3) ? 0 : 3;
    E->reverse_t = 0;
}
```
(no drive/steer physics: the car rolls backwards with the speed set once.)

#### ai_state_racer_wait (0x40a290) - state 11
```c
h = ai_wmap_add(E->object); if (h < 0) fatal("...with twp %d...", E->twp, h);
wp = ai_wmap_get_waypoint(h); saved = E->twp;
E->twp = ScanPath(fsm, h, 5, wp, near /*[esp+0x10]*/, far /*[esp+0x1c]*/);
if (E->twp > -1) ai_set_steering(fsm, near); else E->twp = saved;
ai_drive(fsm); ai_steer_physics(fsm);
car_suspension(obj); car_roll_spring(obj); car_pitch_spring(obj);
OBJ(E->object)->speed = 16.666f;                /* 0x418553f8 */
ai_wmap_remove(h);
```

#### ai_state_cop_arrest (0x40a390) - state 5 ('An illegal racer was arrested')
```c
E->timer = 0;
cop_siren(E->object, 0);                         /* sub_40c640 */
if (E->target == g_player_object /*0x4d9e28*/) {
    if (g_game_state /*0x4d9a7c*/ == 0) sub_424d20(8);   /* player busted sequence */
} else {
    idx = -1;
    for (i = 0; i < 3; i++) if (g_fsm[i].object == E->target) idx = i;
    if (idx == -1) fatal("An illegal racer was arrested in %s %d", "C:\\work\\A2racII\\Game\\ai_path.cpp", 1089);
    g_fsm[idx].state = 7;                         /* FSM_RACER_ARRESTED */
}
for (i = 3; i < 15; i++) if (g_fsm[i].target == E->target) g_fsm[i].state = 6;   /* incl. this cop */
car_suspension(obj); car_pitch_spring(obj); car_roll_spring(obj);
```

#### ai_state_cop_arrested_wait (0x40a480) - state 6
```c
OBJ(obj)->speed = 0; OBJ(obj)->vel_lat /*+0x6c*/ = 0; OBJ(obj)->yaw_rate /*+0x7c*/ = 0;
car_suspension(obj); car_pitch_spring(obj); car_roll_spring(obj);
```

#### ai_state_racer_arrested (0x40a610) - state 7
```c
E->throttle = 0; E->brake = 0;
OBJ(obj)->speed = 0; OBJ(obj)->vel_lat = 0; OBJ(obj)->yaw_rate = 0;
car_suspension(obj); car_pitch_spring(obj); car_roll_spring(obj);
```

#### ai_state_cop_parked (0x40a4f0) - state 8
```c
if (rceng_chunk_distance(OBJ(obj)->chunk, OBJ(g_player_object)->chunk) > 30) {
    OBJ(obj)->field_34 = 0;                       /* object +0x34 */
    ai_fsm_release_cop(fsm);                      /* sub_4035b0 */
    return;
}
car_suspension(obj); car_roll_spring(obj); car_pitch_spring(obj);
OBJ(obj)->speed    -= dt() * (OBJ(obj)->speed    * 3.0f);
OBJ(obj)->vel_lat  -= dt() * (OBJ(obj)->vel_lat  * 3.0f);
OBJ(obj)->yaw_rate -= dt() * (OBJ(obj)->yaw_rate * 3.0f);
```

#### ai_state_cop_target_wrong_way (0x40aab0) - state 14
(0x40aab0..0x40ac8f only; 0x40ac90 is HANDLE_RACER_AI, see below.)
```c
h = ai_wmap_add(E->object); if (h < 0) fatal("...with twp %d...", E->twp, h);
wp = ai_wmap_get_waypoint(h); saved = E->twp;
E->twp = ScanPath(fsm, h, 5, wp, near /*[esp+0x1c]*/, far /*[esp+0x10]*/);
if (E->twp > -1) {
    if (ai_chunk_ahead(OBJ(E->object)->chunk, OBJ(E->target)->chunk)) {   /* sub_407660 */
        if (OBJ(E->object)->speed > OBJ(E->target)->speed) {
            E->brake += 400.0f * dt(); if (E->brake > 100.0f) E->brake = 100.0f;
            E->throttle = 0;
        }
    } else ai_speed_control(fsm, far);
    ai_set_steering(fsm, near);
} else E->twp = saved;
ai_drive(fsm); ai_steer_physics(fsm);
if (OBJ(obj)->speed < 0.0f) { OBJ(obj)->speed = 0; car_stop_damp(obj) /*sub_40fba0*/; E->brake = 0; }
E->stuck_time = 0;                                /* +0x14 */
car_suspension(obj); car_roll_spring(obj); car_pitch_spring(obj);
ai_wmap_remove(h);
```

#### ai_state_cop_give_up (0x40a9d0) - state 15
```c
E->coast = 1;
h = ai_wmap_add(E->object); if (h < 0) fatal("...with twp %d...", E->twp, h);
wp = ai_wmap_get_waypoint(h); saved = E->twp;
E->twp = ScanPath(fsm, h, 5, wp, near, far);
if (E->twp > -1) {
    if (ai_slower_car_ahead(wp, E->object)) E->coast = 1;
    else ai_speed_control(fsm, far);
} else E->twp = saved;
CalculateStearing(fsm, near);
ai_wmap_remove(h);
```

#### ai_state_cop_target_other_side (0x40a6a0) - state 16
Identical to state 14 except the last part:
```c
... (same as 0x40aab0 up to the speed < 0 check) ...
if (OBJ(obj)->speed < 0.0f) { OBJ(obj)->speed = 0; car_stop_damp(obj); E->brake = 0; }
if (!(OBJ(E->target)->speed > 0.0f)) E->stuck_time = 0;      /* only when the target stands still */
car_suspension(obj); car_roll_spring(obj); car_pitch_spring(obj);
ai_wmap_remove(h);
```
ScanPath locals: near = [esp+0x1c], far = [esp+0x10] (same as 14).

#### ai_state_cop_depart (0x40a8a0) - state 17
```c
OBJ(obj)->field_38 = 0;                           /* object +0x38 */
float p[3]; p[0] = O3D(E->target)->x; p[2] = O3D(E->target)->z;   /* p[1] uninitialised */
a = ai_get_angle_to_point(obj, p);
if (a > 0x400 && a < 0xc00) {                     /* target behind */
    ai_state_race(fsm);
    OBJ(obj)->speed = 10.0f;
    E->timer += dt();
    if (E->timer > 3.0f) OBJ(obj)->speed = 0;
}
car_suspension(obj); car_pitch_spring(obj); car_roll_spring(obj);
```

---------------------------------------------------------------------------------------------------

### 5. Racer per-object AI (ai_racer.cpp)

#### HANDLE_RACER_AI (0x40ac90)
`int handle_racer_ai(int object)` - object callback registered by sub_411170 (`add_car(..., handler=0x40ac90)`).
Used only when `ai_model == 1` (the FSM path is used when ai_model == 0); otherwise it only animates.
Racer "state" t_ai+0x34: 0 = rubber band on progress, 1 = pace the player, 2 = finished (no speed logic).
```c
if (g_ai_model /*0x4d9b10*/ != 1) { car_wheel_anim(object) /*sub_413be0*/; return 1; }
if (!g_4d9a9c || !g_4d9a78) return 1;            /* race not running */
ai = OBJ(object)->ai_index;                        /* +0x0c */
check_collision(object);                           /* sub_4084b0 ('Checking for collision') */
A = AI(ai);
if (A->collision /*+0x2c*/ == 0) {                 /* 'No collision.' */
    if (OBJ(object)->chunk != OBJ(object)->prev_chunk /*+0x1c*/) check_racer_trigger(object, -1);
    b = A->behavior;                               /* +0x24 */
    switch (A->racer_state /*+0x34*/) {
    case 0: {
        d = A->progress /*+0x60*/ - g_player_progress;
        if (d > 5) A->max_speed /*+0x18*/ = BH(b)->top_speed /*+0x14*/ - (float)(2*d - 10);
        else       A->max_speed = BH(b)->top_speed;
        if (A->max_speed < 5.0f) A->max_speed = 5.0f;
        break; }
    case 1: {
        d = OBJ(object)->chunk - OBJ(g_player_object)->chunk;     /* plain difference, not wrapped */
        if (d < -9) {
            A->max_speed = g_player_top_speed;                     /* float 0x4d9e68 */
            if (BH(b)->top_speed > A->max_speed) A->max_speed = BH(b)->top_speed;
        } else if (d < -3) {
            A->max_speed = BH(b)->top_speed;
            A->racer_state = 0;
        } else {
            if (d > 0) A->max_speed = (1.0f - (float)(d / 15) * 0.2f) * OBJ(g_player_object)->speed;
            else       A->max_speed = OBJ(g_player_object)->speed;
            if (A->max_speed < 15.0f) A->max_speed = 15.0f;
        }
        break; }
    case 2: break;
    default: fatal("%s(%d): Illegal racer state.\n", "C:\\work\\A2racII\\Game\\ai_racer.cpp", 134);
    }
    change_lane(object, A->ai_state /*+0x38*/, -1);           /* sub_403660 */
    if (A->ai_state != 7) handle_basic_ai(object);             /* sub_4076b0; 7 = refuelling stop */
}
sndfx_play_object_sample(object, A->sample /*+0x4c*/);         /* sub_428bc0 */
car_wheel_anim(object);                                        /* sub_413be0 */
return 1;
```
(`d / 15` is integer division, magic 0x88888889.)

#### check_racer_trigger (0x40af90)
`void check_racer_trigger(int object, int chunk)` - fires both chunk triggers and the waypoint trigger.
```c
if (chunk == -1) chunk = OBJ(object)->chunk;
ai = OBJ(object)->ai_index;
for (k = 0; k < 2; k++)  racer_trigger(CHUNK(chunk)->trigger[k] /*+0xec,+0xf0*/, ai, object);   /* raw chunk number */
racer_trigger(WP(OBJ(object)->waypoint)->trigger /*+0x04*/, ai, object);
```

#### racer_trigger (0x40b030)
`void racer_trigger(int trigger, int ai, int object)` - byte table 0x40b294 indexed by `trigger-2`
(0..0x2d), jump table 0x40b278.

| value | name | action |
|---|---|---|
| 2 | TRIGGER_BENZINE_STATION | `A->max_speed = 0; OBJ(object)->speed = 0; A->ai_state = 7;` |
| 9 | TRIGGER_ZERO_CHUNK_MARKER | `sub_42ae20(object);` |
| 14 | TRIGGER_FINISH_POSITION | see below |
| 15 | TRIGGER_FINISH_VALID | `A->finish_valid /*+0x40*/ = 1;` |
| 46 | TRIGGER_FUEL_APPROACH | `A->ai_state = 5; A->max_speed = BH(A->behavior)->top_speed * 0.5f;` |
| 47 | TRIGGER_FINAL_APPROACH | `A->ai_state = 1; A->max_speed = BH(A->behavior)->top_speed * 0.25f;` |
| other | - | debug 'Unknown trigger %d.' only (also for 0) |

```c
case 14: /* TRIGGER_FINISH_POSITION */
    if (A->finish_valid /*+0x40*/ == 1) {
        A->laps /*+0x68*/++;
        if (A->laps == g_nr_laps /*0x4d9ad8*/ && A->behavior != 0) {
            A->behavior = 0;
            A->max_speed = 0;
            OBJ(object)->handler /*+0x44*/ = handle_basic_ai;     /* 0x4076b0 */
            A->racer_state = 2;
            /* 'Racer (obj %d) has finished.' */
            racer_finished(object);                               /* sub_411650 */
        }
    }
```
(finish_valid is not reset here.)

---------------------------------------------------------------------------------------------------

### 6. Car-control library interface (0x40ce10..0x410a10), as used by the AI

All car-physics parameters come from the **global** `g_player_car_model` (0x4d9e2c) and the player's upgrade
options (0x4d9ec4 banden/tyres, 0x4d9ed0 versnellingsbak/gearbox) - NOT from fsm+0x30. Parameter tables:
`P_spring[m]` at 0x4a5df0 (stride 0x18) and `P_car[m]` at 0x4a5ee0 (stride 0x7c, 31 floats), see Structures.

| addr | name | args | reads | writes |
|---|---|---|---|---|
| 0x40fe90 | ai_heading_diff | (obj, float pos[3]) -> int | O3D pos x/z, yaw +0x78 | - |
| 0x40ff90 | ai_drive | (fsm) | E throttle/brake/gear/coast, obj speed | obj speed +0x74, E gear +0x44, E stopped +0x58, obj pitch rate +0x78 |
| 0x40eae0 | car_gear_accel | (fsm, float *ratio) -> float | E gear, obj speed, rubber band | *ratio |
| 0x410280 | ai_steer_physics | (fsm) | E steer/coast/skid, obj speed/vel_lat/yaw_rate, O3D yaw | obj yaw_rate +0x7c, vel_lat +0x6c, speed; O3D roll +0x7c; E skid fields |
| 0x40ebd0 | car_roll_spring | (obj) | | obj +0x80, O3D +0x7c |
| 0x40ece0 | car_pitch_spring | (obj) | | obj +0x78, O3D +0x74 |
| 0x40ee80 | car_suspension | (obj) | road height | obj +0x70, O3D y +0x54 |
| 0x40ede0 | car_ground_clamp | (obj) | road height | O3D y, obj +0x70 (not called by my functions) |
| 0x40fba0 | car_stop_damp | (obj) | | obj +0x6c, +0x74, +0x7c |

Note the car never moves here: position integration from speed/vel_lat/yaw_rate is done by the engine object
update (rceng_handle_objects), not in these functions.

#### ai_heading_diff (0x40fe90)
```c
float dx = p[0] - O3D(obj)->x, dz = p[2] - O3D(obj)->z;
/* (corrected from the listing: the clamps and the sign test are on dx, the quotient is dz/dx, as in 0x401690) */
if (dx == 0.0f) dx = 0.0f;
if (dx > 0.0f && dx <  0.1f) dx =  0.1f;
if (dx < 0.0f && dx > -0.1f) dx = -0.1f;
float a = atanf(dz / dx) * 0.15915494f * 4096.0f;        /* 1/(2pi) */
if (dx > 0.0f) a += 3072.0f; else a += 1024.0f;           /* written as a - (-3072) / a - (-1024) */
a -= O3D(obj)->yaw;
if (a > 2048.0f) a -= 4096.0f;
if (a < -2048.0f) a += 4096.0f;                           /* written as a - (-4096) */
return (int)a;                                            /* _ftol, truncation */
```

#### car_gear_accel (0x40eae0)
```c
bonus = (fsm >= 3 && g_fsm[fsm].state == 3) ? 0.1f : 0.0f;          /* chasing cop */
gear = E->gear; obj = E->object;
f = ai_rubberband_factor(fsm);
if (g_demo /*0x4d9b20*/ && f < 1.0f) f = 1.0f;
k = m*31 + g_opt_gearbox*6 + gear;                                  /* m = g_player_car_model */
r = OBJ(obj)->speed / (P_gear_top[k] /*0x4a5f10*/ * f);
*ratio = r;
acc = (1.0f - r) * P_gear_acc[k] /*0x4a5ee0*/ * f;
if (acc < 0.0f) acc = 0.0f;
return acc + bonus;
```

#### ai_drive (0x40ff90)
```c
thr = E->throttle; brk = E->brake; gear = E->gear; obj = E->object;
max_gear = (g_opt_gearbox != 0) + 4;
t = dt();
if (thr == 0.0f) {                                   /* engine braking / rolling resistance */
    if (spd < 0.0f) { spd += P_decel[m] /*0x4a5f50*/ * t; if (spd > 0.0f) spd = 0; }
    else            { spd -= P_decel[m] * t;            if (spd < 0.0f) spd = 0; }
}
if (thr > 0.0f) E->stopped = 0;
spd0 = spd;
acc = car_gear_accel(fsm, &r);
if (spd > 1.0f) {
    if (r > P_upshift[m] /*0x4a5f40*/ && gear < max_gear) { E->gear++; acc = car_gear_accel(fsm, &r); }
    if (r < P_downshift[m] /*0x4a5f44*/ && gear > 0)      { E->gear--; acc = car_gear_accel(fsm, &r); }  /* old 'gear' */
}
rev = P_reverse[m] /*0x4a5f4c*/; if (rev > 0.0f) rev = -rev;
if (E->coast) { acc = 0; rev = 0; }
spd = spd0 + (thr*acc + brk*rev) * t;               /* OBJ(obj)->speed */
if (brk > 0.0f) {
    if (fabs(spd) < 0.4 && !E->stopped) { spd = 0; E->stopped = 1; }
    d = spd * brk; if (d > 1000.0f) d = 1000.0f;
    OBJ(obj)->pitch_rate /*+0x78*/ -= d * t;        /* nose dive */
}
if (spd < -13.333f) spd = -13.333f;                 /* 0xc15553f8 */
```

#### ai_steer_physics (0x410280) (summary transcription)
```c
t = dt(); E->steer = -E->steer;                     /* negated for the computation, restored at the end */
if (!E->coast) {
    k = OBJ->yaw_rate * 10.0f; OBJ->yaw_rate -= k * t;
    if (!E->coast) {
        E->skid_yaw /*+0x4c*/ = 0; E->skid_heading /*+0x50*/ = 0; E->skid_t /*+0x48*/ = 0;
        k = OBJ->vel_lat * (g_opt_tyres /*0x4d9ec4*/ ? 10.0f : 5.0f);
        OBJ->vel_lat -= k * t;
    }
}
aspd = fabs(OBJ->speed);
if (aspd < 2.0 && fabs(OBJ->vel_lat) < 2.0) grip = sin(fabs(PI*OBJ->speed*0.25f)) * P_grip[m] /*0x4a5f58*/;
else grip = P_grip[m];
a = E->steer*0.5 * 2PI * (1/360.f);  wrap a into [0,2PI) once (a > 2PI: -2PI; a < 0: +2PI);
U = (2PI/4096) /*0x3ac90fda*/ * t * (grip*E->steer + sin(a) * OBJ->speed*50.0f);   /* unwrapped, kept */
D = U wrapped once into [0,2PI);
heading_rad = ((int)O3D->yaw % 4096, +4096 if negative) * (1/4096) * 2PI;
if (!E->coast && E->skid_t == 0.0f) {                /* normal grip driving */
    f = 10.0 - aspd*0.15; if (f < 2.5f) f = 2.5f;
    if (D > 0.0f && D < 2PI) {
        if (OBJ->speed < 0.0f) OBJ->yaw_rate += U*f; else OBJ->yaw_rate -= U*f;
        if (OBJ->speed < 10.0f && g_4a63dc != 0) O3D->roll /*+0x7c*/ += t * P_roll[m] /*0x4a5f54*/ * E->steer;
        else                                      O3D->roll -= t * P_roll[m] * E->steer;
    } else {
        if (OBJ->speed < 0.0f) OBJ->yaw_rate -= E->steer * -0.004f; else OBJ->yaw_rate -= U*f;
        O3D->roll += t * P_roll[m] * E->steer;
        OBJ->speed -= k * t * 0.5f;                  /* k = last value of the damping temp above */
    }
    r = OBJ->yaw_rate * t;
    lat = OBJ->vel_lat*cos(r) - sin(r)*OBJ->speed;
    OBJ->vel_lat = g_opt_tyres ? lat : lat * 0.8f;
} else {                                             /* skid / coast mode (states 9, 15) */
    if (E->skid_heading == 0.0f) E->skid_heading = heading_rad;
    E->skid_t += t; if (E->skid_t > 1.0f) E->skid_t = 1.0f;
    v = sqrt(vel_lat^2 + speed^2);
    if (v > 1.0f) {
        y = sin(v*PI*0.004f) * (E->skid_t * E->steer * 0.18f) * (fsm < 3 ? 0.5f : 1.25f);
        OBJ->yaw_rate = (OBJ->speed < 0.0f && g_4a63e4 /*byte*/) ? y : -y;
        E->skid_yaw += t * y;
        if (fsm < 3) { m2 = (E->steer != 0) ? 1 : 2; vel_lat -= m2*vel_lat*t; speed -= m2*speed*t; }
        else         { vel_lat -= 3*vel_lat*t; speed -= 3*speed*t; }
    } else { vel_lat = 0; speed = 0; yaw_rate = 0; }
}
E->steer = -E->steer;
```

#### car_roll_spring (0x40ebd0) / car_pitch_spring (0x40ece0)
Damped spring with random jitter (rand()%10 < 5 -> +, else -) proportional to speed:
```c
/* roll: rate = obj+0x80, angle = O3D+0x7c, damping P_spring[m].c0 (0x4a5df0) */
/* pitch: rate = obj+0x78, angle = O3D+0x74, damping P_spring[m].c8 (0x4a5df8) */
k = -(damp * rate) - P_spring[m].c4 /*0x4a5df4*/ * angle;
rate  += dt() * k;
angle += dt() * rate;
if (rand() % 10 < 5) rate += 4.0f * dt() * OBJ->speed;     /* written rate - dt*speed*(-4) */
else                 rate -= 4.0f * dt() * OBJ->speed;
```

#### car_suspension (0x40ee80)
Vertical spring to the road height sampled at a look-ahead point.
```c
i = (int)O3D->yaw & g_trig_mask /*[0x49b018]*/;
S = g_tab_a /*[0x4ea4b4]*/[i]; C = g_tab_b /*[0x4ea418]*/[i];
t1 = S*OBJ->vel_lat - C*OBJ->speed;
t2 = (C + S) * OBJ->speed;                          /* sic */
x = O3D->x + 10.0f*dt()*t1;  z = O3D->z + 10.0f*dt()*t2;   /* local only */
c = road_find_chunk(this=&g_road 0x4e5d28, OBJ->chunk, x, O3D->y, z);           /* sub_445a10 */
h = chunk_road_height(this=CHUNK(g_road_chunk_order[c]), x, z);                  /* sub_442f00 */
k = -(P_spring[m].c10 * OBJ->vel_y /*+0x70*/) - (O3D->y_old - h) * P_spring[m].c14;
OBJ->vel_y += dt()*k;
O3D->y += dt()*OBJ->vel_y;
if (y_old < h - 0.5f) { O3D->y = h - 0.5f; OBJ->vel_y = -OBJ->vel_y; }
if (h + 2.0f < y_old) { if (obj == g_player_object) g_4a63dc = 0; OBJ->vel_y -= 10.0f*dt(); }
```
(`y_old` = O3D y copied at function entry.)

#### car_ground_clamp (0x40ede0)
```c
h = chunk_road_height(CHUNK(g_road_chunk_order[OBJ->chunk]), O3D->x, O3D->z);
if (h - 1.0f > O3D->y) { O3D->y = h - 1.0f; OBJ->vel_y = -OBJ->vel_y; }
```

#### car_stop_damp (0x40fba0)
`vel_lat -= vel_lat*10*dt; speed -= speed*10*dt; yaw_rate -= yaw_rate*10*dt;` (each with its own dt() call).

---------------------------------------------------------------------------------------------------

### Structures

| struct | off | type | name | evidence |
|---|---|---|---|---|
| world mapper (0x4a5c08, 8 x 0x18) | +0x00 | int | object (-1 free) | 40be70 0x40bed9 |
| | +0x04 | int | chunk (object +0x18 at add) | 40be70 0x40bee8, 40bf90 |
| | +0x08 | int | start waypoint = drmap_find_closest_waypoint(obj,1) | 40be70 0x40bf0a, 40bf70 |
| | +0x0c | int | unused (set -1 in remove only) | 40bf20 |
| | +0x10 | int | dir_fwd: 1 fwd / -1 backward (only 1 or 0 ever written) | 40bf90 0x40bfa8 |
| | +0x14 | int | dir_side: 1 / -1 (only 1 or 0 ever written) | 40bf90 0x40bf9d |
| scan node (stack) | +0..+2 | u8[3] | tried flags per direction | 409670 |
| | +3 | int | waypoint (packed, node size 7) | 409670 0x409744 |
| t_fsm (0x4a55a0) | +0x00 | int | object | all |
| | +0x04 | int | state | 40a190, 40a390, 40eae0 |
| | +0x08 | int | target object | 409e00, 409f60, 40a390 |
| | +0x10 | float | state timer | 409e00, 40a390, 40a8a0 |
| | +0x14 | float | stuck_time (time in chunk) | 40a190, 40a6a0, 40aab0 |
| | +0x18 | float | reverse timer | 40a190 |
| | +0x1c | int | twp (ScanPath result) | all path handlers |
| | +0x34 | float | steer -100..100 | 401230, 4013c0, 410280 |
| | +0x38 | float | throttle 0..100 | 4012c0, 40ff90 |
| | +0x3c | float | brake 0..100 | 4012c0, 40ff90 |
| | +0x40 | int | coast / skid mode flag | 409d10, 409ed0, 40a9d0, 40ff90, 410280 |
| | +0x44 | int | gear | 40eae0, 40ff90 |
| | +0x48 | float | skid_t (0..1) | 410280 |
| | +0x4c | float | skid yaw accumulator | 410280 |
| | +0x50 | float | heading (rad) at skid start | 410280 |
| | +0x58 | int | stopped (brake-to-standstill latch) | 40ff90 |
| object (0x4e48e0, 0xa0) | +0x0c | int | ai index | 40ac90, 40af90 |
| | +0x18 | int | chunk | many |
| | +0x1c | int | previous chunk | 40ac90 0x40ad41 |
| | +0x20 | int | current waypoint | 409670, 409f60, 40af90 |
| | +0x34 | int | ? (cleared on cop release) | 40a4f0 |
| | +0x38 | int | ? (cleared in state 17) | 40a8a0 |
| | +0x44 | fnptr | per-object handler (set to handle_basic_ai at finish) | 40b030 0x40b0d2 |
| | +0x6c | float | lateral velocity | 40a480, 410280 |
| | +0x70 | float | vertical velocity | 40ee80, 40ede0 |
| | +0x74 | float | forward speed | many |
| | +0x78 | float | pitch rate | 40ece0, 40ff90 |
| | +0x7c | float | yaw rate | 410280, 40fba0 |
| | +0x80 | float | roll rate | 40ebd0 |
| object3d (0x4ea9e0, 0x99) | +0x50/+0x54/+0x58 | float | x / y / z | 40fe90, 40ee80 |
| | +0x74 | float | pitch angle | 40ece0 |
| | +0x78 | float | yaw (0..4096) | 4013c0, 40fe90 |
| | +0x7c | float | roll angle | 40ebd0, 410280 |
| waypoint (0x50) | +0x00 | int | flags; bit 8 = side-switch point | 40bf90 0x40c07b |
| | +0x04 | int | trigger | 40af90 |
| | +0x08 | float[3] | pos | 40c350, 40b970 |
| | +0x14 | int | link straight (next, same lane) | 40bf90 dir1 |
| | +0x18 | int | link lane change A | 40bf90 dir0 |
| | +0x1c | int | link lane change B | 40bf90 dir2 |
| | +0x20 | int | link backward | 40bf90 dir3 |
| | +0x24 | int | obstacle count | 40c3a0 |
| chunk (0x184) | +0xe4 / +0xe8 | int | lanes left / right | 40c400, 40c440, 40b970 |
| | +0xec / +0xf0 | int | trigger[2] | 40af90 |
| | +0x124 / +0x154 | int[12] | lane waypoints left / right | 40c440, 40b970, 40bf90 |
| t_ai (0x6c) | +0x18 | float | max speed allowed | 40ac90, 40b030 |
| | +0x24 | int | behavior | 40ac90, 40b030 |
| | +0x2c | int | collision flag | 40ac90 |
| | +0x34 | int | racer_state 0/1/2 | 40ac90, 40b030 |
| | +0x38 | int | ai_state (lane/behaviour state; 1, 5, 7 set by triggers) | 40ac90, 40b030 |
| | +0x40 | int | finish_valid | 40b030 |
| | +0x4c | int | sound sample | 40ac90 |
| | +0x60 | int | progress (chunks) | 4014e0, 40ac90 |
| | +0x68 | int | laps | 40b030 |
| t_ai_behavior (0x2c) | +0x14 | float | top speed | 40ac90, 40b030 |
| racer info (0x485b20, 0x58) | +0x0c | int | object | 4014e0 |
| P_spring (0x4a5df0, 0x18) | +0x00 | float | roll damping | 40ebd0 |
| | +0x04 | float | roll/pitch stiffness | 40ebd0, 40ece0 |
| | +0x08 | float | pitch damping | 40ece0 |
| | +0x10 / +0x14 | float | vertical damping / stiffness | 40ee80 |
| P_car (0x4a5ee0, 0x7c) | +0x00 | float[2][6] | accel per gearbox/gear | 40eae0 |
| | +0x30 | float[2][6] | gear top speed | 40eae0 |
| | +0x60 / +0x64 | float | upshift / downshift ratio | 40ff90 |
| | +0x6c | float | reverse/brake accel (made negative) | 40ff90 |
| | +0x70 | float | coast deceleration | 40ff90 |
| | +0x74 | float | steering roll gain | 410280 |
| | +0x78 | float | grip / steering gain | 410280 |
| rubber table (0x482258, 12 B) | +0/+4/+8 | float | ahead thr / behind thr / gain | 4014e0 |

### Globals

| addr | type | name | meaning |
|---|---|---|---|
| 0x4a5c08 | t_world_mapper[8] | g_world_mappers | world mapper slots |
| 0x4a5bd8 | int[10+1] | g_obstacle_list | scratch obstacle list (waypoint_get_obstacles) |
| 0x4a5c00 | int | g_ai_object_index? | only printed in HANDLE_RACER_AI debug ('ai_object_index = %d'), used by camera code |
| 0x482258 | float[2][7][3][3] | g_rubber_tab | rubber band thresholds (difficulty, track, racer) |
| 0x4d9b10 | int | g_ai_model | Game.ini OPTIONS ai_model; 1 -> HANDLE_RACER_AI active |
| 0x4d9b20 | int | g_demo | Game.ini OPTIONS demo; rubber band never < 1 |
| 0x4d9ab0 | int | g_difficulty | Game.ini OPTIONS difficulty |
| 0x4d9ad8 | int | g_nr_laps | Game.ini OPTIONS NRLAPS |
| 0x4d9e2c | int | g_player_car_model | car model index used for ALL car physics tables |
| 0x4d9ec4 | int | g_opt_tyres | raser.ini/PLAYER 'banden' upgrade |
| 0x4d9ed0 | int | g_opt_gearbox | PLAYER 'versnellingsbak' upgrade (5 gears instead of 4) |
| 0x4d9e68 | float | g_player_top_speed | from sub_40ce10(player) at init_player_car |
| 0x4d9e70 | int | g_player_progress | player chunk progress |
| 0x4d9e28 | int | g_player_object | |
| 0x4d9a7c | int | g_game_state | 0 -> arrest of player starts sub_424d20(8) |
| 0x4d9a9c, 0x4d9a78 | int | race running flags | |
| 0x4a63dc | int | g_4a63dc | player-car physics state (cleared when car airborne) used in steer roll |
| 0x4a63e4 | byte | g_4a63e4 | player-car physics flag (reverse steering) |
| 0x49b018 | int | g_trig_mask | index mask for 0x4ea4b4 / 0x4ea418 trig tables |

### Open questions

- World mapper dir fields are never set to -1, so SIDESWITCH cases B/C/D (and ai_wmap_closest_waypoint) are
  dead at runtime. Port them anyway or assert.
- ScanPath fallback single-lane offset: `cos/sin(1024.0)` in radians, uses component [1] (height?) for dy and
  writes component [2]; looks like a source bug (1024 = 90 deg in 4096 units). Must be reproduced for 1:1.
- All AI cars use the player's car model / upgrades for physics parameters (g_player_car_model, g_opt_*),
  fsm+0x30 (car model) is not used by the control library. Verify if the player model global is swapped
  elsewhere before AI update (no writes found besides 0x4228b0/0x4256a0).
- sub_413be0 (called at end of HANDLE_RACER_AI) assumed wheel/animation update; sub_424d20(8) = busted sequence.
- object +0x34 / +0x38 meaning unknown.


## 8. Police (ai_cops.cpp, Car_cops.cpp, ev_cops.cpp)


Conventions: `OBJ(i)` = `((t_object*)g_objects)[i]` (0x4e48e0, stride 0xa0), `AI(i)` = t_ai[i] (0x4a5b40 data, stride
0x6c), `COP(i)` = t_cop[i] (0x4a5238 data, stride 0x20), `BH(i)` = t_ai_behavior[i] (0x4a5b88, stride 0x2c),
`FSM(i)` = fsm entry i (0x4a55a0, stride 0x60). `dt()` = `sub_441cd0(&g_timer 0x4e4970)` (clamped frame time, s).
Debug printf (sub_464ca0) calls are omitted; sub_464e50 = `fatal()`.

### Two cop AIs selected by Game.ini `ai_model` (0x4d9b10)

Game.ini: "Selects AI model: Old (Autobahn Raser I)=1, New (A2 Racer II)=0" (default 0).

* `ai_model == 1` (old): HANDLE_COP_AI runs the 5-state t_cop machine of ai_cops.cpp (On the lookout / In pursuit /
  Got him / Quitting chase / Calling it quits).
* `ai_model == 0` (new, default): HANDLE_COP_AI only does the "lookout" test `cop_fsm_lookout` (0x409060) which puts
  the cop into a free cop FSM slot (`fsm_add_cop` 0x4034a0, FSM entries 3..14, start state 3), and brakes a parked
  cop. Pursuit/arrest then run through the FSM (transition table 0x4a5280, see end).

Both models end in the same arrest event: `game_action_start(8)` (sub_424d20(8)) = ev_cops.cpp arrest sequence.

### Cop placement (who creates cops)

Cops are static track objects. `road_load_static_objects(file)` (0x4451d0, rd_main.cpp, called from
road_load_track_files with `"%s.omp"` = `etappeN.omp`) reads `int count` then `count` records of 68 bytes:
`int chunk; int type; float pos[3]; float rot[3] (degrees, scaled by 0.00277778*[0x49b014] i.e. deg->angle units,
y negated); char animname[32]; int ?`. It creates each object (sub_43f770), sets `OBJ.chunk(+0x18) = chunk`, then
`switch (type)`: 5, 8, 10 = movable props (physics params via sub_43f8a0), **9 = cop: `car_add_cop(obj)` (0x40c920)**,
6/7 = nothing. So cop positions are hand placed in the .omp (anim "MPOLIZEI_ANIM"): etappe0 17 cops, 1: 10, 2: 7,
3: 3, 4: 9, 5: 12, 6: 17. Cops start parked (ai max speed 0) and only move once they pursue.

#### car_add_cop (0x40c920)  [Car_cops.cpp]
`void car_add_cop(int obj)`
```c
void car_add_cop(int obj) {
    vec3 zero = {0,0,0};
    int side, lane;                                 // lane lives in the arg slot
    OBJ(obj).start_chunk(+0x1c) = OBJ(obj).chunk(+0x18);
    OBJ(obj).side(+0x28) = (g_direction /*0x4d9ac4, [ETAPPE] DIRECTION*/ == 0) ? 1 : 0;
    OBJ(obj).waypoint(+0x20) = sub_443da0(obj, OBJ(obj).side);       // nearest waypoint on that side
    OBJ(obj).handler(+0x44) = handle_cop_ai;                          // 0x402790
    cop_find_waypoint_in_chunk(OBJ(obj).chunk, OBJ(obj).waypoint, &side, &lane);   // 0x402520
    sub_43f8a0(obj, zero, 0, 7500.0f /*0x4851b0*/, 0.3f /*0x3e99999a*/, 4.5f /*0x4851b4*/, 1.7f /*0x4851b8*/); // physics init (mass ...)
    sub_4424b0(obj, 0);
    sub_4110e0(obj);
    sub_411120(obj, 4);            // car list 0x4a64a0: type 4 = cop
    sub_4426c0(obj);
    add_ai(obj, 14 /*behavior cop*/, OBJ(obj).waypoint, lane);        // sub_406800(obj, behavior, wp, lane)
    OBJ(obj).+0x40 = 1;
}
```
`add_ai` (sub_406800) for behavior 14 does: `AI.cop(+0x44) = cop_new(obj); AI.max_speed(+0x18) = 0.0f;
reset_cop(obj, AI.cop);` (0x406b04..0x406b40). add_ai signature: (object, behavior, waypoint, lane).

#### car_cops_init (0x40c8b0) (init)
For every car-list entry {active, obj, type==4}: `COP(AI(OBJ(obj).ai).cop).target = g_player_obj;`
then `g_active_cops[0..19] = -1; g_active_cops_initted (0x4a5d34) = 1`.

### Structures

| struct | off | type | name | evidence |
|---|---|---|---|---|
| t_cop | 0x00 | int | state (0 lookout,1 pursuit,2 got him,3 quitting,4 calling it quits) | 0x402834, 0x402ad4 |
| t_cop | 0x04 | int | target object (always player, set in cop_new / car_cops_init) | 0x402762, 0x40c8f5 |
| t_cop | 0x08 | int | target lane (lane index of target's waypoint) | 0x402c87 |
| t_cop | 0x0c | float | state timer (s); reset 0 on each state change; must be > 5 s to arrest | 0x402ba7, 0x402cd4 |
| t_cop | 0x10 | float | total pursuit time (s), never reset; >= 40 s -> quit | 0x402bc0, 0x402bce |
| t_cop | 0x14 | float | max speed allowed (m/s) from speed trigger | 0x402284, 0x403257 |
| t_cop | 0x18 | float | max lanes (from lane trigger; only printed) | 0x402376 |
| t_cop | 0x1c | int | cop object index | 0x402776, 0x402dd1 |
| T_ARRAY<t_cop> 0x4a5238 | +0 data, +4 count (0x4a523c), +8 last used (0x4a5240), +0xc initted (0x4a5244), +0x10 used flags (0x4a5248), +0x14 name "Cop States"[25] (0x4a524c), +0x2d type "t_cop"[25] (0x4a5265) | | | 0x4020d0 |
| t_ai | 0x00 | int | current waypoint | 0x406de5 |
| t_ai | 0x04 | int | previous waypoint | 0x406ddd |
| t_ai | 0x0c | int/float | reset 0 on waypoint change | 0x406dee |
| t_ai | 0x14 | int | chosen lane | 0x402e7c, 0x402ebf |
| t_ai | 0x18 | float | max speed allowed (target speed of the AI driver) | 0x402b0f, 0x402f88 |
| t_ai | 0x24 | int | behavior (14 cop, 2 = normal traffic when quitting, 0 = stop at arrest) | 0x402aed, 0x402d1d |
| t_ai | 0x2c | int | "blocked/collision wait" flag (skip AI while 1) | 0x402823, 0x40303d |
| t_ai | 0x30 | float | wait timer for +0x2c (0.5 s) | 0x403052 |
| t_ai | 0x44 | int | cop index (t_cop) | 0x4027e1 |
| t_ai | 0x48 | int | ai_old_sample (engine sample to restore after siren) | 0x40c6a8 |
| t_ai_behavior | 0x14 | float | bh_top_speed; BH(3)+0x14 = 0x4a5b88[0]+0x98 used as "quit chase" speed | 0x402b0b, 0x40292b |
| t_object | 0x0c | int | ai index | 0x4027cd |
| t_object | 0x14 | int | current sound sample (engine loop) | 0x40c674 |
| t_object | 0x18 | int | chunk | 0x402a3a |
| t_object | 0x1c | int | start chunk (copy of chunk at creation) | 0x40c951 |
| t_object | 0x20 | int | waypoint | 0x402c32 |
| t_object | 0x28 | int | side (0/1) | 0x4031b5 |
| t_object | 0x2c | int | player: set 1 at arrest when view mode == 3 | 0x41533a |
| t_object | 0x34 | int | cleared when cop FSM gives up (>30 chunks) | 0x40a541 |
| t_object | 0x38 | int | "AI active"/driving flag (set after +0x2c wait, cleared by collision, player autopilot) | 0x403081, 0x425993 |
| t_object | 0x3c | int | collision/blocked flag (sets AI +0x2c) | 0x403099 |
| t_object | 0x40 | int | set 1 for cops | 0x40ca52 |
| t_object | 0x44 | fnptr | per-object handler (HANDLE_COP_AI) | 0x40c99e |
| t_object | 0x6c,0x74,0x7c | float | velocity components; +0x74 also used as "speed" | 0x40fbb6..0x40fc09 |
| chunk record (0x4e65e0, 0x184) | 0xe4/0xe8 | int | lane count side 0 / side 1 | 0x40253e |
| chunk record | 0xec/0xf0 | int[2] | trigger values (speed 4..8, lane 18..21 / 40..43) | 0x40818e |
| chunk record | 0x124 / 0x154 | int[] | waypoint per lane, side 0 / side 1 | 0x402549 |
| chunk record | 0x24 | vec3 | chunk reference position (camera placement) | 0x415284 |
| FSM entry (0x4a55a0, 0x60) | 0x00 obj, 0x04 state, 0x08 target obj, 0x0c, 0x10 float timer, 0x14 float timer, 0x2c float (racer: speed threshold for cop detection), 0x30 int (= [0x4d9e2c] at cop start) | | | 0x403568, 0x401b0b, 0x4090a5 |
| game action slot (0x4a75c8, 0x10, 13 slots) | +0 init fn, +4 handle fn (returns !=0 when done), +8 exit fn, +0xc free flag | | | 0x416760, 0x4168c0 |

### Globals

| addr | type | name | meaning |
|---|---|---|---|
| 0x4a5238 | T_ARRAY<t_cop> | g_cop_states | "Cop States" array (data ptr at +0) |
| 0x4a5ce0 | int[20] | g_active_cops | object indices of cops with siren on (-1 = free); read by HUD 0x41d040 |
| 0x4a5d30 | int | g_cop_siren_active | = (g_siren_count != 0) |
| 0x4a5d34 | int | g_active_cops_initted | |
| 0x4a5d38 | int | g_siren_count | number of sirens on (clamped >= 0) |
| 0x4a5d3c | int | g_siren_light_frame | 0/1 toggled every 200 ms |
| 0x4a5d40 | uint | g_siren_light_tick | GetTickCount of last toggle |
| 0x4a70e8 | int | g_times_caught | arrests this race (0 at start, ++ per arrest event) |
| 0x4a70dc | int | g_cops_saved_view_mode | saved [0x486640] |
| 0x4a70e0 | int | g_cops_saved_camera | saved [0x4a70c8] |
| 0x4a70e4 | uint | g_cops_start_tick | GetTickCount at arrest start |
| 0x4a70d8 | int | g_cops_sequence_mode | always 0 ("Illegal mode for cop sequence") |
| 0x4a7090 | int | g_event_camera | camera index used for event cameras (cams 0x4e66a0 stride 0x160) |
| 0x4866dc | int | g_cop_chase_voice | sound handle of last "achter*" voice (-1 init) |
| 0x4866e0 | int | g_cop_quit_voice | sound handle of last "opgev*" voice (-1 init) |
| 0x486690 | int[13] | g_chase_samples | 0x40..0x4c = achter1..achter19.wav |
| 0x4866c8 | int[5] | g_quit_samples | 0x4d..0x51 = opgev1..opgev9.wav |
| 0x486648 | int[6] | g_arrest1_samples | 0x30..0x35 = aan11a,aan14a,aanh11..aanh14.wav |
| 0x486660 | int[5] | g_arrest2_samples | 0x36..0x3a = aan21a,aan22a,aanh21..aanh23.wav |
| 0x486678 | int[5] | g_arrest3_samples | 0x3b..0x3f = aan31a,aan31b,aan32a,aanh33,aanh34.wav |
| 0x4882c0 | int | g_hud_message | pending HUD message text index (-1 none); shown blinking 2 s by 0x41a780 |
| 0x4867a4 | int | g_game_over_reason | 0 total loss, 1 time up, 2 ARRESTED (text 40/41/42) |
| 0x4d9a7c | int | g_game_action | current game mode (sub_424d20 sets it): 0 driving, 8 cops arrest, 6 game over ... |
| 0x4d9e38 | int | g_money | Game.ini [PLAYER] money, clamped 0..999999 |
| 0x4d9e44 | int | g_player_chunk | |
| 0x4d9e64 | int | g_player_autopilot | 1 while the player car is driven by AI (arrest) |
| 0x48b438 | int | g_player_ai | t_ai index created for the player (-1 until first arrest) |
| 0x4d9e68 | float | g_player_top_speed | car top speed (m/s); speed trigger 8 => cop limit 2*this |
| 0x4d9ab0 | int | g_difficulty | 0 easy, 1 hard |
| 0x4d9ac4 | int | g_direction | [ETAPPE] DIRECTION |
| 0x489631 | int | g_sfx_volume? | sub_420740 returns it; 0 => no voices; used as -attenuation offset |

### Functions (ai_cops.cpp)

#### cop_states_construct (0x4020d0)
Static constructor of `T_ARRAY<t_cop> g_cop_states` ("Cop States", "t_cop"); registers atexit destructor 0x4021a0.
```c
void cop_states_construct(void) {
    g_cop_states.initted = 0;
    strncpy(g_cop_states.name, "Cop States", 25); strncpy(g_cop_states.type, "t_cop", 25);
    /* inlined destroy (no-op at startup): if initted==1 free(data); if initted free(used); initted=0 */
    g_cop_states.data = NULL; g_cop_states.count = 0; g_cop_states.last = 0;
    g_cop_states.used = NULL; g_cop_states.initted = 0;
    atexit(cop_states_destroy /*0x4021a0: if initted==1 free(data); if initted {free(used); initted=0;}*/);
}
```

#### cop_states_alloc (0x4025c0) — T_ARRAY::alloc(n) (called from 0x4241b1 in game init)
```c
void cop_states_alloc(int n) {
    if (initted) { if (initted == 1) free(data); if (initted) { free(used); initted = 0; } }
    if (n < 0) fatal(" %s(%d) : T_ARRAY(construction) illegal array size %s %s[0..%d]\n", "...array.hpp", 0x103, type, name, n);
    data = malloc(n * 0x20);    /* sub_470340 */
    count = n; last = 0;
    used = malloc(n);
    initted = 1;
    for (i = 0; i < n; i++) used[i] = 0;
}
```

#### reset_cop (0x402200)
`void reset_cop(int obj, int cop)`: reads the chunk triggers backwards from the cop's chunk
(`backtrack_speed_trigger` sub_408080(obj, &speed_trig, &lane_trig); defaults speed 8, lane 19/41) and sets the
cop's speed limit and lane count.
```c
void reset_cop(int obj, int cop) {
    int speed_trig, lane_trig;
    backtrack_speed_trigger(obj, &speed_trig, &lane_trig);   // sub_408080
    switch (speed_trig) {
    case 4: COP(cop).max_speed = 16.67f; break;   /* 0x41855c29, 60 km/h */
    case 5: COP(cop).max_speed = 22.22f; break;   /* 0x41b1c28f, 80 km/h */
    case 6: COP(cop).max_speed = 33.33f; break;   /* 0x420551ec, 120 km/h */
    case 8: COP(cop).max_speed = g_player_top_speed /*0x4d9e68*/ + g_player_top_speed; break;  /* no limit */
    default: /* 7 and others: "ILLEGAL SPEED TRIGGER!!!!" */ break;
    }
    if (OBJ(obj).side == 1) {
        switch (lane_trig) { case 18: max_lanes=1.0f; case 19: 2.0f; case 20: 3.0f; case 21: 4.0f;  /* each with break */
                             default: /* "ILLEGAL LANE TRIGGER!!!" */ }
        // stored in COP(cop).max_lanes (+0x18)
    }
    if (OBJ(obj).side == 0) {           /* re-read: side could be 0 after side-1 block only if it was 0 */
        switch (lane_trig) { case 40: 1.0f; case 41: 2.0f; case 42: 3.0f; case 43: 4.0f; default: "ILLEGAL LANE TRIGGER!!!" }
    }
}
```
(Jump table 0x4024e0 is shared: entries 0..4 speed (trig-4), 5..8 side-1 lanes (trig-18), 9..12 side-0 lanes (trig-40).)

#### cop_find_waypoint_in_chunk (0x402520)
`void cop_find_waypoint_in_chunk(int chunk_rec, int wp, int *p_side, int *p_lane)`. NOTE: arg1 is used directly as
a chunk record index (no chunk->record map 0x4e5d2c lookup), callers pass OBJ.chunk.
```c
void cop_find_waypoint_in_chunk(int chunk, int wp, int *p_side, int *p_lane) {
    CHUNK *c = &g_chunks[chunk];                          // 0x4e65e0 + chunk*0x184
    for (i = 0; i < c->lanes[0] /*+0xe4*/; i++)
        if (c->wp_side0[i] /*+0x124*/ == wp) { *p_side = 0; *p_lane = i; return; }
    for (i = 0; i < c->lanes[1] /*+0xe8*/; i++)
        if (c->wp_side1[i] /*+0x154*/ == wp) { *p_side = 1; *p_lane = i; return; }
    fatal("%s(%d): Waypoint %d not found in chunk %d.\n", "C:\\work\\A2racII\\Game\\ai_cops.cpp", 0xa3, wp, chunk);
    // outputs left unchanged
}
```

#### cop_new (0x4026c0) — T_ARRAY::get_new + init (HANDLE_COP_AI itself starts at 0x402790)
`int cop_new(int cop_obj)`; returns cop index.
```c
int cop_new(int cop_obj) {
    if (!initted) fatal(" %s(%d) : T_ARRAY::get_new %s[%d] of type %s not initialized\n", "...array.hpp", 0xb9, name, count, type);
    int i;
    for (i = last; i < count; i++) if (!used[i]) goto found;
    for (i = 0; i < last; i++)     if (!used[i]) goto found;
    fatal(" %s(%d) : T_ARRAY::new array %s[0..%d] of type %s : not enough elements\n", "...array.hpp", 0xce, name, count, type);
    i = -1;  goto init;               /* original writes to data[-1]! */
found:
    used[i] = 1; last = i;
init:
    COP(i).state = 0;
    COP(i).target = g_player_obj;     /* 0x4d9e28 */
    COP(i).cop_obj = cop_obj;
    return i;
}
```

#### handle_cop_ai (0x402790) — "HANDLE_COP_AI" (object callback, OBJ+0x44)
`int handle_cop_ai(int obj)`.
```c
int handle_cop_ai(int obj) {
    if (g_ai_model /*0x4d9b10*/ != 1) {            /* new FSM model */
        cop_fsm_lookout(obj);                       /* 0x409060 */
        if (!fsm_find_object(obj))                  /* 0x403630: not in FSM table */
            ai_brake_hard(obj);                     /* 0x40fba0: v(+0x6c,+0x74,+0x7c) -= v*10*dt */
        return 0;
    }
    int ret = 1;
    int ai  = OBJ(obj).ai;
    int cop = AI(ai).cop;
    ai_frame_update(obj);                           /* sub_4084b0 ("FRAME: %06d") */
    if (AI(ai).blocked /*+0x2c*/ == 0) {
        switch (COP(cop).state) {
        case 0: /* "On the lookout" */
            ret = cop_lookout(obj, cop);            /* 0x402a10 */
            break;
        case 1: /* "In pursuit of suspect" */
            ret = cop_pursuit(obj, cop);            /* 0x402b40 */
            break;
        case 2: /* "Got him!" */
            AI(ai).max_speed = OBJ(COP(cop).target).speed(+0x74) * 0.5f;
            if (g_game_action /*0x4d9a7c*/ != 8) {  /* arrest sequence finished */
                player_autopilot_off();             /* 0x425980: OBJ(player).+0x38 = 0; g_player_autopilot = 0 */
                COP(cop).state = 3;
                AI(ai).behavior = 2;
                AI(ai).max_speed = BH(3).top_speed;    /* [0x4a5b88]+0x98 */
                COP(cop).timer = 0.0f;
            }
            change_lane(obj, 2, -1);                /* sub_403660 */
            ret = handle_basic_ai(obj);             /* sub_4076b0 */
            break;
        case 3: /* "Quiting chase!" */
            change_lane(obj, 4, -1);
            ret = handle_basic_ai(obj);
            break;
        case 4: /* "Calling it quits for today." (never set anywhere) */
            ret = handle_basic_ai(obj);
            break;
        default:
            fatal("%s(%d): Cop state %d corrupted!\n", "...ai_cops.cpp", 0xf1, cop /* prints index, not state */);
        }
    }
    cop_siren_lights_update();                      /* 0x40c860 */
    return ret;
}
```

#### cop_lookout (0x402a10) (undetected function start)
`int cop_lookout(int obj, int cop)` — always returns 1.
```c
int cop_lookout(int obj, int cop) {
    int target = COP(cop).target;
    int range = OBJ(target).chunk - OBJ(obj).chunk;     /* no wrap */
    if (abs(range) < 2) {
        if (cop_check_target(obj, cop, target) == 1) {  /* 0x403130 */
            /* "Starting pursuit..." */
            int ai = OBJ(obj).ai;
            COP(cop).state = 1;
            AI(ai).behavior = 14;
            AI(ai).max_speed = BH(AI(ai).behavior).top_speed;   /* +0x14 */
            cop_siren(obj, 1);                                  /* 0x40c640 */
            COP(cop).timer = 0.0f;
        }
    }
    return 1;
}
```

#### cop_pursuit (0x402b40)
`int cop_pursuit(int obj, int cop)`. Accumulates timers, quits after 40 s total, positions behind/next to target,
arrests when exactly one chunk ahead of the target in the same lane for > 5 s.
```c
int cop_pursuit(int obj, int cop) {
    int target = COP(cop).target;
    int ai = OBJ(obj).ai;
    COP(cop).timer       += dt();
    COP(cop).chase_time  += dt();
    if (!(COP(cop).chase_time < 40.0f /*0x47f0cc*/)) {           /* give up */
        COP(cop).state = 3;
        AI(ai).behavior = 2;
        AI(ai).max_speed = BH(3).top_speed;
        COP(cop).timer = 0.0f;
        cop_siren(obj, 0);
    }
    else if (OBJ(obj).chunk > OBJ(target).chunk) {               /* "Ahead of target!" */
        int t_side, t_lane, my_side, my_lane;
        cop_find_waypoint_in_chunk(OBJ(target).chunk, OBJ(target).waypoint, &t_side, &t_lane);
        cop_find_waypoint_in_chunk(OBJ(obj).chunk,    OBJ(obj).waypoint,    &my_side, &my_lane);
        COP(cop).target_lane = t_lane;
        if (t_side == my_side) {                                 /* "Sides match..." */
            if (t_lane == my_lane) {
                if (OBJ(obj).chunk - OBJ(target).chunk == 1
                    && COP(cop).timer > 5.0f /*0x47f0d0*/
                    && g_game_action == 0) {
                    /* ARREST */
                    COP(cop).state = 2;
                    COP(cop).timer = 0.0f;
                    AI(ai).behavior = 0;
                    OBJ(obj).speed(+0x74) = OBJ(COP(cop).target).speed;
                    AI(ai).max_speed = 0.0f;
                    cop_siren(obj, 0);
                    game_action_start(8);                        /* sub_424d20(8) -> ev_cops */
                    /* every other cop chasing the same target quits */
                    for (int i = 0; ; i++) {
                        int last_used = -1;
                        for (int k = 0; k < g_cop_states.count; k++) if (g_cop_states.used[k] == 1) last_used = k;
                        if (!(i < last_used)) break;             /* NB: highest used index itself is skipped */
                        if (i != cop && COP(i).state == 1 && COP(i).target == COP(cop).target) {
                            COP(i).state = 3;
                            int ai2 = OBJ(COP(i).cop_obj).ai;
                            AI(ai2).behavior = 2;
                            AI(ai2).max_speed = BH(3).top_speed;
                            COP(i).timer = 0.0f;
                            cop_siren(COP(i).cop_obj, 0);
                        }
                    }
                    player_autopilot_on();                       /* 0x4258f0, see below */
                }
            } else {
                /* "Target left..." if t_lane > AI.lane, else "Target right..." -- both branches identical: */
                if (ai_step_to_side_waypoint(ai) == 1)           /* sub_406d90 */
                    AI(ai).lane(+0x14)--;
                AI(ai).max_speed = OBJ(target).speed * 0.5f;     /* 0x47f0c8 */
            }
        } else {                                                 /* "Match speed of target." */
            AI(ai).max_speed = OBJ(target).speed * 0.8f;         /* 0x47f0d4 = 0x3f4ccccd */
        }
    }
    else if (OBJ(target).chunk - OBJ(obj).chunk > 5) {           /* "Speeding..." (far behind) */
        AI(ai).max_speed = BH(AI(ai).behavior).top_speed;
    }
    else {                                                       /* "Approaching..." */
        AI(ai).max_speed = OBJ(target).speed * 1.25f;            /* 0x47f0d8 */
        if (AI(ai).max_speed < 15.0f /*0x47f0dc*/) AI(ai).max_speed = 15.0f;
    }

    /* "Handling AI." */
    if (g_4d9a9c == 0 || g_4d9a78 == 0) return 1;                /* race not running */
    if (AI(ai).blocked == 1) {
        AI(ai).blocked_time += dt();
        if (AI(ai).blocked_time > 0.5f) { AI(ai).blocked = 0; AI(ai).blocked_time = 0; OBJ(obj).+0x38 = 1; }
    }
    if (OBJ(obj).+0x3c == 1) { AI(ai).blocked = 1; AI(ai).blocked_time = 0; OBJ(obj).+0x38 = 0; }
    if (AI(ai).blocked == 0) {
        change_lane(obj, 2, -1);          /* sub_403660 */
        handle_basic_ai(obj);             /* sub_4076b0 */
    }
    sndfx_play_object_sample(obj, 1);     /* sub_428bc0 */
    return 1;
}
```
`ai_step_to_side_waypoint` (sub_406d90, ai_lane): `n = WP(AI.wp).link[+0x20]; s = WP(n).link[+0x1c]; if n!=-1 &&
s!=-1 && s!=AI.wp { if (AI.prev==AI.wp) AI.prev = AI.wp; AI.wp = s; AI.+0xc = 0; return 1 } return 0` — so the cop
always shifts towards the same neighbour lane regardless of "left"/"right" (original bug).

`player_autopilot_on` (0x4258f0): `if (g_player_ai == -1) { add_ai(player, 0, OBJ(player).waypoint, 0);
g_player_ai = OBJ(player).ai; } AI(OBJ(player).ai).wp = OBJ(player).waypoint; OBJ(player).+0x38 = 0;
g_player_autopilot = 1;` (behavior 0 has max_speed 0 -> the player's car is brought to a stop by the AI).

#### cop_check_target (0x403130)
`int cop_check_target(int obj, int cop, int target)` -> 1 = violation.
```c
int cop_check_target(int obj, int cop, int target) {
    int side, lane;
    cop_find_waypoint_in_chunk(OBJ(target).chunk, OBJ(target).waypoint, &side, &lane);
    if (OBJ(obj).side != side) return 1;          /* "Ghost!" (target on the other carriageway = wrong-way driver) */
    /* "-> Side OK."  lane check is a no-op ("Cop max lanes", "-> Lane OK.") */
    if (COP(cop).max_speed * 1.1f /*0x47f0e0*/ < OBJ(target).speed(+0x74))
        return 1;                                  /* "Speeding!" */
    return 0;                                      /* "No violation." */
}
```

#### fsm_find_object (0x403630)
`int fsm_find_object(int obj)`: `for (i=0;i<15;i++) if (FSM(i).obj == obj) break; return i != 15;`

#### fsm_add_cop (0x4034a0) [ai_fsm.cpp]
`int fsm_add_cop(int cop_obj, int target)` — new-model pursuit start.
```c
int fsm_add_cop(int cop_obj, int target) {
    int max_cops = (target == g_player_obj) ? (g_difficulty ? 3 : 1) : 2;
    if (g_maps[g_track].is_city /*0x4d8e9c + track*0x89*/ == 1) {
        float p = dt() * 1000.0f;                       /* 0x47f0e4 */
        if (!((float)(rand() % 1000) <= p)) return 0;   /* city: random reaction, ~dt per second */
    }
    int i;
    for (i = 3; i < 15 && FSM(i).obj != -1; i++) ;
    if (FSM(i).obj != -1) return 0;                     /* i==15 reads 0x4a5b40 (t_ai data ptr) */
    int n = 0;
    for (k = 3; k < 15; k++) if (FSM(k).target == target) n++;
    if (n >= max_cops) return 0;
    FSM(i).obj = cop_obj; FSM(i).state = 3; FSM(i).target = target; FSM(i).+0x30 = g_4d9e2c;
    cop_voice_chase(target);                            /* 0x4150f0 */
    cop_siren(cop_obj, 1);
    return 1;
}
```

#### fsm_remove_cop (0x4035b0) [ai_fsm.cpp]
`void fsm_remove_cop(int fsm_idx)`
```c
void fsm_remove_cop(int idx) {
    if (idx < 3) { fatal("Illegal cop, this is a racer: %s %d", "...ai_fsm.cpp", 0x18c); return; }
    for (i = 0; i < 20; i++) if (g_active_cops[i] == FSM(idx).obj) { g_active_cops[i] = -1; break; }
    FSM(idx).obj = FSM(idx).state = FSM(idx).target = -1;
    FSM(idx).+0x10 = 0; FSM(idx).+0x14 = 0; FSM(idx).+0x0c = 0;
}
```

#### cop_fsm_lookout (0x409060) (new model, called from handle_cop_ai)
```c
void cop_fsm_lookout(int obj) {
    int chunk = OBJ(obj).chunk;
    for (k = 3; k < 15; k++) if (FSM(k).obj == obj) return;          /* already chasing */
    for (i = 0; i < 3; i++) {                                          /* racers */
        int r = FSM(i).obj;
        if (OBJ(r).chunk == chunk && FSM(i).+0x2c <= OBJ(r).speed) { fsm_add_cop(obj, r); return; }
    }
    if (chunk != g_player_chunk /*0x4d9e44*/) return;
    int speed_trig, lane_trig;
    backtrack_speed_trigger(obj, &speed_trig, &lane_trig);
    float v = OBJ(g_player_obj).speed;
    switch (speed_trig) {
    case 4: if (v > 12.5f) fsm_add_cop(obj, g_player_obj); break;    /* 45 km/h */
    case 5: if (v > 17.5f) fsm_add_cop(obj, g_player_obj); break;    /* 63 km/h */
    case 6: if (v > 25.0f) fsm_add_cop(obj, g_player_obj); break;    /* 90 km/h */
    case 8: if (v > 30.0f) fsm_add_cop(obj, g_player_obj); break;    /* 108 km/h */
    default: break;                                                   /* 0, 7, other */
    }
}
```

#### fsm_cop_arrest (0x40a390) [ai_path.cpp] (FSM handler of the arrest state, for reference)
```c
void fsm_cop_arrest(int idx) {
    FSM(idx).+0x10 = 0;
    cop_siren(FSM(idx).obj, 0);
    if (FSM(idx).target == g_player_obj) {
        if (g_game_action == 0) game_action_start(8);
    } else {
        r = index of racer entry (0..2) with obj == target, else fatal("An illegal racer was arrested in %s %d", ai_path.cpp, 0x441);
        FSM(r).state = 7;
    }
    for (k = 3; k < 15; k++) if (FSM(k).target == FSM(idx).target) FSM(k).state = 6;
    sub_40ee80(FSM(idx).obj); sub_40ece0(FSM(idx).obj); sub_40ebd0(FSM(idx).obj);
}
```
`fsm_cop_return` (0x40a4f0): if `rceng_chunk_distance(OBJ(cop).chunk, OBJ(player).chunk) > 30` { OBJ(cop).+0x34 = 0;
fsm_remove_cop(idx); } else { drive (sub_40ee80, sub_40ebd0, sub_40ece0); v(+0x74,+0x6c,+0x7c) -= v*3*dt }.

### Functions (Car_cops.cpp)

#### cop_siren (0x40c640)
`void cop_siren(int obj, int on)`. Swaps the object's sound sample to SNDFX_POLITIE_SIRENE (40 = psirene1.wav)
and back to `ai_old_sample`, counts sirens, maintains g_active_cops.
```c
void cop_siren(int obj, int on) {
    int ai = OBJ(obj).ai;
    if (on) {
        if (OBJ(obj).sample(+0x14) != 40) {
            AI(ai).old_sample(+0x48) = OBJ(obj).sample;
            sndfx_set_object_sample(obj, 40);       /* sub_4289e0 */
        }   /* else "SIRENE already on!" */
        g_siren_count++;                            /* incremented in both cases */
    } else {
        if (OBJ(obj).sample == 40) sndfx_set_object_sample(obj, AI(ai).old_sample);
        if (--g_siren_count < 0) g_siren_count = 0;
        if (g_siren_count == 0) txt_blit_anim(14, 0);   /* sub_42ae90: siren-light texture copy #14 frame 0 */
    }
    if (!g_active_cops_initted) fatal("\n%s (%d) Array of active cops not initted yet!", "...Car_cops.cpp", 0x93);
    if (on) {
        /* (re-asserts initted, lines 0x72/0x52) */
        if (find(g_active_cops, obj) == -1) { i = find(g_active_cops, -1); if (i != -1) g_active_cops[i] = obj; }
    } else {
        /* searches g_active_cops for obj but does NOT clear it (removal only in fsm_remove_cop) */
    }
    g_cop_siren_active = (g_siren_count != 0);
}
```
Callers: cop_lookout/pursuit, fsm_add_cop, FSM conditions in ai_cfun.cpp (0x401b4b, 0x401cca, 0x401e79),
fsm_cop_arrest, ai_check_cars_outside_handling_range (0x40b430: siren off for a type-4 car leaving the window while
COP.state == 1), sub_408ed0, 0x432bd0/0x432d80.

#### cop_siren_lights_update (0x40c860)
Called once per frame (handle_cop_ai, and sub_403340 FSM loop when a cop FSM slot is active, sub_432db0).
```c
void cop_siren_lights_update(void) {
    if (!g_cop_siren_active) return;
    DWORD now = GetTickCount();
    if ((DWORD)(GetTickCount() - g_siren_light_tick) >= 200) {
        g_siren_light_tick = now;
        txt_blit_anim(14, g_siren_light_frame);   /* sub_42ae90 */
        g_siren_light_frame = (g_siren_light_frame == 0);
    }
}
```

### Functions (ev_cops.cpp)

#### cop_voice_chase (0x4150f0) / cop_voice_give_up (0x415170)
`void cop_voice_chase(int target)` — "achter*" voice when a cop starts chasing (from fsm_add_cop).
`cop_voice_give_up` is identical with handle 0x4866e0 and table 0x4866c8 (5 "opgev*" samples); called by the FSM
give-up condition 0x401e20 (chase time > 30 s easy / 60 s hard, or > 50 chunks away).
```c
void cop_voice_chase(int target) {
    int vol = sub_420740();                       /* [0x489631], 0 => sound off */
    if (!vol) return;
    if (target != g_player_obj && rand() % 3 != 0) return;
    if (g_cop_chase_voice != -1 && snd_is_playing(g_cop_chase_voice) /*sub_467050*/) return;
    g_cop_chase_voice = sndfx_play_sample(g_chase_samples[rand() % 13]);   /* sub_4286a0 */
    snd_set_volume(g_cop_chase_voice, -500 - vol);                         /* sub_467950 */
}
```
(The disassembler string annotation 'Illegal value for g_times_caught' on 0x4866dc/0x4866e0 is a false string hit.)

#### cops_event_camera (0x4151f0)
Places the event camera (index g_event_camera 0x4a7090) above the next chunk looking at the player.
```c
void cops_event_camera(void) {
    CAMERA *cam = &g_cameras[g_event_camera];          /* 0x4e66a0 + i*0x160 */
    vec3 off = {0, 20.0f, 0};
    cam_set_target(cam, off, OBJ(g_player_obj).obj3d);              /* sub_4477f0 (thiscall) */
    CHUNK *c = &g_chunks[g_chunk_map[OBJ(g_player_obj).chunk + 1]]; /* 0x4e5d2c */
    vec3 pos = c->ref_pos(+0x24); pos.y -= -20.0f;                   /* 0x47f534 */
    if (cam->+0x70) sub_4477c0(cam);                                 /* detach */
    cam_set_position(cam, pos);                                      /* sub_4474f0 */
    cam_update(cam);                                                 /* sub_447ab0 */
    camera_set_mode(6);                                              /* sub_4142b0 */
}
```

#### Arrest sequence = game action 8 (registered by game_action_start, sub_424d20)
`game_action_start(8)`: `g_game_action = 8; g_4d9aa0 = 1; game_action_register(11, cops_init 0x415300,
cops_handle 0x415420, cops_exit 0x415490)`. Game actions (ev_main.cpp, sub_416760 / runner sub_4168c0, 13 slots at
0x4a75c8): register calls init immediately if the slot is free and marks it busy; every frame the runner calls
handle; when handle returns != 0 it calls exit and frees the slot. Who sets action 8: cop_pursuit (old model) and
fsm_cop_arrest (new model), both only when g_game_action == 0.

##### cops_init (0x415300)
```c
void cops_init(void) {
    g_times_caught++;
    g_cops_saved_view_mode = g_view_mode;      /* 0x486640 */
    g_cops_saved_camera    = g_camera_index;   /* 0x4a70c8 */
    if (g_view_mode == 3) OBJ(g_player_obj).+0x2c = 1;
    cops_event_camera();
    if (g_times_caught == 1) g_hud_message = 49;      /* "EERSTE WAARSCHUWING" (first warning) */
    else if (g_times_caught == 2) g_hud_message = 50; /* "LAATSTE WAARSCHUWING" (last warning) */
    g_cops_sequence_mode = 0;
    int vol = sub_420740();
    if (vol) {
        int h;
        switch (g_times_caught) {
        case 1: h = sndfx_play_sample(g_arrest1_samples[rand() % 6]); break;
        case 2: h = sndfx_play_sample(g_arrest2_samples[rand() % 5]); break;
        case 3: h = sndfx_play_sample(g_arrest3_samples[rand() % 5]); break;
        default: fatal("%s (%d) Illegal value for g_times_caught: %d", "...ev_cops.cpp", 0xf1, g_times_caught);
                 h = /* uninitialised stack value */; break;
        }
        snd_set_volume(h, -100 - vol);
    }
    g_cops_start_tick = GetTickCount();
}
```

##### cops_handle (0x415420)
```c
int cops_handle(void) {
    if (g_game_action != 8) return 1;
    if (g_times_caught >= 3) {
        g_game_over_reason = 2;          /* 0x4867a4: game over text 42 "GEARRESTEERD" + 43 "RACE OVER" */
        game_action_start(6);            /* game over (ev_g_ovr.cpp, slot 12: 0x416060/0x4162e0/0x416440) */
        return 1;
    }
    if (g_cops_sequence_mode != 0) {
        fatal("%s (%d) Illegal mode for cop sequence! Mode was:%d", "...ev_cops.cpp", 0x17b, g_cops_sequence_mode);
        return 0;
    }
    return (GetTickCount() - g_cops_start_tick) >= 4000;     /* 4 s stop */
}
```

##### cops_exit (0x415490)
```c
void cops_exit(void) {
    if (g_game_action == 8) {                     /* not after game over */
        camera_set_mode(g_cops_saved_camera);     /* sub_4142b0 */
        sub_414b70(g_cops_saved_view_mode);       /* restore view mode */
        game_action_start(0);                     /* back to driving: g_4d9a9c = g_4d9e58 = 1 */
    }
}
```
After the action ends, the old-model cop in state 2 sees g_game_action != 8, calls player_autopilot_off
(0x425980) and goes to state 3 (drives away as traffic).

#### 0x415990
A bare `ret` (no-op callback shared by many tables, xrefs 0x421b0e, 0x422d6d, 0x429ab0, data 0x48c494 ...). Not
cop-specific.

#### 0x4159a0 (unreferenced, dead code in ev_cops.cpp area)
Every 200 ms (GetTickCount vs 0x4a75c4) spawns an effect (sub_41fff0) at a random point relative to the player:
dx = rand()%6 - 3 + 16, dz = rand()%18 - 9, rotated by the player's object3d heading (+0x78, sin/cos tables
0x4ea418/0x4ea4b4, masked by 0x49b018) and added to the player position (0x4d9e48/4c/50). No references found.

#### game_over_camera (0x415b20) (called from game-over init 0x416060; ev_g_ovr side)
Copies the current camera (g_cameras[g_camera_index]) position/target (sub_447720 / sub_447820, or 0 vectors if
+0x70/+0x74 unset) into g_cameras[g_event_camera] attached to the player's object3d (sub_4476f0, sub_4477f0),
updates it (sub_447ab0) and selects camera mode 6.

### Fines / money / arrest rules

* **There are no fines in A2 Racer II.** language.ini texts 1/7/9/11 "BOETE: FL %d", 5 "KOSTEN", 20..23 "BUDGET /
  PRIJZENGELD / BOETES / NIEUW BUDGET" are never referenced by spel.dat (all text lookups go through sub_431d30 with
  constant indices 0x11,0x18..0x1c,0x20..0x2b,0x34..0x37 or the HUD message index g_hud_message which is only set to
  0x1f,0x2c..0x33); a2_II.exe/menu.dat contain no fine logic either. Leftovers from A2 Racer I.
* Money (0x4d9e38, Game.ini [PLAYER] money) is only changed by `money_add` (0x428180, clamp 0..999999), called only
  by the finish action exit 0x415690: `money_add(prize(position)); money_add(g_4dc964 * 2000)` with prize table
  55000/35000/15000/0 (x4 on track 6; 4th place gets 5000 when difficulty==0) (sub_4280c0). Written back by
  write_player_info (0x422ec0). An arrest costs nothing but time.
* Arrest rules: 1st arrest -> 4 s stop, HUD "EERSTE WAARSCHUWING"; 2nd -> "LAATSTE WAARSCHUWING"; 3rd -> game over,
  reason 2 "GEARRESTEERD". g_times_caught is a BSS int (0 at process start, spel.dat runs one race per launch), never
  reset otherwise.
* Old model: a cop on the lookout reacts when the target is within 1 chunk (|dchunk| < 2) and either on the other
  carriageway ("Ghost") or faster than 1.1 * cop max speed (speed trigger: 16.67/22.22/33.33 m/s or 2*top speed). It
  chases with siren, quits after 40 s total; arrests when it is exactly 1 chunk ahead, same side and lane, state
  timer > 5 s, and g_game_action == 0. All other cops chasing the same target then quit.
* New model (default): a cop in the player's chunk reacts if player speed > 12.5/17.5/25/30 m/s for speed trigger
  4/5/6/8; max simultaneous cops per player: 1 (easy) / 3 (hard), 2 per AI racer; in "city" tracks only with
  probability ~ dt per frame. Arrest happens in the FSM (condition 0x4019f0: target slow (|vx|+|vz| < 7) and within
  sqrt(5000)~70.7 units in x/z; not during game action 3 / racer state 9).

### Cop FSM transition table 0x4a5280
See section 6 "Transition tables" (authoritative: 20 entries at indices 0..11 and 13..20, index 12 never set).

### Open questions
* 0x489631 (sub_420740): unaligned global used as sound volume/enable; check sound init.
* t_object +0x38 / +0x3c / +0x2c semantics belong to the object/physics area (named here by usage).
* FSM entry +0x2c (racer), compared against the racer speed in cop_fsm_lookout, is the racer speed limit from the
  speed triggers (section 6). [0x4d9e2c] (player car model) is stored in FSM +0x30 at cop start.
* sub_470340 = malloc (assumed; T_ARRAY data not explicitly cleared, so t_cop +0x08..+0x10 start as heap contents
  unless malloc zeroes - pursuit timer +0x10 is never reset, so in the old model a cop can chase only 40 s total
  per race, and state 3 never returns to 0).
* ai_frame_update (sub_4084b0), change_lane (sub_403660), handle_basic_ai (sub_4076b0) are documented in
  sections 4 and 5.


## 9. Car creation, traffic, racers, ADAC (Car_main.cpp, ai_racer.cpp / ai_wmap.cpp, Car_cops.cpp)


#### Car types (0x484b18, 16 x 0xee bytes, car_main.cpp)
`char name[14]` at +0, the loaded resource handle at +0x0e (filled by `sub_410b60` via `sub_42b310(name)`), physics
parameters follow (floats at +0x16.. are read by add_car: +0x16 = mass-like value, +0x2e/+0x32/+0x36 passed to the
object creator; documented by the car-physics side).

| type | name | used by |
|---|---|---|
| 0..6 | RACER_1_ANIM .. RACER_7_ANIM | player car (Game.ini [PLAYER] car) and the 3 CPU racers |
| 7 | MPOLIZEI_ANIM | cops |
| 8 | ADAC_ANIM | the ADAC/Wegenwacht car |
| 9 | N_GOLF_ANIM | traffic |
| 10 | N_ASTRA_ANIM | traffic |
| 11 | N_BUS_ANIM | traffic (bus) |
| 12 | N_VRTAUT_ANIM | traffic (vrachtauto = lorry) |
| 13 | N_POLO_ANIM | traffic |
| 14 | N_BESTEL_ANIM | traffic (bestelbus = van) |
| 15 | N_CALIBR_ANIM | traffic (Calibra) |

Traffic model table `g_traffic_types` (0x484a48, 12 x {int car_type; int behavior}):
`{9,2} {9,3} {10,2} {10,3} {13,2} {13,3} {11,2} {12,2} {14,2} {14,3} {15,2} {15,3}`. The intended pairing is not
used: init_traffic draws the car type and the behaviour with two independent `rand() % 12` calls, so
P(type) = Golf, Astra, Polo, Van, Calibra 2/12 each, bus and lorry 1/12 each, and independently
P(behaviour 2) = 7/12, P(behaviour 3) = 5/12 (a bus can get behaviour 3).

#### Car list (0x4a64a0)
`struct t_car_list_entry { int active; int object; int type; }` - entries from 0x4a64a0 to 0x4a7058 (250 entries).
Types (add_car's last argument): 1 = player, 2 = traffic, 3 = racer, 4 = cop, 5 = ADAC. Type 1, 3 and 4 also set
`object+0x04 = 1` (object takes part in object collision / "important car" flag).
`car_list_clear` (0x411100) zeroes it; `car_list_add` (0x411120, "get_free_car_list_entry") is the stand-alone version
of the insertion done inline at the end of add_car.

#### add_car (0x410bc0)
```c
int add_car(int car_type, int chunk, int lane, int side, int dir,
            int (*handler)(int obj), int (*collision_cb)(int obj), int list_type)
{
    if (car_type < 0 || car_type > 16) fatal("(add_car) tryimg to add an invalid car type (%d) max. (%d)\n", car_type, 16);
    chunk = chunk_wrap(chunk);                                   // sub_4426e0
    vec3 pos = *rceng_get_waypoint_position(&tmp, chunk, lane, side); // sub_4420f0: waypoint(chunk,lane,side)+0x08
    int obj = create_object(pos, car_types[car_type].resource /*+0x0e*/, 0, handler, collision_cb, 0,
                            car_types[car_type].f_0xea ...);      // sub_43f770 (engine)
    object_set_physics(obj, ..., car_types[car_type].f2e, f32, f36, 0.2f, ...);  // sub_43f8a0 (engine)
    rceng_orient_object_on_road(obj, chunk, dir);                 // sub_442230
    objects[obj].prev_chunk(+0x1c) = objects[obj].chunk(+0x18) = chunk;
    objects[obj].side(+0x28) = side;
    if (list_type == 1 || list_type == 3 || list_type == 4) objects[obj].f04 = 1;
    objects[obj].waypoint(+0x20) = rceng_get_waypoint_index(chunk, lane, side);   // sub_442150
    rceng_add_object_in_waypoint_obstacle_list(obj, 0);           // sub_4424b0
    objects[obj].f24 = g_4d9a90;
    car_list_add(obj, list_type);                                 // inline copy of 0x411120
    sub_4426c0(obj);  object3d_update(&object3d[objects[obj].obj3d]);  // sub_459360
    return obj;
}
```
`dir` = 1 when the car drives in the direction of increasing chunk numbers, 0 for oncoming. NOTE: the traffic
initialiser passes `side` as `dir` (correct only for DIRECTION = 0, see below).

#### init_all_cars (0x410b60)
```c
void init_all_cars(void) {
    for (k = 0; k < 16; k++) car_types[k].resource = load_car_resource(car_types[k].name);  // sub_42b310
    init_player_car();        // sub_4256a0 (plr_car.cpp)
    init_traffic();           // sub_40ca70
    init_racer_cars();        // sub_411170
    fsm_init();               // sub_4093b0 (racers + cops FSM, see FSM section)
    init_cop_targets();       // sub_40c8b0
    init_adac_car();          // sub_40c4f0
}
```
The player (sub_4256a0): `add_car(player_car, STARTCHUNK - position, lane 4, side = (DIRECTION==0), dir 1,
0x425e20, 0x427c70, type 1)`; `g_player_object` (0x4d9e28) = result. So with the default DIRECTION = 0 the player
drives on **side 1**, in increasing chunk order, starting in lane 4 (clamped to the chunk's lane count - 1 by
rceng_get_waypoint_index).

#### init_traffic (0x40ca70)
Counts are set in `game_init` (sub_423d20, after ini_load_all):
`g_traffic_same_side` (0x484aa8) and `g_traffic_oncoming` (0x484aac) = 10 / 14 when Difficulty (0x4d9ab0) != 0
(HARD), 5 / 7 when EASY. (Static .data defaults 8 / 12 are always overwritten.)
```c
void init_traffic(void)
{
    int side_a = (g_direction == 0) ? 1 : 0;      // player's side
    int side_b = (g_direction == 0) ? 0 : 1;      // oncoming side
    int n;
    // same-direction traffic: n/2 cars behind the visible window, n/2 cars ahead of it
    n = g_traffic_same_side / 2;
    for (i = 0, d = 0; i < n; i++, d += 3) {
        chunk = chunk_wrap(g_startchunk - g_visible_chunks - d - 5);
        t = &g_traffic_types[rand() % 12];  beh = g_traffic_types[rand() % 12].behavior;  // two independent rand()s:
        // car_type from the first, behavior from the second draw
        obj = add_car(t->car_type, chunk, 0, side_a, side_a, HANDLE_TRAFFIC_AI /*0x40b770*/,
                      mesh_damage_collision_func /*0x427ad0*/, 2);
        add_ai(obj, beh, rceng_get_waypoint_index(chunk, 0, side_a), 0 /*lane*/);   // sub_406800
        objects[obj].on(+0x34) = 0;
    }
    for (i = 0, d = 0; i < n; i++, d += 3) {        // ahead
        chunk = chunk_wrap(g_startchunk + g_visible_chunks + d + 5);
        ... same with side_a ...
    }
    // oncoming traffic: same pattern with g_traffic_oncoming / 2 per group and side_b
    n = g_traffic_oncoming / 2;
    for (behind) chunk = chunk_wrap(g_startchunk - g_visible_chunks - d - 5);   add_car(..., 0, side_b, side_b, ...)
    for (ahead)  chunk = chunk_wrap(g_startchunk + g_visible_chunks + d + 5);
}
```
So with HARD: 5 + 5 same-direction cars and 7 + 7 oncoming cars (24 traffic cars); EASY: 2 + 2 and 3 + 3 (10 cars).
All traffic starts in lane 0, spaced 3 chunks apart, starting 5 chunks outside the visible range
(`g_visible_chunks` 0x4d9a8c = Game.ini visiblechunks, clamped to 10..50). `on = 0` switches them off:
rceng_handle_objects (0x440850) skips objects with +0x34 == 0, and they are ignored by the respawn overlap
check. Because they start outside the handling window, ai_check_cars_outside_handling_range immediately
starts trying to respawn them at the window edges (1/30 chance per car per frame), which switches them on. So
the traffic "streams in" at the edges of the window during the first seconds of a race.

#### init_racer_cars (0x411170)
```c
void init_racer_cars(void)
{
    int side = (g_direction == 0);
    int used_pos[4] = {0};  used_pos[g_player_position /*0x4d9e60, Game.ini position*/] = 1;
    for (r = 0; r < 3; r++)                                     // grid slot: first free position
        for (p = 0; p < 4; p++) if (!used_pos[p]) { racers[r].position(+8) = p; used_pos[p] = 1; break; }
    timer_init(8, 0x0fffffff, 0); timer_init(9, ...); timer_init(10, ...);   // sub_421770: racer finish clocks
    memset(g_car_model_used /*0x4a7058*/, 0, 7*4);  g_car_model_used[g_player_car /*0x4d9e2c*/] = 1;
    for (r = 0; r < 3; r++) {                                   // car model: nearest model not yet used
        if (r & 1) {    // search downwards from the player's car, then upwards
            for (m = g_player_car; m >= 0; m--) if (!used[m]) { racers[r].car = m; used[m] = 1; goto done; }
            for (m = g_player_car; m < 7; m++)  if (!used[m]) { racers[r].car = m; used[m] = 1; break; }
        } else {        // r even: upwards first (sub_411410), then downwards (sub_411460)
            if (!pick_model_up(r)) pick_model_down(r);
        }
    }
    for (r = 0; r < 3; r++) {
        chunk = chunk_wrap(g_startchunk - racers[r].position);      // one chunk per grid position
        lane  = rceng_get_valid_lane(chunk, side, 4);                // min(4, lanes-1)
        static const int beh_of_car[7] = { 7, 9, 8, 10, 11, 12, 13 };   // jump table 0x4113f0, default 10
        int beh = beh_of_car[racers[r].car];
        obj = add_car(racers[r].car, chunk, lane, side, 1, HANDLE_RACER_AI /*0x40ac90*/, 0x427ad0, 3);
        add_ai(obj, beh, rceng_get_waypoint_index(chunk, lane, side), lane);
        objects[obj].f38 = 0;
        sub_4136c0(racers[r].car, obj);                            // camera/name binding (not AI)
        objects[obj].f40 = 1;
        racers[r].object(+0x0c) = obj;  racers[r].finished(+0x10) = 0;  racers[r].f14 = 0;
        fsm[r].object(+0x00) = obj;     fsm[r].car(+0x30) = racers[r].car;
    }
}
```
`racers` = 0x485b20, 3 x 0x58: +0x00 car model, +0x04 finish time, +0x08 position (grid, later race position),
+0x0c object, +0x10 finished, +0x14 ?. Game.ini [RACERS] `n=car,position,time` is *written* after the race
(sub_422d40) from this table.

`racer_finished` (0x411650, obj): for the racer whose object == obj: `racers[r].time = timer_get(8+r)`
(sub_4214d0), `timer_stop(8+r)` (sub_421300), `racers[r].finished = 1`.

`racers_finish_estimate` (0x4114a0, called at race end, sub_422d40 / sub_42a420): for every racer not finished, take its
finish time now and extrapolate it over the remaining distance:
```c
    total = g_chunk_count * g_laps (0x4d9ad8);  done = g_player_progress (0x4d9e70)... 
    racers[r].time += racers[r].time / (total - (g_player_progress - ai[racer].progress(+0x60))) *
                      (g_player_progress - ai[racer].progress);   // exact integer order: time + (time / (total - diff)) * diff
```
then makes equal times unique (t1==t2 -> t1+1, ...) and assigns `position` = number of racers with a smaller time,
+1 if >= the player's finishing position (0x4d9e60).

#### ai_check_cars_outside_handling_range (0x40b2d0) - per frame, from the main loop
The "handling range" is the chunk window `player_chunk ± (visible_chunks - 1)`. Cars that leave it are recycled to
the other end of the window (a ring of traffic travelling with the player).
```c
void ai_check_cars_outside_handling_range(int player_obj)
{
    int lo, hi, lo2, hi2, half = g_chunk_count2 /*0x4e5d30*/ / 2;
    int wrapped = chunk_window(objects[player_obj].chunk, g_visible_chunks - 1, &lo, &hi, &lo2, &hi2); // sub_445d70
    for (e in car list) {
        if (!e->active || e->object == player_obj) continue;
        int c = objects[e->object].chunk;
        if (wrapped) {
            if (c < half) {                       // in the low part [0 .. hi2]
                if (c <= hi2) continue;           // inside
                switch (e->type) { case 3: case 4: continue;  case 5: turn_off_adac(); continue;
                                   default: respawn_car(e->object, lo, 0); }     // too far ahead -> rear end
            } else {                              // in the high part [lo .. N-1]
                if (c >= lo) continue;
                switch (e->type) { case 3: continue;
                                   case 4: if (cop_states[ai[obj.ai].cop].state == 1) cop_siren(e->object, 0); continue;
                                   case 5: turn_off_adac(); continue;
                                   default: respawn_car(e->object, hi2, 0); }    // fell behind -> front end
            }
        } else {
            if (c < lo) {                         // fell behind
                switch (e->type) { case 3: continue;
                                   case 4: if (cop_states[...].state == 1) cop_siren(e->object, 0); continue;
                                   case 5: turn_off_adac(); continue;
                                   default: respawn_car(e->object, hi, 0); }
            } else if (c > hi) {                  // too far ahead
                switch (e->type) { case 3: case 4: continue;  case 5: turn_off_adac(); continue;
                                   default: respawn_car(e->object, lo, 0); }
            }
        }
    }
}
```
`chunk_window` (0x445d70, engine): `lo = c - r; hi = c + r;` if `lo < 0`: `lo2 = 0; hi2 = hi % N; lo += N - 1;
hi = N - 1; return 1`; else if `hi > N - 1`: `lo2 = 0; hi2 = hi % N; hi = N - 1; return 1`; else return 0
(N = 0x4e5d30, the number of chunks; note the `N - 1` off-by-one in the first case, keep it for 1:1).
Racers (3) and cops (4) are never relocated; the ADAC is switched off when it leaves the window; traffic (2) and the
player are the only other types.

#### respawn_car (0x40b480) and can_respawn_at (0x40b650)
```c
void respawn_car(int obj, int chunk, int force)
{
    chunk = chunk_wrap(chunk);
    if (objects[obj].ai == -1) return;
    int side = objects[obj].side;
    int dir  = (side == 0) ? (g_direction != 0) : (g_direction == 0);   // 1 = drives with the player
    // never respawn into a single-lane chunk (checks the player's carriageway lane count only)
    if (g_direction == 0) { if (chunk_rec(chunk)->lanes[1] /*+0xe8*/ == 1) return; }
    else                  { if (chunk_rec(chunk)->lanes[0] /*+0xe4*/ == 1) return; }
    int wp = rceng_get_waypoint_index(chunk, 0, side);
    if (!can_respawn_at(wp, obj, force)) { objects[obj].on = 0; return; }
    rceng_remove_object_in_waypoint_obstacle_list(obj);            // sub_442520
    vec3 p = *rceng_get_waypoint_position(&tmp, chunk, 0, dir);
    rceng_orient_object_on_road(obj, chunk, dir);                   // sub_442230
    object3d[objects[obj].obj3d].pos(+0x50) = p;
    objects[obj].prev_chunk = objects[obj].chunk = chunk;
    ai_reset_triggers(obj, 0);                                      // sub_406bf0 (re-reads speed/lane triggers)
    objects[obj].waypoint = wp;  ai[objects[obj].ai].wp(+0x00) = ai[..].wp2(+0x04) = wp;
    rceng_add_object_in_waypoint_obstacle_list(obj, 0);
    objects[obj].on(+0x34) = 1;
}

int can_respawn_at(int wp, int obj, int force)
{
    if (!force) {
        if (rand() % 30 != 0) return 0;                            // ~3.3 % chance per frame
        if (1.0f / frame_dt() < 14.5f) return 0;                   // not when the frame rate is below 14.5 fps
    }
    if (wp != -1) {
        W *w = &waypoints[wp];
        if (w->obstacle_count(+0x24)) return 0;
        if (waypoints[w->link14].obstacle_count) return 0;          // NOTE: read before the -1 check (as original)
        if (waypoints[w->link20].obstacle_count) return 0;
        if (w->link14 != -1 && waypoints[w->link14].obstacle_count) return 0;
        if (w->link20 != -1 && waypoints[w->link20].obstacle_count) return 0;
    }
    for (e in car list) if (e->active && objects[e->object].on &&
                            objects_overlap(obj, e->object) /*sub_446a10*/ && e->object != obj) return 0;
    return 1;
}
```
(`sub_446a10(a, b)` is the engine's distance test between two objects, using the object3d positions and half the
object extents at object+0x6c/+0x70/+0x74 with factors 0.5, -0.5, ±1.25; note that it tests the car's *current*
position, not the respawn position.)

So a car that left the window does not reappear immediately: each frame it gets a 1/30 chance, and only if lane 0
of the target chunk and its two linked waypoints (+0x14, +0x20) are empty and it does not overlap another car that is on.
While waiting it stays where it is with `on = 0` (an object that is off is not handled or drawn).

#### HANDLE_TRAFFIC_AI (0x40b770) - object handler of traffic cars (and of the ADAC once it arrived)
```c
int HANDLE_TRAFFIC_AI(int obj)
{
    car_reset_steering(obj);   // sub_40ece0  (car-control library)
    car_reset_throttle(obj);   // sub_40ebd0
    if (!g_race_started /*0x4d9a78*/) return 1;
    int a = objects[obj].ai;
    if (ai[a].state(+0x08) == 1 /*waiting at red light*/ && g_traffic_light_state /*0x48c484*/ != 0 /*not red*/) {
        ai[a].max_speed_allowed(+0x18) = 500.0f;  ai[a].state = 0;
    }
    ai_check_collision(obj);                                        // sub_4084b0
    if (ai[a].collision(+0x2c) == 0) {
        if (objects[obj].chunk != objects[obj].prev_chunk) check_triggers(obj);        // sub_407fc0
        int c10 = chunk_wrap(objects[obj].chunk + 10);
        lane_trigger(a, objects[obj].side, chunk_rec(c10)->trigger_ec /*+0xec*/);     // sub_407de0
        lane_trigger(a, objects[obj].side, chunk_rec(c10)->trigger_f0 /*+0xf0*/);
        change_lane(obj, 0, ai[a].lanes_allowed(+0x3c));                              // sub_403660
        handle_basic_ai(obj);                                                          // sub_4076b0
    }
    sndfx_play_object_sample(obj, ai[a].sample(+0x4c));            // sub_428bc0
    object3d[objects[obj].obj3d].y(+0x54) = road_height_at_object(obj);   // sub_40ede0
    return 1;
}
```
`road_height_at_object` (0x40ede0): plane of the object's chunk record: `h = (rec+0xdc - rec+0x3c * x - rec+0x44 * z)
/ rec+0x40` (sub_442f00); if `h - 1 > y` it also stores `y = h - 1` and negates `objects[obj].f70`; returns h.

#### The ADAC car ("Wegenwacht")
Globals: `g_adac_object` 0x4a5ccc, `g_adac_arrived` 0x4a5cc8, beacon: `g_adac_beacon_on` 0x4a5cd0,
`g_adac_beacon_state` 0x4a5cd4, `g_adac_beacon_tick` 0x4a5cd8.
```c
void init_adac_car(void)   // 0x40c4f0
{
    int side = (g_direction == 0), dir = side;
    int chunk = chunk_wrap(0);
    int lane = rceng_get_valid_lane(chunk, side, 4);
    g_adac_object = add_car(8 /*ADAC_ANIM*/, chunk, lane, side, dir, HANDLE_ADAC_AI /*0x401000*/, 0x427ad0, 5);
    add_ai(g_adac_object, 15 /*behaviour*/, rceng_get_waypoint_index(chunk, lane, side), lane);
    objects[g_adac_object].visible(+0x2c) = 0;  objects[g_adac_object].on(+0x34) = 0;
    rceng_remove_object_in_waypoint_obstacle_list(g_adac_object);
    g_adac_arrived = 0;
}
void turn_off_adac(void)   // 0x40c5f0
{
    rceng_remove_object_in_waypoint_obstacle_list(g_adac_object);
    objects[g_adac_object].visible(+0x2c) = 0;  objects[g_adac_object].on(+0x34) = 0;
}
int HANDLE_ADAC_AI(int obj)   // 0x401000 ("Handling Adac AI.")
{
    if (!g_game_running /*0x4d9a9c*/ || !g_race_started /*0x4d9a78*/) return 1;
    int a = objects[obj].ai;
    ai_check_collision(obj);
    if (ai[a].collision == 0) {
        if (g_adac_arrived == 1) {                   // "Adac has arrived": follow the player as traffic
            float v = objects[g_player_object].speed(+0x74) * 0.9f;
            if (ai[a].max_speed_allowed < v) {
                ai[a].max_speed_allowed = v;
                if (behaviors[ai[a].behavior].top_speed(+0x14) < ai[a].max_speed_allowed)
                    ai[a].max_speed_allowed = behaviors[ai[a].behavior].top_speed;
            }
            return HANDLE_TRAFFIC_AI(obj);
        }
        int d = abs(objects[obj].chunk - objects[g_player_object].chunk);   // AI_ADAC_RANGE = 3
        if (d < 3) {                                 // "In range, braking."
            ai[a].max_speed_allowed = 0.0f;
            if (objects[obj].speed == 0.0f) g_adac_arrived = 1;
        }
        handle_basic_ai(obj);
    }
    adac_beacon_update();                            // 0x40c4a0
    sndfx_play_object_sample(obj, ai[a].sample);
    object3d[objects[obj].obj3d].y = road_height_at_object(obj);
    return 1;
}
void adac_beacon_update(void)   // 0x40c4a0: toggles texture-copy slot 15 every 150 ms
{
    if (!g_adac_beacon_on) return;
    DWORD now = GetTickCount();
    if (GetTickCount() - g_adac_beacon_tick >= 150) {
        g_adac_beacon_tick = now;  texture_copy_set(15, g_adac_beacon_state);   // sub_42ae90
        g_adac_beacon_state = !g_adac_beacon_state;
    }
}
```
What makes the ADAC come: **nothing in spel.dat**. The ADAC car is created at chunk 0 (lane min(4, lanes-1) of the
player's side) and immediately switched off and made invisible (+0x34 = 0, +0x2c = 0, removed from the waypoint
obstacle list). No code ever switches it on again: `g_adac_object` is only referenced by init_adac_car and
turn_off_adac, the car list type 5 is only used to call turn_off_adac, `g_adac_beacon_on` is never written, and
respawn_car is never called for type 5. rceng_handle_objects skips objects that are off, so HANDLE_ADAC_AI never
runs in the shipped game. The ADAC/"Wegenwacht" is an unfinished feature (dormant code). Its intended logic, as
coded: drive (basic AI, behaviour 15, adac_lane in change_lane) towards the player until within 3 chunks
(AI_ADAC_RANGE), brake to a standstill, set `arrived`, then follow as traffic at up to 90 % of the player's speed
(capped at its top speed); switched off when it leaves the handling window. A port that wants to be 1:1 creates
the car and never shows it.


## 10. Cross-references, name mapping and open questions

### Names used here that are defined in other names files
| address | used in this document | other names file |
|---|---|---|
| 0x4076b0 | handle_basic_ai | handle_basic_ai (spel_physics) |
| 0x407660 | chunk_is_ahead / ai_chunk_ahead | chunk_moved_forward (spel_physics) |
| 0x4108d0 | apply_difficulty_modifier | ai_apply_difficulty (spel_physics) |
| 0x410a10 | ai_heading_with_road / ai_object_drives_forward | car_heading_matches_track (spel_render) |
| 0x40c920 | car_add_cop | car_cops_add_static (spel_objects) |
| 0x4451d0 | road_load_static_objects | road_load_omp (spel_objects) |
| 0x4258f0 / 0x425980 | player_autopilot_on / _off | plr_set_autopilot / plr_clear_autopilot (spel_physics) |
| 0x40eae0 | car_gear_accel | ai_gear_accel (spel_physics) |
| 0x40ebd0 / 0x40ece0 / 0x40ee80 | car_roll_spring / car_pitch_spring / car_suspension | car_body_roll / car_body_pitch / car_body_height |
| 0x40ede0 | road_height_at_object / car_ground_clamp | car_road_height_reset (spel_physics) |
| 0x40fba0 | car_stop_damp / ai_brake_hard | car_damp_all (spel_physics) |
| 0x4d9e28 / 0x4d9e2c / 0x4d9e68 / 0x4d9e70 | g_player_object / g_player_car / g_player_top_speed / g_player_progress | g_plr_obj / g_plr_car / g_plr_vmax / g_plr_progress |
| 0x4d9b10 / 0x4d9b20 | g_ai_model / g_demo | g_opt_ai_model / g_opt_demo |
| 0x4d9a7c | g_game_event / g_game_action / g_game_mode | g_game_event (spel_render) |
| 0x48c484 | g_traffic_light_state | g_traffic_light_state (spel_objects) |

The sections were written separately, so a few functions have two names in the text (e.g. 0x4034a0
`ai_cop_start_chase` = `fsm_add_cop`, 0x4035b0 `ai_fsm_release_cop` = `fsm_remove_cop`, 0x409060
`ai_cop_check_speeding` = `cop_fsm_lookout`, 0x402520 `ai_find_waypoint_lane` = `cop_find_waypoint_in_chunk`,
0x406d10/0x406d90 `ai_switch_lane_right/left` = `ai_lane_shift_a/b` = `ai_step_to_side_waypoint`,
0x4026c0 `add_cop` = `cop_new`). `names/spel_ai.txt` has one name per address; the address is authoritative.

### Things that look like bugs in the original (keep them for a 1:1 port)
- Traffic type and behaviour are drawn independently from the 12-entry model table (init_traffic).
- init_traffic passes `side` as the `dir` argument of add_car. This is only correct for DIRECTION = 0, which is
  the value in every shipped Game.ini.
- next_waypoint picks forks with `(int)dt() % 2`, which is always 0.
- change_lane's "LEFT" (lower lane index) calls 0x406d90, which follows link +0x1c, and "RIGHT" calls 0x406d10
  (+0x18). In the .dmp data +0x18 leads to lane - 1 and +0x1c to lane + 1 (lane 0 is next to the centre line,
  FORMAT_ROAD.md 3.4), so the switches go the other way than the bookkeeping. Traffic, which is spawned in lane 0,
  can then never "keep right" (+0x18 of lane 0 is -1) and stays in lane 0 unless it is blocked.
- change_lane: refuelling (driver type 7) and the default case set `l_chosen_lane = p_object`, and `l_lane` is
  uninitialised when the waypoint is on the other side.
- find_obstacle_here compares a stale distance; pick_lane can index lane -1.
- adjust_speed "stop" returns 2.0.
- cfun_cop_stuck practically never fires.
- The 0x408cb0 tie fix has no bound.
- 0x408780 divides by the sum of the two lengths (only the sign is used).
- In the old cop model, "target left" and "target right" do the same thing, the "other cops quit" loop skips the
  highest index, and cop_new writes `data[-1]` when the pool is full.
- The ScanPath fallback uses `cos/sin(1024.0)` in radians.
- All FSM cars use the *player's* car model and upgrades for their physics parameters (g_player_car 0x4d9e2c),
  not their own model at fsm+0x30.

### Open questions
- Which colour traffic-light states 2 and 3 are. State 0 is red: cars stop at trigger 10 and are released when
  the state is non-zero.
- t_ai_state +0x20 (15) and +0x54 are written once and never read; bh +0x28 (2.0) is never read.
- What the second argument of `sndfx_play_object_sample` (0x428bc0) means exactly: it is called every frame with
  `ai+0x4c` (1), or with 0 for a finished cop.
- Object fields +0x04, +0x24, +0x38 and +0x40 belong to the object/physics area; their names here come from how
  the AI uses them.
- The ADAC car and its beacon are dormant: nothing switches them on. Was it meant to come when the car is wrecked
  (`damage` >= 99 ends the race instead, 0x424bed)?
- World-mapper direction fields are only ever 1 or 0, so SIDESWITCH cases B/C/D and ai_wmap_closest_waypoint
  (0x40b970) never run.
- The language.ini fine texts ("BOETE: FL %d", "BOETES") are unused in spel.dat. They are probably leftovers
  from A2 Racer I / Autobahn Raser.
