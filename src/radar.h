/* radar.h - the radar of spel.dat (gfx_radr.cpp 0x41c5a0..0x41d455; docs/HUD.md 4.8): a north-up map of the road
 * drawn once into a surface, a window of it around the player copied into the radar texture HIRADAR every frame with
 * dots for the racers, the cops with their siren on and the player, shown in the lower left corner on a quad that
 * turns with the camera.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_RADAR_H
#define A2_RADAR_H
#include "race.h"
#include "vmath.h"

/* init_radar 0x41cf20, from init_game when texture copying works (txcopy_init first) */
void radar_init(const Race *r);
void radar_free(void);
/* render_game's call 0x4251d2: radar_draw 0x41d360 when no event runs, texture copying works, Game.ini
 * disable_overlay is 0 and the overlay detail (D) is at least 1. view = camera target - camera position. */
void radar_draw(const Race *r, Vec3 view);
#endif
