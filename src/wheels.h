/* wheels.h - the wheels of the player's and the racers' cars (car_whl.cpp, docs/RENDER.md "Car wheels").
 * The racer bodies (RACER_1..7_ANIM, car types 0..6) have no wheels in their meshes; the original gives each such car
 * four 'WIEL_ANIM' sub-objects at per-type offsets (table 0x486198, read from spel.dat here), spins them with the
 * speed, turns the front pair with the steering and keeps them on the road while the body bobs. Traffic and police
 * meshes have their wheels modelled in.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_WHEELS_H
#define A2_WHEELS_H
#include "race.h"
#include "vmath.h"

/* reads the wheel table from spel.dat (once) and forgets the wheel state of the previous race */
void wheels_init(void);
/* one game tick: car_update_wheels 0x413900 (player) / car_update_wheels_ai 0x413be0 (racers), call after the
 * objects' update callbacks */
void wheels_tick(const Race *r);
/* draws the four wheels of car o (if its type has them); body = the body's world matrix as drawn, alpha = the
 * interpolation between the last two ticks */
void wheels_draw(const Race *r, int o, const Mat4 *body, float alpha);
#endif
