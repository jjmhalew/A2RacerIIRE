/* collide.h - collisions of the cars (spel.dat bbox.cpp, obj_phys.cpp, obj_main.cpp, plr_car.cpp; docs/PHYSICS.md 8):
 * oriented rectangles in the XZ plane for the cars of the physics list and for the guard rails (.cmp) of the chunks
 * around a car, a swept test that stops the motion at the first contact, a slide-out, impulse responses, the
 * collision callbacks and the player's damage.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_COLLIDE_H
#define A2_COLLIDE_H
#include "race.h"

void  collide_init(Race *r);
/* the steps of one obj_move_all sub-step (PHYSICS.md 8.6) */
float bbox_sweep_object(int obj, int *rotblk, float dt);                   /* 0x44a0f0 -> fraction of the step */
int   obj_slide_out(int obj, float dt, Vec3 *out, float frac, int *rotblk); /* 0x441490: 1 = move by *out */
void  coll_after_step(int obj);        /* obj_resolve_collision 0x4497f0, the callback and the in-collision flags */
/* collision callbacks (object +0x48) */
int   plr_collision_cb(int obj);                                           /* 0x427c70 */
int   mesh_damage_collision_func(int obj);                                 /* 0x427ad0 */
#endif
