/* shadow.h - the car shadows (gfx_shdw.cpp, docs/RENDER.md "Car shadows"): one S_SHADOW_ANIM quad, stretched to the
 * floor rectangle of each car's mesh and laid on the road under it, half transparent black, drawn just before the car.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_SHADOW_H
#define A2_SHADOW_H
#include "race.h"
#include "vmath.h"

/* init_car_shadow_object 0x410df0 (from init_game): the quad, its texture coordinates; Game.ini alpha_blending */
void shadow_init(void);
/* the shadow callback 0x41d460 for object o (only cars have one: add_car, car_cops_add_static); body = the car's
 * world matrix as it is drawn. Call right before the car's body. */
void shadow_draw(const Race *r, int o, const Mat4 *body);
#endif
