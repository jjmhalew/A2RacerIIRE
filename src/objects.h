/* objects.h - the static track objects of etappeN.omp (docs/FORMAT_OBJECTS.md 2): buildings, trees, signs, pylons,
 * barriers, traffic lights, speed cameras, parked police cars. Each belongs to a road chunk and is drawn with the
 * chunks of the visible window (objects_draw 0x440990).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_OBJECTS_H
#define A2_OBJECTS_H
#include "track.h"
#include "vmath.h"

enum { OT_SCENERY = 0, OT_SOLID = 1, OT_PYLON = 2, OT_BARRIER = 3, OT_TRAFFIC_LIGHT = 4, OT_FIXED = 5,
       OT_SPEED_CAMERA = 7, OT_ARROW = 8, OT_COP_AI = 9, OT_COP_PARKED = 10 };

typedef struct {
    int chunk, type, variant;
    Vec3 pos;
    float rot[3];               /* the file's degrees */
    float yaw;                  /* radians, as model_place takes it */
    int anim, frame;
    char name[32];
    int hidden;                 /* knocked away, collected ... (game logic) */
} TrackObject;

typedef struct {
    int n;
    TrackObject *obj;
    int *first, *count;         /* per chunk: objects sorted by chunk */
    int *order;
} ObjectSet;

int  objects_load(ObjectSet *s, const Track *t);
void objects_free(ObjectSet *s);
void objects_draw(const ObjectSet *s, const Track *t, int start, int count);
#endif
