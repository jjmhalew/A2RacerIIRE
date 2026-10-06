/* objects.c - see objects.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "objects.h"
#include "model.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static const ObjectSet *g_sort_set;
static int cmp_chunk(const void *a, const void *b)
{
    const TrackObject *x = &g_sort_set->obj[*(const int *)a], *y = &g_sort_set->obj[*(const int *)b];
    return x->chunk != y->chunk ? (x->chunk < y->chunk ? -1 : 1) : (*(const int *)a - *(const int *)b);
}

int objects_load(ObjectSet *s, const Track *t)
{
    memset(s, 0, sizeof *s);
    char name[32];
    snprintf(name, sizeof name, "etappe%d.omp", t->index);
    size_t n = 0;
    uint8_t *d = res_load_file(name, &n);
    if (!d || n < 4) { free(d); return 0; }
    int count = (int)rd32(d), have = (int)((n - 4) / 68);
    if (count < 0 || count > 100000) count = have;
    s->n = count;
    s->obj = calloc((size_t)count + 1, sizeof *s->obj);
    for (int i = 0; i < count; i++) {
        TrackObject *o = &s->obj[i];
        if (i < have) {
            const uint8_t *p = d + 4 + (size_t)i * 68;
            o->chunk = (int)rd32(p);
            o->type = (int)rd32(p + 4);
            o->pos = v3(rdf(p + 8), rdf(p + 12), rdf(p + 16));
            o->rot[0] = rdf(p + 20); o->rot[1] = rdf(p + 24); o->rot[2] = rdf(p + 28);
            memcpy(o->name, p + 32, 31);
            o->name[31] = 0;
            o->variant = (int)rd32(p + 64);
            o->yaw = o->rot[1] * PI_F / 180;
        } else if (i > 0) {
            /* etappe2/6.omp count one record more than they hold: the loader keeps the previous record's values,
             * with the rotation converted to ANGLE_UNITS a second time (FORMAT_OBJECTS.md 2) */
            *o = s->obj[i - 1];
            o->yaw = -o->rot[1] * ((float)ANGLE_UNITS / 360.0f) * PI_F / 180;
        }
        if (o->chunk < 0 || o->chunk >= t->nchunks) o->chunk = 0;
        o->anim = anim_find(o->name);
    }
    free(d);
    /* index by chunk */
    s->order = malloc(sizeof *s->order * (size_t)(count + 1));
    for (int i = 0; i < count; i++) s->order[i] = i;
    g_sort_set = s;
    qsort(s->order, (size_t)count, sizeof *s->order, cmp_chunk);
    s->first = calloc((size_t)t->nchunks, sizeof *s->first);
    s->count = calloc((size_t)t->nchunks, sizeof *s->count);
    for (int i = count - 1; i >= 0; i--) {
        int c = s->obj[s->order[i]].chunk;
        s->first[c] = i;
        s->count[c]++;
    }
    return 1;
}

void objects_free(ObjectSet *s)
{
    free(s->obj);
    free(s->order);
    free(s->first);
    free(s->count);
    memset(s, 0, sizeof *s);
}

void objects_draw(const ObjectSet *s, const Track *t, int start, int count)
{
    if (!s->n) return;
    for (int k = 0; k <= count; k++) {
        int c = track_wrap(t, start + k);
        for (int j = 0; j < s->count[c]; j++) {
            const TrackObject *o = &s->obj[s->order[s->first[c] + j]];
            if (o->hidden || o->type == OT_COP_AI) continue;     /* type 9: the AI police car is drawn by the AI */
            model_draw(anim_model(o->anim, o->frame), o->pos, o->yaw);
        }
    }
    gfx_set_model(NULL);
}
