/* particles.h - the particle module (prt_main.cpp, docs/RENDER.md "Particles / rain"): one emitter, the rain, a
 * spray of grey streaks from a point 20 above the camera that moves with it. Game.ini [GFX OPTIONS] particles (the
 * whole module; 0 on the CD) and part_rain (the rain emitter; 1 on the CD) switch it.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_PARTICLES_H
#define A2_PARTICLES_H
#include "vmath.h"

/* prt_init 0x431f20 (from init_game): reads the two Game.ini keys and sets up the rain emitter (prt_rain_setup
 * 0x4328b0); no particles yet */
void prt_init(void);
void prt_free(void);
/* the update half of prt_frame 0x431f60 (prt_emitter_update 0x432520: move, age and remove, then spawn): once per
 * game tick, which is one of the original's frames */
void prt_tick(void);
/* the drawing half of prt_frame (prt_rain_follow_cam 0x432990, prt_draw 0x4326e0): after the effects, before the finish
 * flag. eye / fwd: the camera as drawn; unit_px: the size of one of the original's pixels at view depth 1 */
void prt_draw(Vec3 eye, Vec3 fwd, float unit_px);
#endif
