/* objects.h - the static track objects of etappeN.omp (docs/FORMAT_OBJECTS.md 2): buildings, trees, signs, pylons,
 * barriers, traffic lights, speed cameras, parked police cars. Each belongs to a road chunk and is drawn with the
 * chunks of the visible window (objects_draw 0x440990).
 *
 * In a race (objects_race_init / objects_tick) the object types of gm_init.cpp come alive (FORMAT_OBJECTS.md 2.1):
 * types 5, 8 and 10 get a physics body in the race's object list (road_load_omp 0x4451d0), so the cars collide with
 * them; pylons, barriers and speed-camera poles (2, 3, 7) fly off when the player hits them; arrows (8) blink; and the
 * road-side objects play their sample (the .omp variant: 1 road works, 2 church bell).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
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
    /* the main object's game state (FORMAT_OBJECTS.md 6) */
    int obj;                    /* types 5, 8, 10: the physics body in Race.obj (it holds position and yaw); -1 = none */
    int state;                  /* +0x10: 0 standing, 1 flying (after a hit), 2 lying */
    int sample;                 /* +0x14: the .omp variant, then the hit sounds; 0 = none */
    int snd;                    /* the sample its sound slot plays (a slot keeps its first sample, 0x428ac0) */
    int at;                     /* +0x18: the chunk it is in now (chunk = the .omp one, the draw list's) */
    Vec3 vel;                   /* +0x6c..+0x74 while flying: world units per tick */
    float grav;                 /* +0x30: the fall per tick, halved at every bounce */
    float pitch, roll;          /* instance +0x74 / +0x7c, ANGLE_UNITS */
} TrackObject;

typedef struct {
    int n;
    TrackObject *obj;
    int *first, *count;         /* per chunk: objects sorted by chunk */
    int *order;
    const struct Obj *phys;     /* the race's objects, for the bodies of types 5, 8, 10 (NULL outside a race) */
} ObjectSet;

int  objects_load(ObjectSet *s, const Track *t);
void objects_free(ObjectSet *s);
void objects_draw(const ObjectSet *s, const Track *t, int start, int count);

struct Race;
/* the physics bodies and the handlers' state: called by race_init before the cars are added */
void objects_race_init(struct Race *r);
/* once per tick before the objects' update callbacks: the handlers of the types without a body (1, 2, 3, 7) */
void objects_tick(void);
#endif
