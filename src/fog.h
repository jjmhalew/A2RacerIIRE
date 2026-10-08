/* fog.h - the fog (gfx_bkg.cpp init_bkg / bkg_set_fog_params, fog_enable, the fog triggers; docs/RENDER.md 8):
 * Game.ini [OPTIONS] fog (CD: 0) = 1 or 2 turns it on in a random half of the races; its colour and range follow the
 * track progress and the race clock like the sky. fog = 1 also switches the device to TEXTUREMAPBLEND COPY (texture
 * colours only: "conflicts with alpha blending"), fog = 2 keeps MODULATE.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_FOG_H
#define A2_FOG_H
#include "gfx.h"

void fog_init(void);                    /* init_bkg 0x418da0, once per race */
/* bkg_update_colors 0x418f00 / bkg_set_fog_params 0x418830: this frame's fog, from the sky's progress factor a
 * (|sin(t pi/2)|) and the lap clock. Call before the sky is drawn; it sets the device states for the frame. */
void fog_frame(float a);
/* only the device states (fog off, COPY) for the frame: the software modes, which have no backdrop (screen.h) */
void fog_states(void);
/* the vertex fog of the sky backdrop (TL vertices: the specular alpha is the fog factor), over the sky's triangles
 * as just drawn (tex v = 0: the top edge) */
void fog_sky(const GfxVertex *v, int n, float a);
void fog_trigger_fade_start(void);      /* track trigger 16, 0x429ad0 */
void fog_trigger_fade_end(void);        /* track trigger 17, 0x429b20 */
#endif
