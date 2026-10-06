/* effects.h - the race's effects (docs/RENDER.md 9), all drawn with regions of S_SFX.PCX and counted in frames like
 * the original (one game tick = one of its frames): skid marks (gfx_skid.cpp), tyre smoke puffs (gfx_wolk.cpp) and
 * sparks (gfx_sprk.cpp). Game.ini [GFX OPTIONS] skidmarks, fade_skmarks, clouds, alpha_blending switch them.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_EFFECTS_H
#define A2_EFFECTS_H
#include "vmath.h"

typedef struct {               /* a car as the effects see it */
    Vec3 pos, right, up, fwd;  /* world position and axes */
    Vec3 vel;                  /* local velocity, m/s (x lateral, z forward) */
    float cg_rear;             /* g_car_types +0x26: rear axle distance, m */
} FxCar;

void fx_init(int skidmarks, int fade, int smoke, int alpha_blending);
void fx_free(void);
void fx_reset(void);
void fx_skid(const FxCar *car, int slip, float dt);          /* skid_update 0x41dd00, every tick */
void fx_smoke(const FxCar *car, int slip, float dt);         /* tyre_smoke_emitter 0x425a90, every tick */
void fx_sparks(Vec3 pos, Vec3 dir, float speed, float dt);   /* 0x41f150, on a collision of the player */
void fx_cloud(Vec3 pos, Vec3 vel, float dt);                 /* cloud_emit 0x41fff0: 2 smoke puffs (vel per tick) */
void fx_tick(float dt);
/* draws them; cam_right/cam_up orient the billboards (they copy the camera's orientation) */
void fx_draw(Vec3 cam_pos, Vec3 cam_right, Vec3 cam_up);
#endif
