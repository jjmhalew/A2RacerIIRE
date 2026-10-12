/* paths.c - see paths.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "paths.h"
#include "model.h"
#include "res.h"
#include "sfx.h"
#include "track.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int start_chunk, end_chunk, type, lap, time_ms, npoints;
    Vec3 p[50];
    int started;                /* path+0x338: started in this lap */
} Path;

typedef struct {
    int active;
    int frames, frame;          /* frames = time_ms * 0.001 / frame time, rounded down to whole segments */
    float k;                    /* tangent factor of the spline (0.5) */
    int path;
    int anim, anim2, anim3;     /* the object, and the helicopter's two rotors */
    Vec3 pos;
    float yaw;                  /* ANGLE_UNITS per turn, the original's object angle */
    float rotor;
    int sample;                 /* object +0x14, played every frame by the update (sndfx_play_object_sample 0x428bc0) */
} Active;

#define SND_KEY 1000            /* sfx_object keys of the path objects: SND_KEY + slot (race objects use 0..511) */

static Path g_path[20];         /* g_paths 0x4e0710, max 20 */
static int g_npaths, g_lap = -1;
static Active g_act[10];        /* g_active_paths 0x4e0580 */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

int paths_load(int etappe)
{
    paths_free();
    char n[32];
    snprintf(n, sizeof n, "etappe%d.pth", etappe);
    size_t sz = 0;
    uint8_t *d = res_load_file(n, &sz);
    if (!d) return 0;
    size_t p = 8;
    if (sz < 8 || (int32_t)rd32(d) != -1) { free(d); return 0; }     /* 'Incorrect version path file' */
    int np = (int)rd32(d + 4);
    for (int i = 0; i < np && i < 20 && p + 24 <= sz; i++) {
        Path *t = &g_path[g_npaths++];
        t->start_chunk = (int)rd32(d + p); t->end_chunk = (int)rd32(d + p + 4); t->type = (int)rd32(d + p + 8);
        t->lap = (int)rd32(d + p + 12); t->time_ms = (int)rd32(d + p + 16); t->npoints = (int)rd32(d + p + 20);
        p += 24;
        if (t->npoints < 0 || t->npoints >= 50) t->npoints = 0;
        for (int k = 0; k < t->npoints && p + 16 <= sz; k++, p += 16)
            t->p[k] = v3(rdf(d + p + 4), rdf(d + p + 8), rdf(d + p + 12));
    }
    free(d);
    return g_npaths;
}

void paths_free(void)
{
    for (int i = 0; i < 10; i++) if (g_act[i].active) sfx_object_stop(SND_KEY + i);
    memset(g_path, 0, sizeof g_path);
    memset(g_act, 0, sizeof g_act);
    g_npaths = 0;
    g_lap = -1;
}

/* hermite_1d 0x434030 on each axis, pth_get_position 0x433b10: segment i of the n-1 between the points, tangents
 * (p[i+1] - p[i-1]) * k and (p[i+2] - p[i]) * k, indices clamped at the ends */
static Vec3 path_position(const Path *t, int frames, int frame, float k)
{
    int n = t->npoints;
    if (n <= 1) return n ? t->p[0] : v3(0, 0, 0);
    int seg = frames / (n - 1);
    if (seg < 1) seg = 1;
    int i = frame / seg;
    if (i > n - 2) i = n - 2;
    float s = (float)(frame - i * seg) / seg;
    int im = i > 0 ? i - 1 : 0, i2 = i + 2 < n ? i + 2 : n - 1;
    Vec3 p0 = t->p[i], p1 = t->p[i + 1];
    Vec3 m0 = v3_scale(v3_sub(p1, t->p[im]), k), m1 = v3_scale(v3_sub(t->p[i2], p0), k);
    float s2 = s * s, s3 = s2 * s;
    float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
    return v3_add(v3_add(v3_scale(p0, h00), v3_scale(m0, h10)), v3_add(v3_scale(p1, h01), v3_scale(m1, h11)));
}

static const char *TRAFFIC[5] = { "N_BUS_ANIM", "N_VRTAUT_ANIM", "N_BESTEL_ANIM", "N_GOLF_ANIM", "N_ASTRA_ANIM" };  /* 0x4969f8 */

static void start(int pi, float dt)
{
    const Path *t = &g_path[pi];
    Active *a = NULL;
    for (int i = 0; i < 10; i++) if (!g_act[i].active) { a = &g_act[i]; break; }
    if (!a || t->npoints < 2 || t->time_ms <= 0) return;
    memset(a, 0, sizeof *a);
    a->anim = a->anim2 = a->anim3 = -1;
    switch (t->type) {
    case 3: a->anim = anim_find("E00_ROND_ANIM"); break;                 /* canal tour boat, pth_boat_init 0x434800 */
    case 4: a->anim = anim_find("C_TREIN_ANIM"); a->sample = SFX_TREIN; break;   /* tram 0x434a70 */
    case 5: a->anim = anim_find("C_HELI_ANIM"); a->sample = SFX_HELI;    /* helicopter 0x433170 */
            a->anim2 = anim_find("C_LBLADE_ANIM"); a->anim3 = anim_find("C_SBLADE_ANIM"); break;
    case 6: a->anim = anim_find(TRAFFIC[rand() % 5]); a->sample = 32; break;      /* traffic car 0x434cf0 */
    case 7: a->anim = anim_find("A_VLIEGT_ANIM"); a->sample = SFX_VLIEGT; break; /* aeroplane 0x434550 */
    default: return;                                                     /* 0..2: no object in the data */
    }
    a->active = 1;
    a->path = pi;
    a->k = 0.5f;
    /* pth_activate 0x434220: frames from the time and the frame time, rounded down to whole segments */
    a->frames = (int)(t->time_ms * 0.001f / dt);
    a->frames = a->frames / (t->npoints - 1) * (t->npoints - 1);
    a->pos = t->p[0];
}

void paths_update(int player_chunk, int lap, float dt)
{
    if (lap != g_lap) {                                     /* the counters reset when the lap changes */
        g_lap = lap;
        for (int i = 0; i < g_npaths; i++) g_path[i].started = 0;
    }
    for (int i = 0; i < g_npaths; i++) {                    /* pth_check_triggers 0x434460 */
        Path *t = &g_path[i];
        if (t->start_chunk && t->start_chunk == player_chunk && !t->started && (t->lap == 0 || t->lap == lap + 1)) {
            t->started = 1;
            start(i, dt);
        }
    }
    for (int i = 0; i < 10; i++) {                          /* pth_update_all 0x434380 */
        Active *a = &g_act[i];
        if (!a->active) continue;
        const Path *t = &g_path[a->path];
        if (a->frame < a->frames - 2) {                     /* e.g. pth_boat_update 0x4348f0 */
            Vec3 p = path_position(t, a->frames, a->frame, a->k), q = path_position(t, a->frames, a->frame + 1, a->k);
            a->frame++;
            a->yaw = (atan2f(q.z - p.z, q.x - p.x) - 3.14159f * 0.5f) * ANGLE_UNITS / 6.28319f;
            a->pos = p;
        } else if (!t->end_chunk) a->active = 0;            /* ran to its end */
        a->rotor += dt * ANGLE_UNITS * 3;
        if (t->end_chunk && t->end_chunk == player_chunk) a->active = 0;
        if (a->active && a->sample) sfx_object(SND_KEY + i, a->sample, a->pos, 1);
        else sfx_object_stop(SND_KEY + i);
    }
}

void paths_draw(void)
{
    for (int i = 0; i < 10; i++) {
        const Active *a = &g_act[i];
        if (!a->active) continue;
        float yaw = -a->yaw * 6.28319f / ANGLE_UNITS;        /* object angle -> model_place's yaw */
        model_draw(anim_model(a->anim, 0), a->pos, yaw);
        if (a->anim2 >= 0) model_draw(anim_model(a->anim2, 0), a->pos, yaw + a->rotor * 6.28319f / ANGLE_UNITS);
        if (a->anim3 >= 0) model_draw(anim_model(a->anim3, 0), a->pos, yaw);
    }
    gfx_set_model(NULL);
}

int paths_objects(Vec3 *pos, float *yaw, int *anim, int max)
{
    int n = 0;
    for (int i = 0; i < 10 && n < max; i++)
        if (g_act[i].active) { pos[n] = g_act[i].pos; yaw[n] = g_act[i].yaw; anim[n] = g_act[i].anim; n++; }
    return n;
}
