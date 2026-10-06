/* lights.h - the alpha-blended object lights (gfx_lght.cpp, docs/RENDER.md "Object lights"): radial red glows, used
 * only for the player car's two brake lights, lit while g_plr_brake_light is set. Game.ini [GFX OPTIONS] object_lights
 * and alpha_blending switch them.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_LIGHTS_H
#define A2_LIGHTS_H
#include "race.h"
#include "vmath.h"

/* light_init 0x41b330 (from init_game) and create_brake_lights 0x425500 (from init_player_car): after the player car */
void lights_init(const Race *r);
/* the brake-light switch of handle_player_car 0x425f0b: once per game tick, before the cars' update callbacks (it
 * sees the previous tick's g_plr_brake_light, as the original does) */
void lights_tick(const Race *r);
/* the drawn world matrix of object o this frame (the lights of o follow it) */
void lights_attach(int o, const Mat4 *body);
/* light_draw 0x41b720: after the opaque objects, before the other effects */
void lights_draw(void);
/* the object lights are in use (g_opt_object_lights after light_init); else the brake lights are a texture copy
 * (txcopy.c) */
int lights_on(void);
#endif
