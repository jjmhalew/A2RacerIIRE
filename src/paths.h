/* paths.h - the scripted paths of etappeN.pth (pth_main.cpp, pth_heli.cpp; docs/FORMAT_OBJECTS.md 3): Amsterdam's
 * canal tour boats, the trams of Den Haag and Rotterdam, the helicopter over Den Haag, the aeroplane over the A4 and
 * a few cars, each started when the player reaches its start chunk and moved along a spline in a fixed time.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_PATHS_H
#define A2_PATHS_H
#include "vmath.h"

int  paths_load(int etappe);                      /* 0 paths is fine (etappe5 has none) */
void paths_free(void);
/* once per game tick (pth_check_triggers 0x434460 + pth_update_all 0x434380): lap = laps done (0-based) */
void paths_update(int player_chunk, int lap, float dt);
void paths_draw(void);
/* the moving path objects, for collisions: returns the count, positions/yaw (1024 units) in out arrays */
int  paths_objects(Vec3 *pos, float *yaw, int *anim, int max);
#endif
