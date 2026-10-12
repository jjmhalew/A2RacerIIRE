/* font3d.h - the 3D font of spel.dat (gfx_3dft.cpp; docs/RENDER.md "3D font"): words of FONT3D_ANIM letters (one
 * mesh per character, frame = index in the charset 0x486b68) put in the world and moved by an "effect serie" (a list
 * of up to 9 effects run one after the other, table 0x486ae0); the race-start countdown "3", "2", "1", "GO?" uses it.
 * Drawn without depth test, so always on top of the world. Counted in frames like the original (which converts
 * seconds with the frame time when an effect starts); one game tick is one of its frames.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_FONT3D_H
#define A2_FONT3D_H
#include "vmath.h"

/* reads the tables from spel.dat (0 = not found); tick_dt = seconds per game tick */
int  font3d_init(float tick_dt);
void font3d_free(void);
void font3d_clear(void);                            /* removes all letters */
/* font3d_create_text 0x417140: the word centred on a, its letters 12.5 apart along the right of direction b (y is
 * ignored), each letter turned to b; serie 0..2 */
void font3d_text(const char *text, Vec3 a, Vec3 b, int serie);
/* once per game tick: the letters' effects (the original runs them per drawn frame from font3d_draw 0x417870);
 * player = the player's position (some effects fly towards it) */
void font3d_tick(Vec3 player);
/* draws the letters, interpolated between the last two ticks (alpha 0..1), with the depth test off */
void font3d_draw(float alpha);
int  font3d_count(void);                            /* letters alive */
/* inst_build_matrix 0x459360: roll, then pitch, then yaw (ANGLE_UNITS, truncated to whole units), then pos */
Mat4 font3d_inst_matrix(Vec3 pos, float pitch, float yaw, float roll);
#endif
