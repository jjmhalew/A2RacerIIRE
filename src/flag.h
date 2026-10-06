/* flag.h - the waving chequered finish flag of spel.dat (gfx_flag.cpp; docs/RENDER.md "Finish flag"): a 17 x 11
 * vertex grid (10 triangle strips) 200 x 150 units, 100 units in front of the camera (camera space), drawn over the
 * whole picture with the depth test off, half transparent, a travelling sine wave in depth and shade. Started by the
 * finish event 1500 ms after the finish (flag_start(1)) and shown until the race ends; Game.ini [GFX OPTIONS]
 * finish_flag switches the drawing.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_FLAG_H
#define A2_FLAG_H
#include "vmath.h"

/* flag_init 0x41aea0 (init_game): on = Game.ini finish_flag, tick_dt = seconds per game tick; loads S_SFX */
int  flag_init(int on, float tick_dt);
void flag_free(void);
/* flag_start 0x41abc0: the wave period in frames (2 s) and the flag on (1) or off (0) */
void flag_start(int on);
void flag_tick(void);                               /* once per game tick: the wave's frame (flag_draw's frame++) */
/* flag_draw 0x41aee0: in front of the camera at eye looking at target; aspect = window width / height (the flag is
 * widened beyond 4:3 so it still covers the picture); alpha 0..1 interpolates the wave between the game ticks */
void flag_draw(Vec3 eye, Vec3 target, float aspect, float alpha);
#endif
