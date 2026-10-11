/* collide.c - see collide.h. The pseudocode is docs/PHYSICS.md 8; angles are ANGLE_UNITS (4096) per turn, the
 * rotations use the engine's SIN/COS tables like the original.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "collide.h"
#include "ai.h"
#include "effects.h"
#include "exetab.h"
#include "model.h"
#include "pad.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BOXES 200

typedef struct {                        /* 0x4c */
    Vec3 corner[4];                     /* local, axis aligned: (xmin,zmin) (xmax,zmin) (xmax,zmax) (xmin,zmax) */
    Vec3 centre;                        /* world */
    float radius;
    int owner;                          /* object, -1 for a rail */
    int angle;
    int is_object;
} BBox;

typedef struct { int active, type, obj1, obj2; Vec3 point, normal; } Collision;   /* g_collision 0x4e6760 */
typedef struct { int hit; Vec3 point; int edge; } BoxResult;                      /* g_bbox_result 0x4e6678 */

static Race *R;
static BBox g_boxes[MAX_BOXES];
static int g_nboxes;
static Collision g_coll;
static BoxResult g_res;
static float g_last_impulse;            /* 0x4e6788 */
static float g_crash_sound_ms = -1000;  /* 0x4d9e20 */
static int g_scrape_snd = -1;           /* 0x48b468 */
static int g_crash_samples[3][2];       /* 0x48b470 / 0x48b478 / 0x48b480 */

#define OBJ(o) (&R->obj[o])

static int ftol(float f) { return (int)f; }
static int round_away(float a) { return (int)(a > 0 ? ceilf(a) : floorf(a)); }

/* rot(v, a): x' = COS*x - SIN*z, z' = SIN*x + COS*z (vec_rot_y 0x448e30 keeps y, bbox_rot_vec 0x44ae10 sets 0) */
static Vec3 rot(Vec3 v, int a) { return v3(race_cos(a) * v.x - race_sin(a) * v.z, v.y, race_sin(a) * v.x + race_cos(a) * v.z); }
static Vec3 rot0(Vec3 v, int a) { Vec3 r = rot(v, a); r.y = 0; return r; }

void collide_init(Race *r)
{
    R = r;
    memset(&g_coll, 0, sizeof g_coll);
    g_crash_sound_ms = -1000;
    g_scrape_snd = -1;
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {
        static const uint32_t tab[3] = { 0x48b470, 0x48b478, 0x48b480 };
        for (int k = 0; k < 3; k++) {
            const uint8_t *p = pe_ptr(&pe, tab[k], 8);
            if (p) memcpy(g_crash_samples[k], p, 8);
        }
        pe_close(&pe);
    }
}

/* ---- boxes [8.2] ----------------------------------------------------------------------------------------------- */
static void bbox_make_own(int o, BBox *b)                             /* 0x449de0 */
{
    const Model *m = anim_model(OBJ(o)->anim, OBJ(o)->frame);
    Vec3 lo = m ? m->min : v3(-8.5f, 0, -22.5f), hi = m ? m->max : v3(8.5f, 0, 22.5f);
    b->corner[0] = v3(lo.x, 0, lo.z);
    b->corner[1] = v3(hi.x, 0, lo.z);
    b->corner[2] = v3(hi.x, 0, hi.z);
    b->corner[3] = v3(lo.x, 0, hi.z);
    b->centre = OBJ(o)->pos;
    b->radius = m ? m->radius : 0;
    b->angle = round_away(OBJ(o)->yaw);
    b->owner = o;
    b->is_object = 1;
}

static BBox *bbox_new(void)
{
    if (g_nboxes == MAX_BOXES) return NULL;                           /* "Max nb of bboxs reached!" */
    return &g_boxes[g_nboxes++];
}

static void bbox_add_rail(int chunk, int k)                           /* 0x449c10: a 1 m slab on the rail's -x side */
{
    const Rail *l = &R->track.rails[R->track.chunks[chunk].rails[k]];
    BBox *b = bbox_new();
    if (!b) return;
    b->corner[0] = v3(-10, 0, -1);
    b->corner[1] = v3(0, 0, -1);
    b->corner[2] = v3(0, 0, l->len + 1);
    b->corner[3] = v3(-10, 0, l->len + 1);
    b->centre = l->p;
    b->radius = 0;
    b->owner = -1;
    b->angle = round_away(l->yaw);
    b->is_object = 0;
}

static int chunk_distance(int a, int b)
{
    int d = abs(a - b), n = R->track.nchunks;
    return d > n / 2 ? n - d : d;
}

/* bbox_collect_static 0x44a010: the rails of chunks c-1..c+1 (only for a car that is not driven on its track, i.e.
 * the player or a crashed AI car), and the cars within 10 chunks */
static void bbox_collect_static(int o)
{
    Obj *c = OBJ(o);
    int n = R->track.nchunks;
    if (!c->no_vel_rotate)
        for (int d = -1; d <= 1; d++) {
            int ch = ((c->chunk + d) % n + n) % n;
            for (int k = 0; k < R->track.chunks[ch].nrails; k++) bbox_add_rail(ch, k);
        }
    for (int j = 0; j < R->nobj; j++) {
        /* cars that are off are left out (the original keeps them in its physics list; the dormant ADAC car would
         * then be an invisible block at chunk 0) */
        if (j == o || !R->obj[j].used || !R->obj[j].active || R->obj[j].physics != 1) continue;
        if (chunk_distance(c->chunk, R->obj[j].chunk) < 10) {
            BBox *b = bbox_new();
            if (b) bbox_make_own(j, b);
        }
    }
}

/* ---- the swept rectangle test [8.3] ---------------------------------------------------------------------------- */
static int bbox_lb_clip(float p, float q, float *tE, float *tL)       /* 0x44b2b0, Liang-Barsky */
{
    if (p > 0) { float r = q / p; if (r > *tL) return 0; if (r > *tE) *tE = r; }
    else if (p < 0) { float r = q / p; if (r < *tE) return 0; if (r < *tL) *tL = r; }
    else if (q > 0) return 0;
    return 1;
}

static int bbox_point_inside_edge(Vec3 P, Vec3 lo, Vec3 hi)          /* 0x44b390: the nearest edge, or -1 */
{
    float d[4] = { P.z - lo.z, hi.x - P.x, hi.z - P.z, P.x - lo.x };
    if (!(d[0] > 0 && d[1] > 0 && d[2] > 0 && d[3] > 0)) return -1;
    int e = -1;
    float best = 1e10f;
    for (int i = 0; i < 4; i++) if (d[i] < best) { best = d[i]; e = i; }
    return e;
}

static float bbox_seg_vs_rect(Vec3 P0, Vec3 P1, Vec3 lo, Vec3 hi, int *edge)   /* 0x44b480 */
{
    float dx = P1.x - P0.x, dz = P1.z - P0.z;
    if (dx == 0 && dz == 0) return 1;
    int e = bbox_point_inside_edge(P0, lo, hi);
    if (e != -1) {
        if (bbox_point_inside_edge(P1, lo, hi) == -1) return 1;
        *edge = e;
        return 0;
    }
    float tE = 0, tL = 1, last = 0;
    if (!bbox_lb_clip(dx, lo.x - P0.x, &tE, &tL)) return 1;
    if (tE != last) { *edge = 3; last = tE; }
    if (!bbox_lb_clip(-dx, P0.x - hi.x, &tE, &tL)) return 1;
    if (tE != last) { *edge = 1; last = tE; }
    if (!bbox_lb_clip(dz, lo.z - P0.z, &tE, &tL)) return 1;
    if (tE != last) { *edge = 0; last = tE; }
    if (!bbox_lb_clip(-dz, P0.z - hi.z, &tE, &tL)) return 1;
    if (tE != last) *edge = 2;
    return tE > 0 ? tE : 1;
}

/* B's corners in A's (axis aligned) frame */
static void corners_in_frame(const BBox *A, const BBox *B, Vec3 cB[4])
{
    Vec3 d = v3_sub(B->centre, A->centre);
    for (int i = 0; i < 4; i++) cB[i] = rot(v3_add(rot(B->corner[i], B->angle), d), -A->angle);
}

static int bbox_broadphase(const Vec3 cA[4], const Vec3 cB[4], Vec3 m)   /* 0x44ab40 */
{
    float x0 = 9999.9f, x1 = -9999.9f, z0 = 9999.9f, z1 = -9999.9f;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 2; k++) {
            float x = cB[i].x + (k ? m.x : 0), z = cB[i].z + (k ? m.z : 0);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (z < z0) z0 = z;
            if (z > z1) z1 = z;
        }
    x0 -= 1; x1 += 1; z0 -= 1; z1 += 1;
    return !(x1 < cA[0].x || x0 > cA[1].x || z1 < cA[0].z || z0 > cA[2].z);
}

/* bbox_sweep 0x44a520: B's corners moving by m against A's rectangle, in A's frame -> the contact fraction */
static float bbox_sweep(const BBox *A, const BBox *B, Vec3 m)
{
    Vec3 cB[4];
    corners_in_frame(A, B, cB);
    if (!bbox_broadphase(A->corner, cB, m)) return 1.0f;
    float min = 1e8f;
    int pt = -1, edge = 0;
    for (int i = 0; i < 4; i++) {                                     /* bbox_clip_corners 0x44b6f0 */
        int e = 0;
        float t = bbox_seg_vs_rect(cB[i], v3_add(cB[i], m), A->corner[0], A->corner[2], &e);
        if (t < min) { min = t; pt = i; edge = e; }
    }
    if (!(min < 1)) { pt = -1; edge = 0; }
    if (pt != -1) { g_res.hit = 1; g_res.point = B->corner[pt]; g_res.edge = edge; }
    return min;
}

static int bbox_overlap(const BBox *A, const BBox *B)                 /* 0x44acf0 */
{
    Vec3 cB[4];
    corners_in_frame(A, B, cB);
    for (int i = 0; i < 4; i++)
        if (cB[i].x > A->corner[0].x && cB[i].x < A->corner[1].x && cB[i].z > A->corner[0].z && cB[i].z < A->corner[2].z) {
            g_res.hit = 1; g_res.point = cB[i]; g_res.edge = 0;
            return 1;
        }
    return 0;
}

/* bbox_edge_normal 0x44b860: the outward normal of A's edge, turned by ang */
static Vec3 bbox_edge_normal(int e, const BBox *A, int ang)
{
    Vec3 d = v3_sub(A->corner[(e + 1) & 3], A->corner[e & 3]);
    float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    Vec3 u = l != 0 ? v3_scale(d, 1.0f / l) : d;                      /* "Division by zero" */
    Vec3 r = rot(u, ang);
    return v3(r.z, 0, -r.x);
}

/* collision_record 0x449840: A has the edge, B the corner; the normal is in obj1's frame */
static void collision_record(const BBox *A, const BBox *B)
{
    if (!g_res.hit) return;
    g_coll.active = 1;
    g_coll.point = g_res.point;
    if (!A->is_object) {                                              /* my corner hit a rail */
        g_coll.type = 0; g_coll.obj1 = B->owner; g_coll.obj2 = 0;
        g_coll.normal = bbox_edge_normal(g_res.edge, A, A->angle - B->angle);
    } else if (!B->is_object) {                                       /* a rail corner hit my rectangle */
        g_coll.type = 0; g_coll.obj1 = A->owner; g_coll.obj2 = 0;
        g_coll.normal = v3_scale(bbox_edge_normal(g_res.edge, A, 0), -1);
    } else {
        g_coll.type = 1; g_coll.obj1 = B->owner; g_coll.obj2 = A->owner;
        g_coll.normal = bbox_edge_normal(g_res.edge, A, A->angle - B->angle);
    }
}

float bbox_sweep_object(int obj, int *rotblk, float dt)
{
    Obj *o = OBJ(obj);
    float frac = 1;
    int rb = 0;
    BBox me;
    g_coll.active = 0;
    g_res.hit = 0;
    if (o->vel.x == 0 && o->vel.z == 0 && o->yawrate == 0) { *rotblk = 1; return 0; }
    g_nboxes = 0;
    bbox_collect_static(obj);
    bbox_make_own(obj, &me);
    for (int i = 0; i < g_nboxes; i++) {
        const BBox *b = &g_boxes[i];
        Vec3 m = v3_scale(rot0(o->vel, me.angle - b->angle), dt * 10);   /* my motion in b's frame */
        g_res.hit = 0;
        float t = bbox_sweep(b, &me, m);
        if (t < frac) { frac = t; collision_record(b, &me); }
        g_res.hit = 0;
        Vec3 m2 = v3_scale(o->vel, -(dt * 10));                       /* b against me, in my frame */
        t = bbox_sweep(&me, b, m2);
        if (t < frac) { frac = t; collision_record(&me, b); }
    }
    me.centre = v3_add(me.centre, v3_scale(rot0(o->vel, me.angle), frac * dt * 10));
    me.angle += round_away(dt * ANGLE_UNITS * o->yawrate / 6.283185f);   /* the full step's turn */
    if (o->yawrate != 0)
        for (int i = 0; i < g_nboxes; i++) {
            g_res.hit = 0;
            if (bbox_overlap(&g_boxes[i], &me)) rb = 1;
            g_res.hit = 0;
            if (bbox_overlap(&me, &g_boxes[i])) rb = 1;
        }
    *rotblk = rb;
    return frac;
}

/* ---- slide-out and responses [8.4, 8.5] ------------------------------------------------------------------------ */
int obj_slide_out(int obj, float dt, Vec3 *out, float frac, int *rotblk)
{
    if (!(frac == 0 && g_coll.active)) return 0;
    Obj *o = OBJ(obj);
    Collision saved = g_coll;
    Vec3 v0 = o->vel, n = g_coll.normal;
    float vn = v3_dot(o->vel, n);
    if (vn > 0) return 0;                                             /* moving away: no motion this step */
    o->vel = v3_add(o->vel, v3_scale(n, 10 - vn));                    /* out along the normal at 10 m/s */
    int rb2 = 0;
    float f = bbox_sweep_object(obj, &rb2, dt);
    if (rb2 == 1) *rotblk = 1;
    *out = v3_scale(o->vel, f);
    o->vel = v0;
    g_coll = saved;
    return 1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void obj_world_response(Collision *k)                          /* 0x449570: car against a rail */
{
    Obj *o = OBJ(k->obj1);
    Vec3 n = k->normal, r = v3_scale(k->point, 0.1f);
    Vec3 vp = v3_add(o->vel, v3(-o->yawrate * r.z, 0, o->yawrate * r.x));
    float vn = vp.x * n.x + vp.z * n.z, kk = r.x * n.z - r.z * n.x;
    float num = -vn * (1 + o->restitution);
    if (o->inv_mass == 0 && o->inv_inertia == 0) return;
    float j = o->inv_mass != 0 ? num / o->inv_mass : num;
    g_last_impulse = j;
    float dw = clampf(0.34f * o->inv_inertia * j * kk, -6, 6);
    o->vel = v3_add(o->vel, v3_scale(n, o->inv_mass * j));
    o->yawrate += dw;
    o->pitch_vel += 16 * o->inv_mass * n.z * j;
    o->roll_vel -= 16 * o->inv_mass * n.x * j;
    o->vel.y = o->inv_mass * j * 1.5f;                                /* a small hop */
}

static void obj_object_response(Collision *k)                         /* 0x448eb0: car against car */
{
    Obj *o1 = OBJ(k->obj1), *o2 = OBJ(k->obj2);
    int A1 = ftol(o1->yaw), A2 = ftol(o2->yaw);
    k->point = v3_scale(k->point, 0.1f);                              /* in place, as the original */
    Vec3 Nw = rot(k->normal, A1), r1 = rot(k->point, A1);
    Vec3 V1 = rot(o1->vel, A1), V2 = rot(o2->vel, A2);
    Vec3 d = v3_scale(v3_sub(o1->pos, o2->pos), 0.1f), r2 = v3(d.x + r1.x, 0, d.z + r1.z);
    Vec3 vp1 = v3_add(V1, v3(-o1->yawrate * r1.z, 0, o1->yawrate * r1.x));
    Vec3 vp2 = v3_add(V2, v3(-o2->yawrate * r2.z, 0, o2->yawrate * r2.x));
    float vn = v3_dot(v3_sub(vp1, vp2), Nw);
    float e = (o1->restitution + o2->restitution) * 0.5f;
    float num = -vn * (1 + e), den = o1->inv_mass + o2->inv_mass;
    float J = den != 0 ? num / den : num;
    g_last_impulse = J;
    float K1 = r1.x * Nw.z - r1.z * Nw.x, K2 = r2.x * Nw.z - r2.z * Nw.x;
    float dw1 = clampf(0.34f * o1->inv_inertia * J * K1, -6, 6), dw2 = clampf(0.34f * o2->inv_inertia * J * K2, -6, 6);
    o1->vel = v3_add(o1->vel, rot(v3_scale(Nw, o1->inv_mass * J), -A1));
    o2->vel = v3_sub(o2->vel, rot(v3_scale(Nw, o2->inv_mass * J), -A2));
    o1->yawrate += dw1;
    o2->yawrate -= dw2;
    o1->pitch_vel += 16 * o1->inv_mass * k->normal.z * J;
    o1->roll_vel -= 16 * o1->inv_mass * k->normal.x * J;
    Vec3 Nl2 = rot(Nw, -A2);
    o2->pitch_vel -= 16 * o2->inv_mass * Nl2.z * J;
    o2->roll_vel += 16 * o2->inv_mass * Nl2.x * J;
    o1->vel.y = o2->inv_mass * J * 1.5f;                              /* [quirk] both with o2's inverse mass */
    o2->vel.y = o2->inv_mass * J * 1.5f;
}

void coll_after_step(int obj)
{
    Obj *o = OBJ(obj);
    if (!g_coll.active) { o->in_collision = 0; return; }
    if (g_coll.type == 0) obj_world_response(&g_coll);                /* obj_resolve_collision 0x4497f0 */
    else obj_object_response(&g_coll);
    if (o->collide && !o->in_collision) o->collide(obj);              /* only when the contact begins */
    o->in_collision = 1;
    OBJ(g_coll.obj1)->in_collision = 1;
    if (g_coll.type == 1) OBJ(g_coll.obj2)->in_collision = 1;
}

/* ---- the player's damage, sound and sparks [8.7] --------------------------------------------------------------- */
static void plr_collision_sound(void)                                 /* 0x427cc0 */
{
    int J = ftol(g_last_impulse);
    float now = (float)R->tick * 1000.0f / RACE_HZ;
    if (ai_snd_play) {
        if (now - g_crash_sound_ms < 300) {
            if (J < 18000 && g_coll.type == 0 && (g_scrape_snd == -1 || !(ai_snd_playing && ai_snd_playing(g_scrape_snd))))
                g_scrape_snd = ai_snd_play(0x52);                     /* rail scrape */
        } else ai_snd_play(g_crash_samples[J < 18000 ? 0 : J < 25000 ? 1 : 2][rand() % 2]);
    }
    g_crash_sound_ms = now;
}

static void player_car_collision(void)                                /* 0x427de0 */
{
    plr_collision_sound();
    int J = ftol(g_last_impulse), d = 0;
    /* PORT: the pad rumbles with the impulse (the original only bumped a DEVICE 4 force-feedback stick, 0.2 s):
     * a scrape lightly, a damaging crash (18000 and up) hard and longer */
    if (J > 2000) {
        float s = (J - 2000) / 28000.0f;
        if (s > 1) s = 1;
        pad_rumble(0.2f + 0.8f * s, 120 + (int)(230 * s));
    }
    if (J >= 18000) {
        if (J > 35000) J = 35000;
        int t = ftol(J * 0.0028571428f), maxd = ai_difficulty() == 1 ? 12 : 6;
        d = ftol(maxd * 0.01f * t);
        if (d < 0) d = 0;
        if (d > maxd) d = maxd;
    }
    if (!R->game_event) {                                             /* plr_add_damage 0x428390 */
        R->damage += d;
        if (R->damage < 0) R->damage = 0;
        if (R->damage > 100) R->damage = 100;
    }
    /* player_spawn_crash_sparks 0x4281f0: at the contact point, outwards */
    Vec3 rw = rot(g_coll.point, ftol(OBJ(R->plr_obj)->yaw));
    rw.y = 0;
    float l = sqrtf(rw.x * rw.x + rw.z * rw.z);
    if (l > 0) fx_sparks(v3(R->plr_pos.x + rw.x, R->plr_pos.y + 3, R->plr_pos.z + rw.z), v3_scale(rw, 1.0f / l),
                         (float)R->speed_kmh, R->dt);
}

int plr_collision_cb(int obj)
{
    (void)obj;
    if (g_coll.active && (g_coll.obj1 == R->plr_obj || g_coll.obj2 == R->plr_obj)) player_car_collision();
    return 1;
}

/* the mesh deformation it starts (handle_mesh_damage 0x427190) is dormant: no .DMG files exist (PHYSICS.md 8.8) */
int mesh_damage_collision_func(int obj)
{
    (void)obj;
    if (!g_coll.active) return 1;
    if (g_coll.type == 1 ? (g_coll.obj1 == R->plr_obj || g_coll.obj2 == R->plr_obj) : g_coll.obj1 == R->plr_obj)
        player_car_collision();
    return 1;
}
