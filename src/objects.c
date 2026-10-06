/* objects.c - see objects.h. The object types are gm_init.cpp's (init_track_object_types 0x429310, table 0x48c2a8;
 * docs/FORMAT_OBJECTS.md 2.1); a handler runs once per tick for the objects in the handling window, as
 * rceng_handle_objects 0x440850 calls it once per frame.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "objects.h"
#include "ai.h"
#include "model.h"
#include "race.h"
#include "res.h"
#include "rules.h"
#include "sfx.h"
#include <math.h>
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
        o->obj = -1;
        o->at = o->chunk;
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

/* the chunk an object is in now: a body's (obj_move_all), a flying object's (+0x18), else its .omp chunk */
static int object_chunk(const ObjectSet *s, const TrackObject *o)
{
    return o->obj >= 0 && s->phys ? s->phys[o->obj].chunk : o->at;
}

static void draw_object(const ObjectSet *s, const TrackObject *o)
{
    const Model *m = anim_model(o->anim, o->frame);
    if (o->obj >= 0 && s->phys) {
        /* a body lies on its chunk's plane like the cars (rceng_set_object_to_chunk_orientation 0x442390, called for
         * every physics object by obj_physics_step) */
        const Obj *c = &s->phys[o->obj];
        Vec3 r, u, f;
        car_axes(c, &r, &u, &f);
        Mat4 w = m4_identity();
        w.m[0] = r.x; w.m[1] = r.y; w.m[2] = r.z;
        w.m[4] = u.x; w.m[5] = u.y; w.m[6] = u.z;
        w.m[8] = f.x; w.m[9] = f.y; w.m[10] = f.z;
        w.m[12] = c->pos.x; w.m[13] = c->pos.y; w.m[14] = c->pos.z;
        model_draw_matrix(m, &w);
    } else if (o->pitch != 0 || o->roll != 0) {
        /* knocked over (object_fall_over 0x429020): yaw, pitch and roll as the cars' body pitch / roll are drawn */
        Mat4 body = m4_mul(m4_rot_x(-o->pitch * 2 * PI_F / ANGLE_UNITS), m4_rot_z(o->roll * 2 * PI_F / ANGLE_UNITS));
        Mat4 w = m4_mul(model_place(o->pos, o->yaw), body);
        model_draw_matrix(m, &w);
    } else model_draw(m, o->pos, o->yaw);
}

void objects_draw(const ObjectSet *s, const Track *t, int start, int count)
{
    if (!s->n) return;
    for (int k = 0; k <= count; k++) {
        int c = track_wrap(t, start + k);
        for (int j = 0; j < s->count[c]; j++) {
            const TrackObject *o = &s->obj[s->order[s->first[c] + j]];
            if (o->hidden || o->type == OT_COP_AI) continue;     /* type 9: the AI police car is drawn by the AI */
            if (object_chunk(s, o) != c) continue;               /* moved away: below */
            draw_object(s, o);
        }
    }
    /* objects that left their .omp chunk (hit and flying, pushed, or placed by obj_move_all's road_find_chunk in
     * another one) are drawn with the chunk they are in now */
    for (int i = 0; i < s->n; i++) {
        const TrackObject *o = &s->obj[i];
        int c = object_chunk(s, o);
        if (c == o->chunk || o->hidden || o->type == OT_COP_AI) continue;
        if (track_wrap(t, c - start) <= count) draw_object(s, o);
    }
    gfx_set_model(NULL);
}

/* ---- the object types in a race ---------------------------------------------------------------------------------- */
#define OBJ_SND_KEY 2000        /* sfx_object keys: OBJ_SND_KEY + .omp index (race objects 0..511, paths 1000..) */

static Race *R;
static ObjectSet *S;
static int g_track_of[MAX_OBJS];                /* race object -> .omp index of the bodies */
static unsigned char *g_snd_on, *g_snd_was;     /* per .omp object: asked for its sample this tick / the tick before */
static int g_arrow_state;                       /* 0x4dc96c: 0 = frame 1, 1 = frame 0 */
static float g_arrow_end;                       /* timer 7: game time (ms) when the state changes */

static int ftol(float f) { return (int)f; }
static float now_ms(void) { return (float)R->tick * 1000.0f / RACE_HZ; }

/* sndfx_play_object_sample 0x428bc0 -> sndfx_object_sample 0x428ac0: the object's sample (+0x14) at its position,
 * heard by the player car; on = loop. Returns 1 = done: no sample, out of range (|dx| + |dz| >= 2500), no free
 * slot, or a one-shot that has ended. A slot that is already playing is only updated (0x428b5d): it keeps the
 * sample it was started with */
static int play_object_sample(int i, int on)
{
    TrackObject *t = &S->obj[i];
    if (!t->sample) return 1;
    int idx = t->snd ? t->snd : t->sample;
    if (!sfx_object(OBJ_SND_KEY + i, idx, t->pos, on)) { t->snd = 0; return 1; }
    if (!t->snd && getenv("A2_TRACE")) fprintf(stderr, "t%u object %d %s plays sample %d\n", R->tick, i, t->name, idx);
    t->snd = idx;
    g_snd_on[i] = 1;
    return 0;
}

/* sub_429350 -> sub_446930(obj, g_plr_obj): the player car's centre within the sum of both mesh radii (+0x74),
 * in the XZ plane. Only the player knocks objects over */
static int hit_by_player(int i)
{
    const TrackObject *t = &S->obj[i];
    const Obj *p = &R->obj[R->plr_obj];
    const Model *a = anim_model(t->anim, t->frame), *b = anim_model(p->anim, p->frame);
    float dx = p->pos.x - t->pos.x, dz = p->pos.z - t->pos.z;
    float r = (b ? b->radius : 0) + (a ? a->radius : 0);
    return dx * dx + dz * dz < r * r;
}

/* object_flyup_check_collision 0x428ea0 (obj, mass): when the player hits it, it takes the car's velocity turned to
 * the world by the car's yaw +-125 units at random, the forward part x1.5, and flies up at (vx + vz) / mass */
static int object_flyup_check_collision(int i, float mass)
{
    TrackObject *t = &S->obj[i];
    if (mass == 0) return 0;                                          /* "massa is zero" (fatal) */
    if (!hit_by_player(i)) return 0;
    const Obj *p = &R->obj[R->plr_obj];
    Vec3 v = p->vel;
    int a = ftol(p->yaw) + (125 - rand() % 250);
    if (a > ANGLE_UNITS) a -= ANGLE_UNITS;                            /* [0x49b010], then & mask */
    float s = (v.x + v.z) / mass, sn = race_sin(a), cs = race_cos(a);
    t->vel.x = cs * v.x - sn * v.z * 1.5f;
    t->vel.z = sn * v.x - cs * v.z * -1.5f;
    t->vel.y = s;
    t->grav = 1.0f;                                                   /* +0x30 */
    t->state = 1;
    if (getenv("A2_TRACE"))
        fprintf(stderr, "t%u object %d %s (type %d) hit, flies at %.1f %.1f %.1f\n", R->tick, i, t->name, t->type,
                t->vel.x, t->vel.y, t->vel.z);
    return 1;
}

/* object_fall_over 0x429020 (obj, rest pitch, rest height): one tick of the flight, per frame in the original
 * (positions in world units, the velocity added as it is). Returns 1 when it touches the ground: it bounces (vy
 * x-0.6, the gravity halves) and lies down once vy < 2. It tumbles by 64 / 32 / 48 units of pitch / yaw / roll */
static int object_fall_over(int i, int rest_pitch, float rest_h)
{
    TrackObject *t = &S->obj[i];
    int ret = 0;
    t->vel.x *= 0.98f;
    t->vel.z *= 0.98f;
    t->vel.y -= t->grav;
    t->pos = v3_add(t->pos, t->vel);
    t->at = track_find_chunk(&R->track, t->at, t->pos);               /* rceng_update_object_chunk 0x442020 */
    float h = chunk_height_at(&R->track.chunks[t->at], t->pos.x, t->pos.z);
    const Model *m = anim_model(t->anim, t->frame);
    float r = m ? m->radius : 0, dy = t->pos.y - h;
    if (r * r > dy * dy) {                                            /* within its radius of the road plane */
        t->vel.y *= -0.6f;
        t->pos.y = h + 1;
        if (t->vel.y < 2) {
            t->pos.y = h + rest_h;
            t->pitch = (float)rest_pitch;
            t->state = 2;
            t->roll = 0;
        }
        t->grav *= 0.5f;
        ret = 1;
        if (getenv("A2_TRACE"))
            fprintf(stderr, "t%u object %d bounces at %.0f %.0f %.0f (chunk %d)%s\n", R->tick, i, t->pos.x, t->pos.y,
                    t->pos.z, t->at, t->state == 2 ? ", lies down" : "");
    }
    t->pitch += 64;
    t->yaw -= 32 * 2 * PI_F / ANGLE_UNITS;                            /* instance yaw +32 (yaw is -units here) */
    t->roll += 48;
    return ret;
}

/* disable_speed_cameras_ahead 0x428de0: the speed-camera triggers (1) of the 20 chunks from the pole's on are
 * cleared for the rest of the race */
static void disable_speed_cameras_ahead(int i)
{
    int c = S->obj[i].at;
    if (c < 0 || c > R->track.nchunks) c = R->obj[R->plr_obj].chunk;
    for (int k = 0; k < 20; k++) {
        Chunk *ch = &R->track.chunks[track_wrap(&R->track, c + k)];
        if (ch->trigger[0] == 1) ch->trigger[0] = 0;
        if (ch->trigger[1] == 1) ch->trigger[1] = 0;
    }
}

static void objtype_solid(int i)                                      /* 0x429380: types 1 and 5, a looping sample */
{
    play_object_sample(i, 1);
}

/* objtype_pylon 0x4293f0 (type 2), objtype_barrier 0x4297d0 (type 3), objtype_speed_camera 0x428d30 (type 7): +0x10
 * 0 standing (hit test), 1 flying; the hit and the first bounce set the one-shot sample, which is cleared when done.
 * Knocking a speed-camera pole over switches the cameras ahead off and gives the RADAR BONUS (message 51, 2000
 * money at the finish: rules.c) */
static void objtype_knock_over(int i, float mass, int rest_pitch, float rest_h, int s_hit, int s_bounce)
{
    TrackObject *t = &S->obj[i];
    if (t->state == 0) {
        if (object_flyup_check_collision(i, mass)) {
            if (t->type == OT_SPEED_CAMERA) disable_speed_cameras_ahead(i);
            t->sample = s_hit;
            if (t->type == OT_SPEED_CAMERA) rules_radar_bonus(R);    /* g_radar_bonus_count++, g_hud_message 0x33 */
        }
    } else if (t->state == 1) {
        if (object_fall_over(i, rest_pitch, rest_h)) t->sample = s_bounce;
    }
    if (play_object_sample(i, 0)) t->sample = 0;
}

/* the handlers of the bodies (types 5, 8, 10), called as their update callbacks; the .omp record follows the body */
static TrackObject *body_sync(int o)
{
    TrackObject *t = &S->obj[g_track_of[o]];
    const Obj *c = &R->obj[o];
    t->pos = c->pos;
    t->yaw = -c->yaw * 2 * PI_F / ANGLE_UNITS;
    t->at = c->chunk;
    if (getenv("A2_TRACE") && c->in_collision)
        fprintf(stderr, "t%u object %d %s (type %d) in a collision, v %.2f %.2f\n", R->tick, g_track_of[o], t->name,
                t->type, c->vel.x, c->vel.z);
    return t;
}

static int body_solid(int o)                                          /* type 5: objtype_solid 0x429380 */
{
    body_sync(o);
    objtype_solid(g_track_of[o]);
    return 1;
}

static int objtype_arrow(int o)                                       /* 0x4296b0: type 8, blinks with timer 7 */
{
    TrackObject *t = body_sync(o);
    Obj *c = &R->obj[o];
    c->vel = v3_scale(c->vel, 0.95f);                                 /* pushed: slows down */
    c->yawrate *= 0.95f;
    if (g_arrow_state == 0) c->frame = 1;
    else if (g_arrow_state == 1) c->frame = 0;
    t->frame = c->frame;
    if (now_ms() >= g_arrow_end) {                                    /* timer_expired(7) */
        if (g_arrow_state == 0) { g_arrow_state = 1; g_arrow_end = now_ms() + 1000; }
        else { g_arrow_state = 0; g_arrow_end = now_ms() + 500; }
    }
    return 1;
}

static int objtype_police_car(int o)                                  /* 0x4293a0: type 10, a parked police car */
{
    body_sync(o);
    Obj *c = &R->obj[o];
    c->vel = v3_scale(c->vel, 0.9f);
    c->yawrate *= 0.9f;
    return 1;
}

void objects_race_init(Race *r)
{
    R = r;
    S = &r->objects;
    S->phys = r->obj;
    free(g_snd_on);
    free(g_snd_was);
    g_snd_on = calloc((size_t)S->n + 1, 1);
    g_snd_was = calloc((size_t)S->n + 1, 1);
    for (int o = 0; o < MAX_OBJS; o++) g_track_of[o] = -1;
    g_arrow_state = 0;                                                /* sub_4297b0: timer 7 at 500 ms */
    g_arrow_end = 500;
    for (int i = 0; i < S->n; i++) {
        TrackObject *t = &S->obj[i];
        t->obj = -1;
        t->state = 0;
        t->sample = t->variant;                                       /* main_object_add(..., variant) -> +0x14 */
        t->snd = 0;
        t->at = t->chunk;
        /* road_load_omp 0x44542e, jump table 0x4454d8: obj_init_physics_idx (vel 0, yaw rate 0, mass, restitution,
         * length 4, width 1) and physics_list_add */
        float mass, e;
        int (*cb)(int o);
        switch (t->type) {
        case OT_FIXED: mass = 0; e = 0.1f; cb = body_solid; break;            /* columns, walls: immovable */
        case OT_ARROW: mass = 13000; e = 0.2f; cb = objtype_arrow; break;
        case OT_COP_PARKED: mass = 1300; e = 0.2f; cb = objtype_police_car; break;
        default: continue;
        }
        if (t->anim < 0) continue;
        int o = obj_add(t->anim, t->pos, -t->yaw * ANGLE_UNITS / (2 * PI_F));   /* ry' = -ry * units / 360 */
        if (o < 0) break;
        Obj *c = &r->obj[o];
        c->chunk = c->prev_chunk = t->chunk;                          /* main_object +0x18 = the .omp chunk */
        c->visible = 0;                                               /* drawn by objects_draw */
        c->frame = t->frame;
        c->update = cb;
        obj_init_physics(c, mass, 1.0f, 4.0f, e);
        t->obj = o;
        g_track_of[o] = i;
    }
}

/* tests (A2_TEST_OBJ=n[,d]): the player car starts d chunks (default 3) before .omp object n, as far from the centre
 * line as the object, facing along the road */
static void test_place_player(int i, int d)
{
    if (i < 0 || i >= S->n || R->plr_obj < 0) return;
    const TrackObject *t = &S->obj[i];
    const Chunk *a = &R->track.chunks[t->chunk], *b = &R->track.chunks[track_wrap(&R->track, t->chunk - d)];
    float lat = v3_dot(v3_sub(t->pos, a->pos), a->right);
    Vec3 f = b->fwd;
    Obj *p = &R->obj[R->plr_obj];
    p->pos = v3_add(b->pos, v3_scale(b->right, lat));
    p->chunk = p->prev_chunk = track_wrap(&R->track, t->chunk - d);
    p->pos.y = chunk_height_at(&R->track.chunks[p->chunk], p->pos.x, p->pos.z);
    p->yaw = atan2f(-f.x, f.z) * ANGLE_UNITS / (2 * PI_F);
    p->vel = v3(0, 0, 0);
    p->yawrate = 0;
    p->draw_pos0 = p->draw_pos1 = p->pos;
    p->draw_yaw0 = p->draw_yaw1 = p->yaw;
}

void objects_tick(void)
{
    if (!R || !S || !S->n) return;
    if (R->tick == 1 && getenv("A2_TEST_OBJ")) {
        const char *k = strchr(getenv("A2_TEST_OBJ"), ',');
        test_place_player(atoi(getenv("A2_TEST_OBJ")), k ? atoi(k + 1) : 3);
    }
    for (int i = 0; i < S->n; i++) {                  /* samples no longer asked for are silenced (as race.c does) */
        if (g_snd_was[i] && !g_snd_on[i]) sfx_object_stop(OBJ_SND_KEY + i);
        g_snd_was[i] = g_snd_on[i];
        g_snd_on[i] = 0;
    }
    /* rceng_handle_objects 0x440850: the .omp objects come first in the object list; the bodies' handlers run as
     * their update callbacks, the traffic lights' (type 4) in ai.c */
    for (int i = 0; i < S->n; i++) {
        TrackObject *t = &S->obj[i];
        if (t->obj >= 0 || !ai_chunk_in_window(t->at)) continue;
        switch (t->type) {
        case OT_SOLID: objtype_solid(i); break;
        case OT_PYLON: objtype_knock_over(i, 4.0f, 0x4fb, 4.0f, SFX_PIONB1, SFX_PIONB3); break;
        case OT_BARRIER: objtype_knock_over(i, 8.0f, 0x400, 2.0f, SFX_PAAL, SFX_PAAL + 1); break;
        case OT_SPEED_CAMERA: objtype_knock_over(i, 10.0f, 0x400, 4.0f, SFX_PAAL, SFX_PAAL + 1); break;
        default: break;
        }
    }
}
