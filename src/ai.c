/* ai.c - see ai.h. Function names and addresses refer to spel.dat; the pseudocode is in docs/AI.md (sections in
 * brackets below). The original's globals are the statics of this file. Angles are ANGLE_UNITS (4096) per turn.
 *
 * Timing: the original calls its per-frame code once per drawn frame with the smoothed frame time; here once per
 * game tick of RACE_HZ, with dt() = the tick length (timer_frame_dt 0x441cd0 never returns more than 0.1 s).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "ai.h"
#include "collide.h"
#include "exetab.h"
#include "model.h"
#include "res.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28319f
#define FLT_BIG 3.4e38f                 /* 0x7f7fc99e, the source's literal 3.4e38 */
#define NAI 50                          /* init_ai(0x32) */
#define NCOPS 40                        /* cop_states_alloc(0x28) */
#define NCARLIST 250
#define NFSM 15
#define WP_MAXOBJ 10

void (*ai_snd_object)(int obj, int sample, int on);
int (*ai_snd_play)(int sample);
int (*ai_snd_playing)(int handle);

static Race *R;

static int ftol(float f) { return (int)f; }
static float dt(void) { return R->dt; }
#define OBJ(o) (&R->obj[o])
#define WP(w) (&R->track.wps[w])
#define CHUNK(c) (&R->track.chunks[c])

/* ---- structures [3] ------------------------------------------------------------------------------------------ */
typedef struct {                        /* t_ai_behavior, 0x2c */
    int lookahead;
    float wp_range, brake_speed, steer_gain, accel, top_speed, decel, rear_range, follow_range, front_range;
} AiBehavior;

typedef struct {                        /* t_ai_state, 0x6c */
    int waypoint, waypoint2, state, lane_change, scan_range, chosen_lane;
    float max_speed_allowed, chosen_speed;
    int behavior, collision;
    float collision_timer;
    int racer_state, racer_mode, max_lanes, finish_valid, cop, old_sample, sample_flag;
    float stuck_timer;
    int stuck_count;
    float speed_factor;
    int progress, last_chunk, laps;
} AiState;

typedef struct {                        /* t_cop, 0x20 (the old cop model) */
    int state, target, target_lane;
    float timer, chase_time, max_speed, max_lanes;
    int obj;
} Cop;

typedef struct {                        /* t_fsm, 0x60 */
    int object, state, target, laps;
    float timer, time_in_chunk, reverse_t;
    int twp, chunk, prev_chunk, lap_trigger;
    float speed_limit;
    int car;                            /* +0x30 */
    float steer, throttle, brake;       /* +0x34 -100..100, +0x38 0..100, +0x3c 0..100 */
    int coast, gear;                    /* +0x40, +0x44 */
    float skid_t, skid_yaw, skid_heading;
    int stopped;                        /* +0x58 */
    float skill;                        /* +0x5c */
} Fsm;

typedef struct { int from, to, expected; int (*cond)(int i); } Trans;
typedef struct { int object, chunk, waypoint, dir_fwd, dir_side; } WorldMapper;
typedef struct { int car, time, position, object, finished; } RacerInfo;   /* 0x485b20, 0x58 */
typedef struct { int active, object, type; } CarListEntry;
typedef struct { int dead_end, dead_range, front_obstacle; float front_range; int front_status;
                 int rear_obstacle; float rear_range; int rear_status; } LaneInfo;     /* t_lane_info, 0x20 */
enum { LANE_FREE, LANE_FASTER, LANE_SLOWER, LANE_BRAKE, LANE_STOP, LANE_GHOST };

/* ---- globals ------------------------------------------------------------------------------------------------- */
static int g_difficulty, g_dif_mod, g_ai_model, g_demo, g_visible_chunks, g_nr_laps, g_direction;   /* Game.ini */
static int g_startchunk, g_finish_chunk = -1, g_player_position, g_player_pos_frozen;
static float g_player_top_speed;        /* 0x4d9e68 */
static int g_traffic_same_side, g_traffic_oncoming;
static AiBehavior g_bh[16];
static AiState g_ai[NAI];
static char g_ai_used[NAI];
static int g_ai_last;
static Cop g_cops[NCOPS];
static char g_cop_used[NCOPS];
static int g_cop_last;
static Fsm g_fsm[NFSM];
static Trans g_racer_trans[25], g_cop_trans[25];
static WorldMapper g_wm[8];
static RacerInfo g_racers[3];
static CarListEntry g_car_list[NCARLIST];
static int g_car_model_used[7];
static int g_active_cops[20];
static int g_siren_count, g_cop_siren_active, g_siren_light_frame;
static float g_siren_light_ms;
static int g_player_ai = -1, g_player_autopilot;
static int g_adac_object = -1;
static int *g_wp_n, (*g_wp_obj)[WP_MAXOBJ];          /* waypoint +0x24 / +0x28 */
static int g_obstacle_list[WP_MAXOBJ + 1];           /* 0x4a5bd8 */
/* tables in spel.dat .data / .rdata */
static float g_rubber_tab[2][7][3][3];               /* 0x482258 {ahead, behind, gain} */
static int g_traffic_types[12][2];                   /* 0x484a48 {car type, behaviour} */
static int g_chase_samples[13], g_quit_samples[5], g_arrest_samples[3][6];
static const int g_arrest_count[3] = { 6, 5, 5 };
static float g_steer_c[16];                          /* the N*pi constants of init_ai at 0x47f130.. */
/* game actions (ev_main.cpp): the cop sequence (slot 11) and game over (slot 12) */
static int g_times_caught, g_cops_action, g_cop_chase_voice = -1, g_cop_quit_voice = -1;
static float g_cops_start_ms;
/* traffic lights: g_traffic_light_state 0x48c484 (0 red, 2 amber, 3 green) and gm_clock timer 1 */
static int g_light_state;
static float g_light_timer_end;

static float now_ms(void) { return (float)R->tick * 1000.0f / RACE_HZ; }

/* ---- road helpers (rd_*.cpp) --------------------------------------------------------------------------------- */
static int nchunks(void) { return R->track.nchunks; }

static int rceng_wrap_chunk(int c)                                    /* 0x4426e0: one wrap only */
{
    int n = nchunks();
    if (c >= n) c -= n;
    if (c < 0) c = n - abs(c);
    return c;
}

static int rceng_chunk_distance(int a, int b)                         /* 0x442700 */
{
    int d = abs(a - b), n = nchunks();
    return d > n / 2 ? n - d : d;
}

static int rceng_get_waypoint_index(int chunk, int lane, int side)    /* 0x442150 */
{
    const Chunk *c = CHUNK(chunk);
    int n = c->nwp[side ? 1 : 0];
    if (n <= 0) return -1;
    if (lane > n - 1) lane = n - 1;
    return c->wp[side ? 1 : 0][lane];
}

static int rceng_get_valid_lane(int chunk, int side, int lane)        /* 0x442730 */
{
    int n = CHUNK(chunk)->nwp[side ? 1 : 0];
    if (lane > n - 1) lane = n - 1;
    return lane < 0 ? 0 : lane;
}

/* rceng_set_object_to_chunk_angle 0x442230: yaw along the chunk (dir 1) or reversed (dir 0) */
static void rceng_set_object_to_chunk_angle(int o, int chunk, int dir)
{
    Vec3 f = CHUNK(chunk)->fwd;
    float l = sqrtf(f.x * f.x + f.z * f.z);
    float a = (atan2f(f.z / l, f.x / l) - 3.14159f * 0.5f) * ANGLE_UNITS / TWO_PI;
    OBJ(o)->yaw = dir == 1 ? a : a + ANGLE_UNITS * 0.5f;
    OBJ(o)->draw_yaw0 = OBJ(o)->draw_yaw1 = OBJ(o)->yaw;
}

/* calc_chunk_window 0x445d70: [lo, hi], plus [lo2, hi2] when the window wraps (returns 1) */
static int calc_chunk_window(int c, int r, int *lo, int *hi, int *lo2, int *hi2)
{
    int n = nchunks();
    *lo = c - r;
    *hi = c + r;
    if (*lo < 0) { *lo2 = 0; *hi2 = *hi % n; *lo += n - 1; *hi = n - 1; return 1; }   /* sic: n - 1 */
    if (*hi > n - 1) { *lo2 = 0; *hi2 = *hi % n; *hi = n - 1; return 1; }
    *lo2 = *hi2 = -1;
    return 0;
}

/* rceng_handle_objects 0x440850 / obj_move_all: objects outside the window are neither handled nor moved */
int ai_obj_in_window(int o)
{
    if (!R || R->plr_obj < 0 || o == R->plr_obj || OBJ(o)->always_update) return 1;
    return ai_chunk_in_window(OBJ(o)->chunk);
}

int ai_chunk_in_window(int c)
{
    if (!R || R->plr_obj < 0) return 1;
    int lo, hi, lo2, hi2;
    int wrapped = calc_chunk_window(OBJ(R->plr_obj)->chunk, 2 * g_visible_chunks, &lo, &hi, &lo2, &hi2);
    return (c >= lo && c <= hi) || (wrapped && c >= lo2 && c <= hi2);
}

/* ---- waypoint obstacle lists (rd_drmap.cpp) ------------------------------------------------------------------- */
static void waypoint_add_object(int w, int o)                         /* 0x4442a0: at most 10 */
{
    if (g_wp_n[w] == WP_MAXOBJ) return;
    g_wp_obj[w][g_wp_n[w]++] = o;
}

static void waypoint_remove_object(int w, int o)                      /* 0x4442d0 */
{
    int k = -1;
    if (g_wp_n[w] == 0) return;
    for (int i = 0; i < WP_MAXOBJ; i++) if (g_wp_obj[w][i] == o) { k = i; break; }
    if (k == -1) return;
    for (; k < WP_MAXOBJ - 1; k++) g_wp_obj[w][k] = g_wp_obj[w][k + 1];
    g_wp_obj[w][k] = -1;
    g_wp_n[w]--;
}

/* waypoint_get_obstacles 0x40c3a0: the list, -1 terminated, and its length */
static int waypoint_get_obstacles(int w, int *out)
{
    int i;
    for (i = 0; i < g_wp_n[w]; i++) out[i] = g_wp_obj[w][i];
    out[i] = -1;
    return g_wp_n[w];
}

/* drmap_find_closest_waypoint 0x443da0: nearest lane waypoint of the object's chunk on side 1, 0 or both (-1) */
static int drmap_find_closest_waypoint(int o, int side)
{
    const Chunk *c = CHUNK(OBJ(o)->chunk);
    Vec3 p = OBJ(o)->pos;
    int best = -1;
    float closest = FLT_BIG;
    for (int s = 1; s >= 0; s--) {
        if (side != s && side != -1) continue;
        for (int i = 0; i < c->nwp[s]; i++) {
            int w = c->wp[s][i];
            Vec3 d = v3_sub(WP(w)->pos, p);
            float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            if (dist < closest) { best = w; closest = dist; }
        }
    }
    return best;
}

void ai_wp_add(int o)
{
    int w = drmap_find_closest_waypoint(o, OBJ(o)->important ? -1 : OBJ(o)->side);
    if (w == -1) return;
    OBJ(o)->waypoint = w;
    waypoint_add_object(w, o);
}

void ai_wp_remove(int o)
{
    int w = OBJ(o)->waypoint;
    if (w < 0 || w >= R->track.nwaypoints) return;     /* the original reports "illegal waypoint" and goes on */
    waypoint_remove_object(w, o);
}

/* ---- sounds (sndfx, Car_cops.cpp) ------------------------------------------------------------------------------ */
static void sndfx_set_object_sample(int o, int sample)                /* 0x4289e0 */
{
    OBJ(o)->sample = sample;
    if (ai_snd_object) ai_snd_object(o, sample, 1);
}

static void sndfx_play_object_sample(int o, int on)                   /* 0x428bc0, every frame */
{
    if (ai_snd_object) ai_snd_object(o, OBJ(o)->sample, on);
}

static int sndfx_play(int sample) { return ai_snd_play ? ai_snd_play(sample) : -1; }
static int snd_playing(int h) { return h != -1 && ai_snd_playing && ai_snd_playing(h); }

/* ---- car list, add_car (Car_main.cpp) [9] ---------------------------------------------------------------------- */
static void car_list_add(int o, int type)                             /* 0x411120 */
{
    for (int i = 0; i < NCARLIST; i++)
        if (!g_car_list[i].active) { g_car_list[i].active = 1; g_car_list[i].object = o; g_car_list[i].type = type; return; }
}

int add_car(int car_type, int chunk, int lane, int side, int dir, int (*handler)(int o), int (*collide)(int o),
            int list_type)
{
    chunk = rceng_wrap_chunk(chunk);
    int w = rceng_get_waypoint_index(chunk, lane, side);
    Vec3 pos = w >= 0 ? WP(w)->pos : CHUNK(chunk)->pos;
    const CarType *t = &R->car_types[car_type];
    int o = obj_add(anim_find(t->anim), pos, 0);
    if (o < 0) return -1;
    Obj *c = OBJ(o);
    c->car_type = car_type;
    c->update = handler;
    c->collide = collide;
    obj_init_physics(c, t->mass, t->width, t->length, 0.2f);
    rceng_set_object_to_chunk_angle(o, chunk, dir);
    c->prev_chunk = c->chunk = chunk;
    c->side = side;
    if (list_type == 1 || list_type == 3 || list_type == 4) c->important = 1;
    c->waypoint = w;
    ai_wp_add(o);
    car_list_add(o, list_type);
    return o;
}

/* ---- AI core (ai_main.cpp) [4] ------------------------------------------------------------------------------- */
static AiBehavior *BH(int a) { return &g_bh[g_ai[a].behavior]; }

/* init_ai 0x405bc0: the behaviour table (computed by code in the original; the difficulty-scaled racer rows are
 * (float)((double)base * K) as the x87 code does) */
static void init_ai(void)
{
    static const struct { int look; float range, brake; int gain; float acc, top, dec, rear, follow, front; int k; } B[16] = {
        { 6, 2, 45, 0, 2, 50, 12.5f, 200, 200, 300, -1 },          /* 0 player on autopilot */
        { 5, 2, 15, 1, 3.75f, 25, 14, 200, 200, 300, -1 },
        { 8, 1, 11.5f, 2, 2.55f, 25, 13, 200, 200, 300, -1 },      /* 2, 3 traffic */
        { 4, 1.2f, 21, 3, 3.9f, 25, 14.5f, 200, 200, 300, -1 },
        { 4, 1.3f, 25, 4, 4.8f, 25, 15.8f, 200, 200, 300, -1 },
        { 5, 1.25f, 32, 5, 9, 40, 14, 200, 200, 400, -1 },
        { 4, 1.25f, 32, 6, 6.3f, 40, 16.2f, 125, 100, 150, -1 },
        { 7, 1.25f, 33, 8, 2.5f, 37, 15, 80, 75, 85, -1 },         /* 7..13 racers by car model */
        { 7, 1.25f, 47, 8, 4.1f, 52, 14.8f, 80, 75, 85, 1 },
        { 7, 1.25f, 49, 8, 4.7f, 55, 14.8f, 80, 75, 85, 0 },
        { 7, 1.25f, 62, 8, 7.5f, 68, 14.8f, 80, 75, 85, 2 },
        { 7, 1.25f, 72, 9, 9.5f, 80, 14.8f, 80, 75, 85, 3 },
        { 7, 1.25f, 80, 10, 9, 90, 14.8f, 80, 75, 85, 4 },
        { 7, 1.25f, 90, 0, 10.5f, 100, 14.8f, 80, 75, 85, 5 },
        { 6, 1.23f, 58, 7, 12, 70, 18, 115, 70, 80, -1 },          /* 14 cop */
        { 4, 1.2f, 19, 1, 3.5f, 24, 15, 200, 100, 120, -1 } };     /* 15 ADAC */
    float K[6] = { 0.98f, 0.96f, 0.94f, 0.92f, 0.90f, 0.88f };
    if (g_difficulty == 0) {
        static const uint32_t easy[6] = { 0x3f75c290, 0x3f68f5c2, 0x3f5eb852, 0x3f547ae2, 0x3f47ae14, 0x3f3d70a4 };
        memcpy(K, easy, sizeof K);
    }
    memset(g_ai, 0, sizeof g_ai);
    memset(g_ai_used, 0, sizeof g_ai_used);
    g_ai_last = 0;
    for (int b = 0; b < 16; b++) {
        AiBehavior *h = &g_bh[b];
        float k = B[b].k >= 0 ? K[B[b].k] : 1.0f;
        h->lookahead = B[b].look;
        h->wp_range = B[b].range;
        h->steer_gain = g_steer_c[B[b].gain] / 1024.0f;
        h->brake_speed = B[b].k >= 0 ? (float)((double)B[b].brake * k) : B[b].brake;
        h->accel = B[b].k >= 0 ? (float)((double)B[b].acc * k) : B[b].acc;
        h->top_speed = B[b].k >= 0 ? (float)((double)B[b].top * k) : B[b].top;
        h->decel = B[b].dec;
        h->rear_range = B[b].rear;
        h->follow_range = B[b].follow;
        h->front_range = B[b].front;
    }
}

static int tarray_get_new(char *used, int n, int *last)               /* T_ARRAY::get_new */
{
    for (int i = *last; i < n; i++) if (!used[i]) { used[i] = 1; *last = i; return i; }
    for (int i = 0; i < *last; i++) if (!used[i]) { used[i] = 1; *last = i; return i; }
    return -1;
}

/* trigger_find_start_position 0x429a20: the first chunk with the finish trigger 14 */
static int trigger_find_start_position(void)
{
    for (int c = 0; c < nchunks(); c++)
        if (CHUNK(c)->trigger[0] == 14 || CHUNK(c)->trigger[1] == 14) return rceng_wrap_chunk(c);
    return 0;
}

static void ai_apply_trigger(int a, int side, int trigger);
static void backtrack_speed_trigger(int o, int *speed_trig, int *lane_trig);
static void reset_cop(int o, int cop);

static int cop_new(int o)                                             /* 0x4026c0 */
{
    int i = tarray_get_new(g_cop_used, NCOPS, &g_cop_last);
    if (i < 0) return -1;
    g_cops[i].state = 0;
    g_cops[i].target = R->plr_obj;
    g_cops[i].obj = o;
    return i;
}

/* add_ai 0x406800 */
static void add_ai(int o, int behavior, int waypoint, int lane)
{
    int speed_trig = 8, lane_trig = 0;
    if (g_finish_chunk == -1) g_finish_chunk = trigger_find_start_position();
    int a = tarray_get_new(g_ai_used, NAI, &g_ai_last);
    if (a < 0) return;
    AiState *s = &g_ai[a];
    Obj *c = OBJ(o);
    s->speed_factor = 1.0f;
    if (c->side == 1) lane_trig = 0x12;
    if (c->side == 0) lane_trig = 0x28;
    s->behavior = behavior;
    s->waypoint = s->waypoint2 = waypoint;
    s->max_speed_allowed = behavior == 0 ? 0.0f : 500.0f;
    s->chosen_speed = g_bh[behavior].top_speed;
    s->chosen_lane = lane;
    s->scan_range = 10;
    s->state = s->lane_change = 0;
    s->collision = 0; s->collision_timer = 0; s->racer_state = 0;
    s->racer_mode = 1;
    s->finish_valid = 0;
    s->max_lanes = 2;
    c->ai = a;
    c->no_vel_rotate = 1;                                             /* obj +0x38 "on track" */
    if (behavior >= 2 && behavior <= 5) {
        backtrack_speed_trigger(o, &speed_trig, &lane_trig);
        ai_apply_trigger(a, c->side, speed_trig);
        ai_apply_trigger(a, c->side, lane_trig);
        s->speed_factor = (float)rand() * 3.05185e-05f * 0.2f - -0.9f;        /* 0.9 .. 1.1 */
    }
    if (behavior == 14) {
        s->cop = cop_new(o);
        s->max_speed_allowed = 0.0f;
        reset_cop(o, s->cop);
    } else s->cop = -1;
    s->old_sample = c->sample;
    s->sample_flag = 1;
    s->stuck_timer = 0; s->stuck_count = 0;
    s->progress = c->chunk - g_finish_chunk;
    s->last_chunk = c->chunk;
    s->laps = 0;
}

/* ai_set_lane 0x406bf0 */
static void ai_set_lane(int o, int lane)
{
    int speed_trig = 8, lane_trig = 0;
    Obj *c = OBJ(o);
    if (c->side == 1) lane_trig = 0x12;
    if (c->side == 0) lane_trig = 0x28;
    int a = c->ai, b = g_ai[a].behavior;
    g_ai[a].chosen_lane = lane;
    g_ai[a].max_speed_allowed = b == 0 ? 0.0f : g_bh[b].top_speed;     /* add_ai uses 500 here */
    c->no_vel_rotate = 1;
    if (b >= 2 && b <= 5) {
        backtrack_speed_trigger(o, &speed_trig, &lane_trig);
        ai_apply_trigger(a, c->side, speed_trig);
        ai_apply_trigger(a, c->side, lane_trig);
    }
}

/* switch_lane_right 0x406d10 / switch_lane_left 0x406d90: re-target the waypoint to the neighbouring lane through
 * the previous waypoint's diagonal link (+0x18 / +0x1c) */
static int switch_lane(int a, int left)
{
    AiState *s = &g_ai[a];
    int prev = WP(s->waypoint)->prev;
    if (prev == -1) return 0;
    int nb = left ? WP(prev)->next_outer : WP(prev)->next_inner;
    if (nb == -1 || s->waypoint == nb) return 0;
    s->waypoint = nb;
    s->lane_change = 0;
    return 1;
}

/* check_for_new_target_waypoint 0x406e10 */
static int check_for_new_target_waypoint(float range, int next_wp, Vec3 p, float interval)
{
    Vec3 d = v3_sub(WP(next_wp)->pos, p);
    float delta = 128.0f - interval;
    if (delta < 0.0f) delta *= -1.0f;
    if (delta > 8.0f) delta = 8.0f;
    float factor = 1.0f - (8.0f - delta) * 0.125f * -0.5f;
    range = range * factor;
    return interval * range > sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
}

/* the object's forward axis (instance matrix row 2): the yaw direction on the chunk's plane */
static Vec3 obj_forward(int o)
{
    Vec3 r, u, f;
    car_axes(OBJ(o), &r, &u, &f);
    return f;
}

/* calc_steering 0x406fd0: the yaw rate towards the waypoint */
static float calc_steering(int o, int w, int a)
{
    Vec3 d = v3_sub(WP(w)->pos, OBJ(o)->pos), f = obj_forward(o);
    float dl = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    return ((f.x / fl) * (d.z / dl) - (f.z / fl) * (d.x / dl)) * BH(a)->steer_gain;
}

/* calc_speed 0x407140: the change of speed towards the curve-reduced chosen speed */
static float calc_speed(int target, int a, int o)
{
    float cur = OBJ(o)->vel.z;
    AiState *s = &g_ai[a];
    const AiBehavior *B = BH(a);
    Vec3 d = v3_sub(WP(target)->pos, OBJ(o)->pos), f = obj_forward(o);
    float dl = sqrtf(d.y * d.y + d.z * d.z + d.x * d.x), fl = sqrtf(f.y * f.y + f.z * f.z + f.x * f.x);
    Vec3 n = v3(d.x / dl, d.y / dl, d.z / dl);
    f = v3(f.x / fl, f.y / fl, f.z / fl);
    float cross = f.x * n.z - f.z * n.x;
    if (cross < 0.0f) cross *= -1.0f;
    float desired = s->chosen_speed - B->brake_speed * cross * s->chosen_speed / B->top_speed;
    if (desired > s->max_speed_allowed) desired = s->max_speed_allowed;
    float v;
    if (desired == cur) v = desired;
    else if (desired > cur) {                                         /* only top speed stops the acceleration */
        v = dt() * B->accel + cur;
        if (v > s->speed_factor * B->top_speed) v = s->speed_factor * B->top_speed;
    } else if (cur <= 0.0f) v = desired;
    else {
        v = cur - dt() * B->decel;
        if (v < 0.0f) v = 0.0f;
    }
    return v - cur;
}

/* next_waypoint 0x4075f0: the fork selector (int)dt() % 2 is always 0 [quirk], so +0x1c is preferred */
static int next_waypoint(int w)
{
    int r = ftol(dt());
    const Waypoint *p = WP(w);
    if (p->next != -1) return p->next;
    if (r % 2 != 0) {
        if (p->next_inner != -1) return p->next_inner;
        if (p->next_outer != -1) return p->next_outer;
        return w;
    }
    if (p->next_outer != -1) return p->next_outer;
    if (p->next_inner != -1) return p->next_inner;
    return w;
}

static int chunk_is_ahead(int a, int b)                               /* 0x407660 */
{
    int n = nchunks();
    if (a > b && a - b < n / 2) return 1;
    if (a < b && b - a > n / 2) return 1;
    return 0;
}

/* handle_basic_ai 0x4076b0: steer to the next waypoint, speed from the one `lookahead` steps ahead */
static int handle_basic_ai(int o)
{
    Obj *c = OBJ(o);
    int a = c->ai, chunk = c->chunk;
    AiState *s = &g_ai[a];
    const AiBehavior *B = BH(a);
    if (chunk != s->last_chunk) {
        int d = rceng_chunk_distance(chunk, s->last_chunk);
        if (chunk_is_ahead(chunk, s->last_chunk)) s->progress += d; else s->progress -= d;
    }
    s->last_chunk = chunk;
    if (CHUNK(chunk)->trigger[0] == 0x2c || CHUNK(chunk)->trigger[1] == 0x2c) return 1;   /* EINDE_WEG */
    int cur = s->waypoint, next = next_waypoint(cur), target = cur;
    for (int i = B->lookahead; i > 0; i--) target = next_waypoint(target);
    if (c->vel.z > B->top_speed * 4.0f) c->vel.z = B->top_speed;
    c->vel.x = 0;
    c->vel.y = 0;
    c->vel.z += calc_speed(target, a, o);
    if (B->top_speed * 4.0f < c->vel.z) c->vel.z = B->top_speed;
    c->yawrate = calc_steering(o, next, a);
    if (c->vel.z < 1.5f || (o == R->plr_obj && R->game_event == 7)) c->yawrate = 0;
    Vec3 d = v3_sub(WP(s->waypoint)->pos, WP(next)->pos);
    float interval = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (check_for_new_target_waypoint(B->wp_range, next, c->pos, interval)) {
        s->waypoint = next;
        s->waypoint2 = next_waypoint(s->waypoint2);
    }
    return 1;
}

/* ai_apply_trigger 0x407ba0: speed limits, the traffic-light stop line, lane counts */
static void ai_apply_trigger(int a, int side, int trigger)
{
    switch (trigger) {
    case 4: g_ai[a].max_speed_allowed = 16.67f; break;
    case 5: g_ai[a].max_speed_allowed = 22.22f; break;
    case 6: g_ai[a].max_speed_allowed = 33.33f; break;
    case 8: g_ai[a].max_speed_allowed = 500.0f; break;
    case 10:
        if (g_light_state == 0) { g_ai[a].max_speed_allowed = 0.0f; g_ai[a].state = 1; }
        break;
    default: break;
    }
    if (side == 1 && trigger >= 0x12 && trigger <= 0x15) g_ai[a].max_lanes = trigger - 0x11;
    if (side == 0 && trigger >= 0x28 && trigger <= 0x2b) g_ai[a].max_lanes = trigger - 0x27;
}

static void ai_limit_lanes(int a, int side, int trigger)              /* 0x407de0: only lowers */
{
    if (side == 1 && trigger >= 0x12 && trigger <= 0x15 && g_ai[a].max_lanes > trigger - 0x11) g_ai[a].max_lanes = trigger - 0x11;
    if (side == 0 && trigger >= 0x28 && trigger <= 0x2b && g_ai[a].max_lanes > trigger - 0x27) g_ai[a].max_lanes = trigger - 0x27;
}

static void ai_check_triggers(int o)                                  /* 0x407fc0 */
{
    Obj *c = OBJ(o);
    ai_apply_trigger(c->ai, c->side, CHUNK(c->chunk)->trigger[0]);
    ai_apply_trigger(c->ai, c->side, CHUNK(c->chunk)->trigger[1]);
    ai_apply_trigger(c->ai, c->side, WP(c->waypoint)->flags2);
}

/* BACKTRACK_SPEED_TRIGGER 0x408080: the last speed and lane triggers before the object (no wrap around) */
static void backtrack_speed_trigger(int o, int *speed_trig, int *lane_trig)
{
    *speed_trig = 0;
    *lane_trig = 0;
    int c = OBJ(o)->chunk, step = g_direction ? 1 : -1, side = OBJ(o)->side;
    while (c <= nchunks() - 1 && c >= 0) {
        for (int t = 0; t < 2; t++) {
            int v = CHUNK(c)->trigger[t];
            if (v == 4 || v == 5 || v == 6 || v == 8) {
                if (*speed_trig == 0) { *speed_trig = v; if (*lane_trig != 0) return; }
            } else if (v >= 0x12 && v <= 0x15) {
                if (side == 1 && *lane_trig == 0) { *lane_trig = v; if (*speed_trig != 0) return; }
            } else if (v >= 0x28 && v <= 0x2b) {
                if (side == 0 && *lane_trig == 0) { *lane_trig = v; if (*speed_trig != 0) return; }
            }
        }
        c += step;
    }
    if (*speed_trig == 0) *speed_trig = 8;
    if (*lane_trig == 0) {
        if (side == 1) *lane_trig = 0x13;
        if (side == 0) *lane_trig = 0x29;
    }
}

/* reset_cop 0x402200 (old cop model): limits from the backtracked triggers */
static void reset_cop(int o, int cop)
{
    int speed_trig, lane_trig;
    if (cop < 0) return;
    backtrack_speed_trigger(o, &speed_trig, &lane_trig);
    switch (speed_trig) {
    case 4: g_cops[cop].max_speed = 16.67f; break;
    case 5: g_cops[cop].max_speed = 22.22f; break;
    case 6: g_cops[cop].max_speed = 33.33f; break;
    case 8: g_cops[cop].max_speed = g_player_top_speed + g_player_top_speed; break;
    default: break;                                                   /* "ILLEGAL SPEED TRIGGER!!!!" */
    }
    if (OBJ(o)->side == 1 && lane_trig >= 0x12 && lane_trig <= 0x15) g_cops[cop].max_lanes = (float)(lane_trig - 0x11);
    if (OBJ(o)->side == 0 && lane_trig >= 0x28 && lane_trig <= 0x2b) g_cops[cop].max_lanes = (float)(lane_trig - 0x27);
}

/* ai_find_waypoint_side_lane 0x402520: side and lane of a waypoint of the chunk; outputs untouched if absent */
static int find_waypoint_side_lane(int chunk, int w, int *side, int *lane)
{
    const Chunk *c = CHUNK(chunk);
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < c->nwp[s]; i++)
            if (c->wp[s][i] == w) { *side = s; *lane = i; return 1; }
    return 0;
}

static void ai_damp_velocity(int o)                                   /* 0x4083d0 */
{
    OBJ(o)->vel.x *= 0.95f;
    OBJ(o)->vel.y *= 0.95f;
    OBJ(o)->vel.z *= 0.95f;
}

/* ai_check_collision 0x4084b0: after a contact the car coasts for 0.5 s, then is put back in the lane of its
 * waypoint; after 5 frames of contact it backs off at 3 m/s, for at most 5 s (cumulative) */
static void ai_check_collision(int o)
{
    Obj *c = OBJ(o);
    AiState *s = &g_ai[c->ai];
    int side = 0, lane = 0;
    if (s->collision != 1) return;
    if (s->stuck_count == -1) {
        s->stuck_timer += dt();
        if (s->stuck_timer > 5.0f) {
            s->stuck_count = 0;
            c->vel = v3(0, 0, 0);
            c->yawrate = 0;
            find_waypoint_side_lane(c->chunk, c->waypoint, &side, &lane);
            ai_set_lane(o, lane);
        }
        return;
    }
    if (c->in_collision == 0) {
        s->collision_timer += dt();
        ai_damp_velocity(o);
        if (s->collision_timer > 0.5f) {
            c->no_vel_rotate = 1;
            s->collision = 0;
            s->collision_timer = 0;
            c->vel = v3(0, 0, 0);
            c->yawrate = 0;
            find_waypoint_side_lane(c->chunk, c->waypoint, &side, &lane);
            ai_set_lane(o, lane);
        }
        s->stuck_count = 0;
    } else {
        s->collision_timer = 0;
        if (++s->stuck_count > 5) { c->no_vel_rotate = 1; c->vel.z = -3.0f; s->stuck_count = -1; }
    }
}

/* ---- lanes (ai_lane.cpp) [5] --------------------------------------------------------------------------------- */
static const LaneInfo g_default_lane_info = { 0, -1, -1, -1.0f, 0, -1, -1.0f, 0 };     /* 0x482b58 */

/* the lane infos as change_lane allocates them; indices outside the array (pick_lane can ask for lane -1 or one
 * past the last) read the default record instead of the original's heap neighbours */
static const LaneInfo *LI(const LaneInfo *L, int n, int i) { return i >= 0 && i < n ? &L[i] : &g_default_lane_info; }

static float obj_dist(int a, Vec3 me)
{
    Vec3 p = OBJ(a)->pos;
    float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static int is_in_front(int other, int me)                             /* 0x404510 */
{
    Vec3 f = obj_forward(me);
    float d = (OBJ(other)->pos.x - OBJ(me)->pos.x) * f.x + (OBJ(other)->pos.z - OBJ(me)->pos.z) * f.z;
    return d < 0.0f ? 0 : 1;
}

static void find_dead_end(LaneInfo *li, int w, int range)             /* 0x4040b0 */
{
    int left = range;
    if (range == 0) { li->dead_end = 0; li->dead_range = -1; return; }
    do {
        if (w == -1) { li->dead_end = 1; li->dead_range = range - left; return; }
        const Waypoint *p = WP(w);
        w = p->next;
        if (w == -1) {
            w = p->next_outer;
            if (w == -1) { w = p->next_inner; if (w == -1) continue; }
        }
        left--;
    } while (left != 0);
    li->dead_end = 0;
    li->dead_range = -1;
}

/* find_obstacle_here 0x404150: nearest car ahead / behind on the lane's own waypoint. The comparison uses the
 * distance of the previous candidate (a stale variable, starting at 0) before the new one is computed [quirk] */
static void find_obstacle_here(LaneInfo *li, int w, int o, int my_lane)
{
    int front = -1, rear = -1, n = g_wp_n[w];
    float front_range = FLT_BIG, rear_range = FLT_BIG, dist = 0.0f;
    Vec3 me = OBJ(o)->pos;
    if (my_lane == 1 && n <= 1) goto out;                             /* only me ("isn't me!" is just reported) */
    for (int i = 0; i < n; i++) {
        int x = g_wp_obj[w][i];
        if (x == o && my_lane == 1) continue;
        if (is_in_front(x, o) == 1) {
            if (dist < front_range) { front = x; dist = obj_dist(x, me); front_range = dist; }
        } else {
            if (dist < rear_range) { rear = x; dist = obj_dist(x, me); rear_range = dist; }
        }
    }
out:
    li->front_obstacle = front; li->front_range = front_range;
    li->rear_obstacle = rear; li->rear_range = rear_range;
}

static int find_obstacle_in_wp(int *obstacle, float *range, int w, int o)   /* 0x404600: includes o itself */
{
    int best = -1;
    float best_d = FLT_BIG;
    Vec3 me = OBJ(o)->pos;
    for (int i = 0; i < g_wp_n[w]; i++) {
        int x = g_wp_obj[w][i];
        float d = obj_dist(x, me);
        if (d < best_d) { best_d = d; best = x; }
    }
    *obstacle = best;
    *range = best_d;
    return best != -1;
}

static void scan_lane(LaneInfo *li, int w, int range, int o, int my_lane)   /* 0x403f60 */
{
    find_dead_end(li, w, range);
    if (li->dead_end == 0 || (li->dead_end == 1 && li->dead_range > 0)) find_obstacle_here(li, w, o, my_lane);
    if (li->dead_end == 0 || (li->dead_end == 1 && li->dead_range > 1)) {
        int p = w, n = range;
        if (li->front_obstacle == -1) {
            if (li->dead_end == 1) n = li->dead_range;
            if (n != 0) do {
                p = WP(p)->next;
                if (p == -1) break;
                if (find_obstacle_in_wp(&li->front_obstacle, &li->front_range, p, o) == 1) break;
            } while (--n);
        }
        p = w;
        n = 2;
        if (li->rear_obstacle == -1) do {
            p = WP(p)->prev;
            if (p == -1) break;
            if (find_obstacle_in_wp(&li->rear_obstacle, &li->rear_range, p, o) == 1) break;
        } while (--n);
    }
}

static void analyse_lane(LaneInfo *li, int o)                         /* 0x404720 */
{
    Vec3 a = obj_forward(o);
    if (li->front_obstacle != -1) {
        Vec3 b = obj_forward(li->front_obstacle);
        if (b.x * a.x + b.z * a.z < 0.0f) li->front_status = LANE_GHOST;
        else li->front_status = OBJ(li->front_obstacle)->vel.z > OBJ(o)->vel.z ? LANE_FASTER : LANE_SLOWER;
    } else if (li->dead_end == 1) li->front_status = li->dead_range < 2 ? LANE_STOP : LANE_BRAKE;
    else li->front_status = LANE_FREE;
    if (li->rear_obstacle != -1) {
        Vec3 b = obj_forward(li->rear_obstacle);
        if (b.x * a.x + b.z * a.z < 0.0f) li->rear_status = 0;
        else li->rear_status = OBJ(li->rear_obstacle)->vel.z > OBJ(o)->vel.z ? 2 : 1;
    } else li->rear_status = 0;
}

/* pick_lane 0x404960: traffic keeps right and overtakes on the left */
static int pick_lane(int o, int n, const LaneInfo *L, int nl, int cur, int illegal)
{
    const AiBehavior *B = BH(OBJ(o)->ai);
#define REAR_OK(i) (LI(L, nl, i)->rear_status == 0 || B->rear_range < LI(L, nl, i)->rear_range)
#define FRONT_OK(i) (B->front_range <= LI(L, nl, i)->front_range)
#define FS(i) (LI(L, nl, i)->front_status)
    if (illegal == 1) {                                               /* "In illegal lane": move left */
        switch (FS(cur - 1)) {
        case 1: if (!FRONT_OK(cur - 1)) return cur;   /* fall through */
        case 0: return REAR_OK(cur - 1) ? cur - 1 : cur;
        case 2: if (!FRONT_OK(cur - 1)) return cur; return REAR_OK(cur - 1) ? cur - 1 : cur;
        case 3: return FS(cur) >= 3 ? cur : cur - 1;
        case 4: return FS(cur) >= 4 ? cur : cur - 1;
        case 5: return FS(cur) != 5 ? cur : cur - 2;                  /* sic */
        default: break;
        }
    }
    if (cur == n - 1) {                                               /* in the slowest lane */
        switch (FS(cur)) {
        case 0: case 1: return cur;
        case 2:
            if (cur == 0) return 0;
            switch (FS(cur - 1)) {
            case 1: if (!FRONT_OK(cur - 1)) return cur;   /* fall through */
            case 0: return REAR_OK(cur - 1) ? cur - 1 : cur;
            case 2:
                if (OBJ(LI(L, nl, cur - 1)->front_obstacle)->vel.z <= OBJ(LI(L, nl, cur)->front_obstacle)->vel.z) return cur;
                if (!FRONT_OK(cur - 1)) return cur;
                return REAR_OK(cur - 1) ? cur - 1 : cur;
            default: return cur;
            }
        case 3: case 4: case 5:
            if (cur == 0) return 0;
            switch (FS(cur - 1)) {
            case 1: if (!FRONT_OK(cur - 1)) return cur;   /* fall through */
            case 0: return REAR_OK(cur - 1) ? cur - 1 : cur;
            case 2: if (!FRONT_OK(cur - 1)) return cur; return REAR_OK(cur - 1) ? cur - 1 : cur;
            default: return cur;
            }
        default: return cur;
        }
    }
    switch (FS(cur)) {
    case 2: case 3: case 4: case 5:                                   /* try overtaking on the left */
        if (cur - 1 >= 0) {
            int s = FS(cur - 1);
            if ((s == 0 || (s == 1 && FRONT_OK(cur - 1))) && REAR_OK(cur - 1)) return cur - 1;
        }
        /* fall through: keep right */
    case 0: case 1:
        switch (FS(cur + 1)) {
        case 1: if (!FRONT_OK(cur + 1)) return cur;   /* fall through */
        case 0: return REAR_OK(cur + 1) ? cur + 1 : cur;
        default: return cur;
        }
    default: return cur;
    }
#undef REAR_OK
#undef FRONT_OK
#undef FS
}

/* race_lane 0x404ee0: a safe lane with the lowest front status, ties by the longer free range */
static int race_lane(int o, int n, const LaneInfo *L, int cur)
{
    const AiBehavior *B = BH(OBJ(o)->ai);
    int cand[12], nc = 0, nc2 = 0, best;
    for (int i = 0; i < n; i++) {
        float rs = (float)L[i].rear_status;
        if (rs == 0.0f) cand[nc++] = i;
        if (rs > 0.0f && B->rear_range < L[i].rear_range) cand[nc++] = i;
    }
    for (int k = 0; k < nc; k++) {
        int c = cand[k];
        float fs = (float)L[c].front_status;
        if (fs == 0.0f) cand[nc2++] = c;
        if (fs > 0.0f && fs < 3.0f && B->front_range < L[c].front_range) cand[nc2++] = c;
    }
    if (nc2 == 0) return cur;
    best = cand[nc2 - 1];
    for (int k = 0; k < nc2 - 1; k++) {
        int c = cand[k];
        if ((L[best].front_status == 1 || L[best].front_status == 2) && L[c].front_status == L[best].front_status
            && L[c].front_range > L[best].front_range) best = c;
        if (L[c].front_status < L[best].front_status) best = c;
    }
    return best;
}

/* cop_lane 0x4051f0: from the chased car's lane (if on my side), else the right-most; the last lane is never tried */
static int cop_lane(int o, int n, const LaneInfo *L)
{
    Obj *me = OBJ(o);
    int cop = g_ai[me->ai].cop, target = cop >= 0 ? g_cops[cop].target : R->plr_obj;
    int side = -1, lane = 0, best;
    find_waypoint_side_lane(OBJ(target)->chunk, OBJ(target)->waypoint, &side, &lane);
    best = side == me->side ? rceng_get_valid_lane(me->chunk, me->side, lane) : n - 1;
    for (int i = 0; i < n - 1; i++) {
        if ((L[best].front_status == 1 || L[best].front_status == 2) && L[i].front_status == L[best].front_status
            && L[i].front_range > L[best].front_range) best = i;
        if (L[i].front_status < L[best].front_status) best = i;
    }
    return best;
}

static float adjust_speed(int o, const LaneInfo *li, float speed)     /* 0x4055c0 */
{
    const AiBehavior *B = BH(OBJ(o)->ai);
    switch (li->front_status) {
    case LANE_FREE: return B->top_speed;
    case LANE_FASTER: case LANE_SLOWER:
        if (li->front_range < B->follow_range) return OBJ(li->front_obstacle)->vel.z * 0.75f;
        if (li->front_range < B->front_range) return OBJ(li->front_obstacle)->vel.z;
        return B->top_speed;
    case LANE_BRAKE: { float s = speed * 0.75f; return s < 2.0f ? 2.0f : s; }
    case LANE_STOP: return 2.0f;                                      /* "halting, returning 0" returns 2 */
    default: return B->top_speed;
    }
}

/* CHANGE_LANE 0x403660: scan every lane of the object's chunk and side, let the driver type choose one, step the
 * chosen lane one lane towards it and set the chosen speed. Driver types: 0 traffic, 1 racer, 2 cop, 4 dead,
 * 5..7 refuelling racers (3, the ADAC, has no caller) */
static void change_lane(int o, int driver, int lanes_allowed)
{
    Obj *c = OBJ(o);
    AiState *ai = &g_ai[c->ai];
    const Chunk *ch = CHUNK(c->chunk);
    int side = c->side ? 1 : 0, n = ch->nwp[side], n_other = ch->nwp[!side];
    const int *wps = ch->wp[side], *wps_other = ch->wp[!side];
    LaneInfo L[12];
    int lane = 0, i, chosen;                                          /* the original leaves lane uninitialised */
    if (n <= 0) return;
    for (i = 0; i < n; i++) L[i] = g_default_lane_info;
    for (i = 0; i < n; i++) if (c->waypoint == wps[i]) { lane = i; break; }
    if (lanes_allowed > n) lanes_allowed = n;
    if (i == n && c->waypoint != 0)                                   /* "Waypoint not found in current chunk!" */
        for (int k = 0; k < n_other; k++) if (c->waypoint == wps_other[k]) { lane = k; break; }
    if (lanes_allowed == -1) lanes_allowed = n;
    int to_scan = lane < lanes_allowed ? lanes_allowed : n;
    for (i = 0; i < n; i++) scan_lane(&L[i], wps[i], ai->scan_range, o, lane == i);
    for (i = 0; i < n; i++) analyse_lane(&L[i], o);
    switch (driver) {
    case 0: chosen = pick_lane(o, to_scan, L, n, lane, lane >= lanes_allowed); break;
    case 1: case 6: chosen = race_lane(o, to_scan < n ? to_scan : n, L, lane); break;
    case 2: chosen = cop_lane(o, to_scan < n ? to_scan : n, L); break;
    case 4: case 5: chosen = to_scan - 1; break;
    default: chosen = o; break;                                       /* sic: the object index (7, refuelling) */
    }
    if (chosen != ai->chosen_lane) {
        if (chosen < ai->chosen_lane && switch_lane(c->ai, 1) == 1) ai->chosen_lane--;
        if (chosen > ai->chosen_lane && switch_lane(c->ai, 0) == 1) ai->chosen_lane++;
        if (ai->chosen_lane < 0) ai->chosen_lane = 0;
        if (ai->chosen_lane > n - 1) ai->chosen_lane = n - 1;
    }
    if (ai->chosen_lane == lane) ai->chosen_speed = adjust_speed(o, &L[ai->chosen_lane], ai->chosen_speed);
    else {
        float s = ai->chosen_speed;
        float a = adjust_speed(o, LI(L, n, lane), s), b = adjust_speed(o, &L[ai->chosen_lane], s);
        ai->chosen_speed = (b + a) * 0.5f;
    }
    if (ai->cop == -1 && c->sample == 41) {
        sndfx_set_object_sample(o, ai->old_sample);
        if (c->sample != 41) ai->sample_flag = 1;
    }
}

/* ---- the car-control library for the FSM cars (0x40e9f0..0x410a10) [7.6] ------------------------------------- */
static float ai_rubberband_factor(int fsm);

/* ai_heading_diff 0x40fe90: the angle from the heading to a point, -2048..2048 (positive = to the left) */
static int ai_heading_diff(int o, Vec3 p)
{
    float dx = p.x - OBJ(o)->pos.x, dz = p.z - OBJ(o)->pos.z;
    if (dx > 0.0f && dx < 0.1f) dx = 0.1f;
    if (dx < 0.0f && dx > -0.1f) dx = -0.1f;
    float a = atanf(dz / dx) * 0.159155f * 4096.0f;
    a += dx > 0.0f ? 3072.0f : 1024.0f;
    a -= OBJ(o)->yaw;
    if (a > 2048.0f) a -= 4096.0f;
    if (a < -2048.0f) a += 4096.0f;
    return ftol(a);
}

/* ai_get_angle_to_point 0x401690: the same, 0..4096 (0 ahead, 2048 behind) */
static int ai_get_angle_to_point(int o, Vec3 p)
{
    float dz = p.z - OBJ(o)->pos.z, dx = p.x - OBJ(o)->pos.x;
    if (dx > 0.0f && dx < 0.1f) dx = 0.1f;
    if (dx < 0.0f && dx > -0.1f) dx = -0.1f;
    float a = atanf(dz / dx) * 0.15915494f * 4096.0f;
    a += dx > 0.0f ? 3072.0f : 1024.0f;
    a -= OBJ(o)->yaw;
    if (a > 4096.0f) a -= 4096.0f;
    if (a < 0.0f) a += 4096.0f;
    return ftol(a);
}

/* ai_gear_accel 0x40eae0: always the player's car (CARS.DAT of g_plr_car and its gearbox) [quirk] */
static float ai_gear_accel(int fsm, float *ratio)
{
    const Fsm *E = &g_fsm[fsm];
    const CarsDat *P = &R->cars[R->plr_car];
    float bonus = fsm >= 3 && E->state == 3 ? 0.1f : 0.0f;          /* a chasing cop */
    float f = ai_rubberband_factor(fsm);
    if (g_demo && f < 1.0f) f = 1.0f;
    int box = R->upg_gearbox != 0;
    float r = OBJ(E->object)->vel.z / (P->vmax[box][E->gear] * f);
    *ratio = r;
    float acc = (1.0f - r) * P->accel[box][E->gear] * f;
    if (acc < 0.0f) acc = 0.0f;
    return acc + bonus;
}

/* ai_drive 0x40ff90: throttle, brake and the automatic gearbox */
static void ai_drive(int fsm)
{
    Fsm *E = &g_fsm[fsm];
    Obj *o = OBJ(E->object);
    const CarsDat *P = &R->cars[R->plr_car];
    float thr = E->throttle, brk = E->brake, t = dt(), r;
    int gear = E->gear, maxgear = (R->upg_gearbox != 0) + 4;
    if (thr == 0.0f) {
        if (o->vel.z < 0.0f) { o->vel.z += P->engine_brake * t; if (o->vel.z > 0.0f) o->vel.z = 0; }
        else { o->vel.z -= P->engine_brake * t; if (o->vel.z < 0.0f) o->vel.z = 0; }
    }
    if (thr > 0.0f) E->stopped = 0;
    float v0 = o->vel.z, acc = ai_gear_accel(fsm, &r);
    if (o->vel.z > 1.0f) {
        if (r > P->shift_up && gear < maxgear) { E->gear++; acc = ai_gear_accel(fsm, &r); }
        if (r < P->shift_down && gear > 0) { E->gear--; acc = ai_gear_accel(fsm, &r); }
    }
    float rev = P->brake;
    if (rev > 0.0f) rev = -rev;
    if (E->coast) { acc = 0; rev = 0; }
    o->vel.z = v0 + (thr * acc + brk * rev) * t;
    if (brk > 0.0f) {
        if (fabsf(o->vel.z) < 0.4f && !E->stopped) { o->vel.z = 0; E->stopped = 1; }
        float d = o->vel.z * brk;
        if (d > 1000.0f) d = 1000.0f;
        o->pitch_vel -= d * t;
    }
    if (o->vel.z < -13.333f) o->vel.z = -13.333f;
}

static float wrap2pi(float a) { if (a > TWO_PI) return a - TWO_PI; if (a < 0.0f) return a + TWO_PI; return a; }

/* ai_steer_physics 0x410280: the new steering model for the FSM cars, with a skid mode for coasting cars */
static void ai_steer_physics(int fsm)
{
    Fsm *E = &g_fsm[fsm];
    Obj *o = OBJ(E->object);
    const CarsDat *P = &R->cars[R->plr_car];
    float t = dt(), k = 0;
    E->steer = -E->steer;                                             /* restored at the end */
    if (!E->coast) {
        k = o->yawrate * 10.0f;
        o->yawrate -= k * t;
        E->skid_yaw = 0; E->skid_heading = 0; E->skid_t = 0;
        k = o->vel.x * (R->upg_banden ? 10.0f : 5.0f);
        o->vel.x -= k * t;
    }
    float aspd = fabsf(o->vel.z);
    float grip = aspd < 2.0f && fabsf(o->vel.x) < 2.0f ? sinf(fabsf(3.14159f * o->vel.z * 0.25f)) * P->steer : P->steer;
    float a = wrap2pi(E->steer * 0.5f * TWO_PI * (1.0f / 360.0f));
    float U = (TWO_PI / 4096.0f) * t * (grip * E->steer + sinf(a) * o->vel.z * 50.0f);
    float D = wrap2pi(U);
    int iy = ftol(o->yaw) % 4096;
    if (iy < 0) iy += 4096;
    float heading = (float)iy * (1.0f / 4096.0f) * TWO_PI;
    if (!E->coast && E->skid_t == 0.0f) {
        float f = 10.0f - aspd * 0.15f;
        if (f < 2.5f) f = 2.5f;
        if (D > 0.0f && D < TWO_PI) {
            if (o->vel.z < 0.0f) o->yawrate += U * f; else o->yawrate -= U * f;
            if (o->vel.z < 10.0f && R->skid != 0) o->roll += t * P->roll_gain * E->steer;   /* sic: the player's skid */
            else o->roll -= t * P->roll_gain * E->steer;
        } else {
            if (o->vel.z < 0.0f) o->yawrate -= E->steer * -0.004f; else o->yawrate -= U * f;
            o->roll += t * P->roll_gain * E->steer;
            o->vel.z -= k * t * 0.5f;
        }
        float r = o->yawrate * t;
        float lat = o->vel.x * cosf(r) - sinf(r) * o->vel.z;
        o->vel.x = R->upg_banden ? lat : lat * 0.8f;
    } else {
        if (E->skid_heading == 0.0f) E->skid_heading = heading;
        E->skid_t += t;
        if (E->skid_t > 1.0f) E->skid_t = 1.0f;
        float v = sqrtf(o->vel.x * o->vel.x + o->vel.z * o->vel.z);
        if (v > 1.0f) {
            float y = sinf(v * 3.14159f * 0.004f) * (E->skid_t * E->steer * 0.18f) * (fsm < 3 ? 0.5f : 1.25f);
            o->yawrate = o->vel.z < 0.0f && R->stop_latch ? y : -y;
            E->skid_yaw += t * y;
            float m = fsm < 3 ? (E->steer != 0.0f ? 1.0f : 2.0f) : 3.0f;
            o->vel.x -= m * o->vel.x * t;
            o->vel.z -= m * o->vel.z * t;
        } else { o->vel.x = 0; o->vel.z = 0; o->yawrate = 0; }
    }
    E->steer = -E->steer;
}

static void car_stop_damp(int o)                                      /* 0x40fba0 */
{
    Obj *c = OBJ(o);
    c->vel.x -= c->vel.x * 10 * dt();
    c->vel.z -= c->vel.z * 10 * dt();
    c->yawrate -= c->yawrate * 10 * dt();
}

static void body_springs(int o) { car_body_height(o); car_body_pitch(o); car_body_roll(o); }

static void ai_set_steering(int fsm, Vec3 target)                     /* 0x401230 */
{
    int a = ai_heading_diff(g_fsm[fsm].object, target);
    g_fsm[fsm].steer = a > 200 ? 100.0f : a < -200 ? -100.0f : (float)a * 0.5f;
}

/* ai_speed_control 0x4012c0: v = 70 - |bend|/15; racers brake above it, cops are set to it */
static void ai_speed_control(int fsm, Vec3 target)
{
    Fsm *E = &g_fsm[fsm];
    int a = abs(ai_heading_diff(E->object, target));
    if (a > 1000) a = 500;
    int v = 70 - a / 15;
    if ((float)v > OBJ(E->object)->vel.z) {
        E->throttle -= dt() * -75.0f;
        if (E->throttle > 100.0f) E->throttle = 100.0f;
        E->brake = 0.0f;
    } else if (fsm < 3) {
        E->brake += 400.0f * dt();
        if (E->brake > 100.0f) E->brake = 100.0f;
    } else OBJ(E->object)->vel.z = (float)v;
}

static void CalculateStearing(int fsm, Vec3 target)                   /* 0x4013c0 */
{
    int o = g_fsm[fsm].object;
    if (OBJ(o)->yaw < 0.0f) OBJ(o)->yaw -= -4096.0f;
    if (!(OBJ(o)->yaw < 4096.0f)) OBJ(o)->yaw -= 4096.0f;
    ai_drive(fsm);
    ai_set_steering(fsm, target);
    ai_steer_physics(fsm);
    car_body_roll(o);
    car_body_pitch(o);
    car_body_height(o);
}

/* ai_rubberband_factor 0x4014e0: the racers' top-speed factor from their progress against the player's */
static float ai_rubberband_factor(int fsm)
{
    static const float A[3] = { 0.95f, 0.92f, 0.90f }, Bf[3] = { 1.02f, 1.01f, 1.00f };
    static const float C[3] = { 1.00f, 0.98f, 0.95f }, D[3] = { 1.15f, 1.10f, 1.05f };
    float f;
    if (fsm >= 3) f = 1.0f;
    else {
        float d = (float)(g_ai[OBJ(g_racers[fsm].object)->ai].progress - R->progress);
        const float *T = g_rubber_tab[g_difficulty ? 1 : 0][R->etappe % 7][fsm];
        if (d > 0.0f) {
            if (d > 50.0f) d = 50.0f;
            f = d > T[0] ? 1.0f - d * T[2] * 0.02f : g_difficulty == 0 ? A[fsm] : Bf[fsm];
        } else {
            if (d < -50.0f) d = -50.0f;
            f = d < T[1] ? 1.0f - d * T[2] * 0.02f : g_difficulty == 0 ? C[fsm] : D[fsm];
        }
    }
    if (f < 0.2f) f = 0.2f;
    if (f > 2.0f) f = 2.0f;
    return f;
}

/* ---- world mapper and ScanPath (ai_wmap.cpp, ai_path.cpp) [7.1, 7.2] ------------------------------------------ */
static int ai_wmap_add(int o)                                         /* 0x40be70 */
{
    int i;
    for (i = 0; i < 8; i++) if (g_wm[i].object == o) return -2;
    for (i = 0; i < 8 && g_wm[i].object >= 0; i++) ;
    if (i == 8) return -1;
    g_wm[i].object = o;
    g_wm[i].chunk = OBJ(o)->chunk;
    g_wm[i].dir_fwd = 1;
    g_wm[i].dir_side = 1;
    g_wm[i].waypoint = drmap_find_closest_waypoint(o, 1);
    return i;
}

static void ai_wmap_remove(int h)                                     /* 0x40bf20 */
{
    if (h >= 8 || h < 0) return;
    g_wm[h].object = g_wm[h].chunk = g_wm[h].waypoint = -1;
    g_wm[h].dir_fwd = g_wm[h].dir_side = 0;
}

/* SIDESWITCH 0x40bf90: the next waypoint along link dir (0 +0x18, 1 +0x14 straight on, 2 +0x1c, 3 +0x20 back).
 * The mapper's direction fields are only ever 1 (or 0 when free), so only the forward / normal-side case of the
 * original runs; its other three cases (and ai_wmap_closest_waypoint 0x40b970 they use) are unreachable. */
static int ai_wmap_sideswitch(int h, int w, int dir)
{
    if (w < 0 || g_wm[h].dir_side != 1 || g_wm[h].dir_fwd != 1) return -1;
    switch (dir) {
    case 0: return WP(w)->next_inner;
    case 1: return WP(w)->next;
    case 2: return WP(w)->next_outer;
    case 3: return WP(w)->prev;
    default: return -1;
    }
}

static int chunk_find_lane_of_waypoint(int chunk, int w)              /* 0x40c440 */
{
    const Chunk *c = CHUNK(chunk);
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < c->nwp[s]; i++) if (c->wp[s][i] == w) return i;
    return -1;
}

/* ScanPath 0x409670: depth-first search for `depth` steps over the links, avoiding waypoints with cars on them;
 * returns path[1] and the positions of path[2] (near, the steering target) and path[4] (far, the speed target) */
static int ScanPath(int fsm, int wm, int depth, int wp, Vec3 *near, Vec3 *far)
{
    struct { unsigned char tried[3]; int wp; } st[26];
    const Fsm *E = &g_fsm[fsm];
    int n = 0, adv = 0, o0 = 1, o1 = 2, o2 = 0, next, i;
    if (depth > 25) return -2;
    for (i = 0; i < depth; i++) { st[i].tried[0] = st[i].tried[1] = st[i].tried[2] = 0; st[i].wp = -1; }
    int chunk_rec = OBJ(E->object)->chunk;
    if (depth > 0) {
        for (;;) {
            st[n].wp = wp;
            if (E->state == 4) {                                      /* the cop moves towards its target's lane */
                int a = chunk_find_lane_of_waypoint(OBJ(E->target)->chunk, OBJ(E->target)->waypoint);
                int b = chunk_find_lane_of_waypoint(OBJ(E->object)->chunk, OBJ(E->object)->waypoint);
                if (a > b) { o0 = 0; o1 = 1; o2 = 2; } else if (a < b) { o0 = 2; o1 = 1; o2 = 0; } else { o0 = 1; o1 = 2; o2 = 0; }
            } else { o0 = 1; o1 = 2; o2 = 0; }
            if (st[n].tried[0]) next = -1;
            else { next = ai_wmap_sideswitch(wm, wp, o0); st[n].tried[0] = 1; }
            if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list) != 0) next = -1;
            if (next >= 0) {
                wp = next;
                n++;
                st[n].tried[0] = st[n].tried[1] = st[n].tried[2] = 0;
            } else {
                for (;;) {
                    if (st[n].tried[1]) next = -1;
                    else { next = ai_wmap_sideswitch(wm, wp, o1); st[n].tried[1] = 1; }
                    if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list) != 0) next = -1;
                    if (next < 0) {
                        if (st[n].tried[2]) next = -1;
                        else { next = ai_wmap_sideswitch(wm, wp, o2); st[n].tried[2] = 1; }
                        if (next >= 0 && waypoint_get_obstacles(next, g_obstacle_list) != 0) next = -1;
                    }
                    if (next >= 0) {
                        n++;
                        adv = 1;
                        st[n].tried[0] = st[n].tried[1] = st[n].tried[2] = 0;
                        wp = next;
                        st[n].wp = next;
                    } else {
                        if (n == 0) goto fallback;
                        wp = st[n - 1].wp;
                        n--;
                    }
                    if (adv) break;
                }
                adv = 0;
            }
            if (n >= depth) break;
        }
    }
    if (st[2].wp < 0 || st[4].wp < 0 || st[1].wp < 0) return -1;     /* "Illegal waypoint in het laatste stukkie" */
    *near = WP(st[2].wp)->pos;
    *far = WP(st[4].wp)->pos;
    return st[1].wp;

fallback: {                                                           /* no free path: two steps ignoring cars */
    int w0 = st[0].wp, w1 = ai_wmap_sideswitch(wm, w0, o0), w2;
    if (w1 < 0) w1 = ai_wmap_sideswitch(wm, w0, o1);
    if (w1 < 0) w1 = ai_wmap_sideswitch(wm, w0, o2);
    if (w1 < 0) { if (w0 > 0) *near = WP(w0)->pos; return -1; }
    Vec3 p1 = WP(w1)->pos;
    w2 = ai_wmap_sideswitch(wm, w1, o0);
    if (w2 < 0) w2 = ai_wmap_sideswitch(wm, w1, o1);
    if (w2 < 0) w2 = ai_wmap_sideswitch(wm, w1, o2);
    if (w2 <= 0) { *near = p1; return -1; }
    *near = WP(w2)->pos;
    if (CHUNK(chunk_rec)->nwp[1] == 1) {
        /* single lane: the target is moved sideways, with cos/sin of 1024 radians and the y component [quirk] */
        double cs = cos(1024.0), sn = sin(1024.0);
        float dx = (near->x - p1.x) * 0.46511626f, dy = (near->y - p1.y) * 0.46511626f;
        float rx = (float)(dx * cs - dy * sn), ry = (float)(dy * cs + dx * sn);
        near->x += rx;
        near->z += ry;
        far->x = near->x;
        far->z = near->z;
    }
    return w2;
    }
}

static int ai_slower_car_ahead(int wp, int o)                         /* 0x409c10 */
{
    int n = waypoint_get_obstacles(wp, g_obstacle_list);
    for (int i = 0; i < n; i++) {
        int x = g_obstacle_list[i];
        if (x == o) continue;
        int a = ai_get_angle_to_point(o, OBJ(x)->pos);
        if (a >= 0x400 && a <= 0xc00) continue;
        if (OBJ(o)->vel.z > OBJ(x)->vel.z) return 1;
    }
    return 0;
}

/* ai_heading_with_road 0x410a10: 1 when the car points along the driving direction of its chunk */
static int ai_heading_with_road(int o)
{
    int w = rceng_get_waypoint_index(OBJ(o)->chunk, 0, g_direction == 0 ? 1 : 0);
    if (w < 0) return 1;
    int n = next_waypoint(w);
    Vec3 f = obj_forward(o);
    return (WP(n)->pos.x - WP(w)->pos.x) * f.x + (WP(n)->pos.z - WP(w)->pos.z) * f.z < 0.0f ? 0 : 1;
}

/* ai_get_drive_direction 0x40a100: 1 along the waypoint links, -1 against them */
static int ai_get_drive_direction(int o, int w)
{
    int rev = 0, l;
    if (w < 0) return 1;
    if ((l = WP(w)->next) >= 0) ;
    else if ((l = WP(w)->next_outer) >= 0) ;
    else if ((l = WP(w)->next_inner) >= 0) ;
    else if ((l = WP(w)->prev) >= 0) rev = 1;
    else return 1;
    int a = ai_get_angle_to_point(o, WP(l)->pos);
    if (rev) { a -= 0x800; if (a < 0) a += 0x1000; }
    return a >= 0x400 && a <= 0xc00 ? -1 : 1;
}

/* ---- police: sirens, voices (Car_cops.cpp, ev_cops.cpp) [8] --------------------------------------------------- */
/* cop_siren 0x40c640: the siren sample (40) instead of the engine, the count of sirens and the active-cop list
 * (the radar's cop dots). The blinking siren-light texture copy (txt_blit_anim 14) is not done here. */
static void cop_siren(int o, int on)
{
    int a = OBJ(o)->ai;
    if (on) {
        if (OBJ(o)->sample != 40) { if (a >= 0) g_ai[a].old_sample = OBJ(o)->sample; sndfx_set_object_sample(o, 40); }
        g_siren_count++;
        int k, f = -1;
        for (k = 0; k < 20 && g_active_cops[k] != o; k++) ;
        if (k == 20) {
            for (k = 0; k < 20; k++) if (g_active_cops[k] == -1) { f = k; break; }
            if (f != -1) g_active_cops[f] = o;
        }
    } else {
        if (OBJ(o)->sample == 40 && a >= 0) sndfx_set_object_sample(o, g_ai[a].old_sample);
        if (--g_siren_count < 0) g_siren_count = 0;
        if (g_siren_count == 0) g_siren_light_frame = 0;
    }
    g_cop_siren_active = g_siren_count != 0;
}

static void cop_siren_light_update(void)                              /* 0x40c860: every 200 ms */
{
    if (!g_cop_siren_active) return;
    if (now_ms() - g_siren_light_ms >= 200) { g_siren_light_ms = now_ms(); g_siren_light_frame = g_siren_light_frame == 0; }
}

static void cop_voice(int target, int *handle, const int *samples, int n)   /* 0x4150f0 / 0x415170 */
{
    if (!ai_snd_play) return;
    if (target != R->plr_obj && rand() % 3 != 0) return;
    if (*handle != -1 && snd_playing(*handle)) return;
    *handle = sndfx_play(samples[rand() % n]);
}

/* ---- game actions: the arrest (cops_init 0x415300 / cops_handle 0x415420 / cops_exit 0x415490) and game over --- */
static void cops_init(void)
{
    g_times_caught++;
    if (g_times_caught == 1) R->hud_message = 49;                     /* EERSTE WAARSCHUWING */
    else if (g_times_caught == 2) R->hud_message = 50;                /* LAATSTE WAARSCHUWING */
    if (ai_snd_play && g_times_caught >= 1 && g_times_caught <= 3)
        sndfx_play(g_arrest_samples[g_times_caught - 1][rand() % g_arrest_count[g_times_caught - 1]]);
    g_cops_start_ms = now_ms();
}

void ai_game_action_start(int action)
{
    R->game_event = action;
    switch (action) {
    case 0: R->event_running = 0; R->control = 1; R->race_started = 1; break;
    case 6: R->event_running = 1; break;
    case 8: R->event_running = 1; g_cops_action = 1; cops_init(); break;
    default: break;
    }
}

static void game_actions_run(void)                                    /* handle_game_actions 0x4168c0, the cops slot */
{
    if (!g_cops_action) return;
    int done;
    if (R->game_event != 8) done = 1;
    else if (g_times_caught >= 3) { R->game_over_reason = 2; ai_game_action_start(6); done = 1; }   /* GEARRESTEERD */
    else done = now_ms() - g_cops_start_ms >= 4000;                   /* a 4 s stop */
    if (!done) return;
    g_cops_action = 0;
    if (R->game_event == 8) ai_game_action_start(0);                  /* cops_exit */
}

/* ---- FSM conditions (ai_cfun.cpp) [6] ------------------------------------------------------------------------- */
static float dist2(int a, int b)
{
    float dx = OBJ(a)->pos.x - OBJ(b)->pos.x, dz = OBJ(a)->pos.z - OBJ(b)->pos.z;
    return dx * dx + dz * dz;
}

static int cfun_player_behind_leader(int i)                           /* 0x401780 */
{
    Fsm *E = &g_fsm[i];
    if (E->timer > 1.0f) { E->timer = 0; return 0; }
    if (dist2(E->object, R->plr_obj) < 10000.0f) {
        int a = ai_get_angle_to_point(E->object, OBJ(R->plr_obj)->pos);
        if (a > 1500 && a < 2500) {
            E->target = R->plr_obj;
            if (g_racers[i].position == 0) return 1;
        }
    }
    return 0;
}

static int cfun_random_block(int i)                                   /* 0x4018b0 */
{
    if (g_racers[i].position != 0) return 0;
    if (OBJ(g_fsm[i].object)->vel.z < 30.0f) return 0;
    float p = dt() * 50.0f;
    int r = rand() % 100;
    if ((float)r < p && g_fsm[i].timer < 1.0f) return 1;
    g_fsm[i].timer = 0;
    return 0;
}

static int cfun_target_behind_cop(int i)                              /* 0x401960 */
{
    int a = ai_get_angle_to_point(g_fsm[i].object, OBJ(g_fsm[i].target)->pos);
    return a > 1700 && a < 2300;
}

static int cfun_can_arrest(int i)                                     /* 0x4019f0 */
{
    int tgt = g_fsm[i].target;
    if (tgt == R->plr_obj) { if (R->game_event == 3) return 0; }
    else {
        int k;
        for (k = 0; k < 3 && g_fsm[k].object != tgt; k++) ;
        if (k < 3 && g_fsm[k].state == 9) return 0;
    }
    return fabsf(OBJ(tgt)->vel.z) + fabsf(OBJ(tgt)->vel.x) < 7.0f && dist2(g_fsm[i].object, tgt) < 5000.0f;
}

static int cfun_wait_4s(int i)                                        /* 0x401b00 */
{
    if (g_fsm[i].timer < 4.0f) { g_fsm[i].timer += dt(); return 0; }
    g_fsm[i].timer = 0;
    cop_siren(g_fsm[i].object, 0);                                    /* also for racers */
    return 1;
}

static int cfun_racer_too_far(int i)                                  /* 0x401b60 (racers only in the table) */
{
    static const float thr[4] = { 2.0e7f, 2.0e7f, 1.75e7f, 1.5e7f };
    int pos = g_racers[i].position;
    if (dist2(g_fsm[i].object, R->plr_obj) > thr[pos & 3]) {
        if (pos < g_player_position) return 2;                        /* racer ahead of the player */
        return g_difficulty ? 3 : 1;
    }
    return 0;
}

static int cfun_racer_near_player(int i) { return dist2(g_fsm[i].object, R->plr_obj) < 1.5e7f; }   /* 0x401cf0 */
static int cfun_target_not_forward(int i) { return ai_heading_with_road(g_fsm[i].target) == 0; }   /* 0x401d70 */

static int cfun_target_forward_near(int i)                            /* 0x401d90 */
{
    int t = g_fsm[i].target;
    if (!ai_heading_with_road(t)) return 0;
    if (rceng_chunk_distance(OBJ(g_fsm[i].object)->chunk, OBJ(t)->chunk) >= 2) return 0;
    return OBJ(t)->vel.z > 0.0f;
}

static int cfun_give_up(int i)                                        /* 0x401e20: 30 s (easy) / 60 s (hard) */
{
    Fsm *E = &g_fsm[i];
    float limit = g_difficulty ? 60.0f : 30.0f;
    E->timer += dt();
    if (E->timer > limit) {
        E->timer = 0;
        cop_siren(E->object, 0);
        cop_voice(E->target, &g_cop_quit_voice, g_quit_samples, 5);
        return 1;
    }
    if (rceng_chunk_distance(OBJ(E->object)->chunk, OBJ(E->target)->chunk) > 50) {
        cop_voice(E->target, &g_cop_quit_voice, g_quit_samples, 5);  /* the siren stays on */
        return 1;
    }
    return 0;
}

static int cfun_stopped(int i)                                        /* 0x401ef0 */
{
    Obj *o = OBJ(g_fsm[i].object);
    if (o->vel.z > 5.0f) return 0;
    o->vel.z = 0; o->vel.x = 0; o->yawrate = 0;
    return 1;
}

static int cfun_target_other_side(int i)                              /* 0x401f60 */
{
    int t = g_fsm[i].target;
    return ai_get_drive_direction(t, OBJ(t)->waypoint) == -1 && ai_heading_with_road(t);
}

static int cfun_target_reversing(int i) { return OBJ(g_fsm[i].target)->vel.z < 0.0f; }   /* 0x401fb0 */

static int cfun_cop_stuck(int i)                                      /* 0x401ff0: practically never fires [quirk] */
{
    if (g_fsm[i].time_in_chunk > 2.0f) return OBJ(g_fsm[i].target)->vel.z > 10.0f;
    g_fsm[i].time_in_chunk = 0;
    return 0;
}

static int cfun_stuck_3s(int i) { return g_fsm[i].time_in_chunk > 3.0f; }   /* 0x402050 */

static int cfun_target_back_on_side(int i)                            /* 0x402080 */
{
    int t = g_fsm[i].target;
    return ai_heading_with_road(t) && ai_get_drive_direction(t, OBJ(t)->waypoint) == 1;
}

static int return_true(int i) { (void)i; return 1; }                  /* 0x434140 */

/* ---- FSM framework (ai_fsm.cpp) [6] --------------------------------------------------------------------------- */
static void ai_fsm_check_transitions(int i)                           /* 0x403380 */
{
    Fsm *f = &g_fsm[i];
    sndfx_play_object_sample(f->object, i >= 3 && f->state == 9 ? 0 : 1);
    const Trans *tab = i < 3 ? g_racer_trans : g_cop_trans;
    for (int k = 0; k < 25; k++)
        if (tab[k].from == f->state && tab[k].cond(i) == tab[k].expected) { f->state = tab[k].to; break; }
}

static void ai_fsm_check_all_transitions(void)                        /* 0x403340 */
{
    int siren_done = 0;
    for (int i = 0; i < NFSM; i++) {
        if (g_fsm[i].object == -1) continue;
        ai_fsm_check_transitions(i);
        if (i >= 3 && !siren_done) { cop_siren_light_update(); siren_done = 1; }
    }
}

/* ai_cop_start_chase 0x4034a0: a free cop slot 3..14; at most 1 (easy) / 3 (hard) cops on the player, 2 on a racer;
 * in a city only with a chance of dt per call */
static int ai_cop_start_chase(int cop, int target)
{
    int max_cops = target == R->plr_obj ? (g_difficulty ? 3 : 1) : 2;
    if (R->track.city == 1) {
        float p = dt() * 1000.0f;
        if (!((float)(rand() % 1000) <= p)) return 0;
    }
    int s;
    for (s = 3; s < NFSM && g_fsm[s].object != -1; s++) ;
    if (s == NFSM) return 0;
    int n = 0;
    for (int k = 3; k < NFSM; k++) if (g_fsm[k].target == target) n++;
    if (n >= max_cops) return 0;
    g_fsm[s].object = cop;
    g_fsm[s].state = 3;
    g_fsm[s].target = target;
    g_fsm[s].car = R->plr_car;
    cop_voice(target, &g_cop_chase_voice, g_chase_samples, 13);
    cop_siren(cop, 1);
    return 1;
}

static void ai_fsm_release_cop(int i)                                 /* 0x4035b0 */
{
    if (i < 3) return;                                                /* "Illegal cop, this is a racer" */
    for (int k = 0; k < 20; k++) if (g_active_cops[k] == g_fsm[i].object) { g_active_cops[k] = -1; break; }
    g_fsm[i].object = g_fsm[i].state = g_fsm[i].target = -1;
    g_fsm[i].timer = 0; g_fsm[i].time_in_chunk = 0; g_fsm[i].laps = 0;
}

static int fsm_find_object(int o)                                     /* 0x403630 */
{
    for (int i = 0; i < NFSM; i++) if (g_fsm[i].object == o) return 1;
    return 0;
}

static void racer_finished(int o)                                     /* 0x411650: stamps timer 8+r */
{
    for (int r = 0; r < 3; r++)
        if (g_racers[r].object == o) { g_racers[r].time = (int)R->race_ms; g_racers[r].finished = 1; }
}

/* ai_fsm_handle_trigger 0x4088f0: speed limits (for the cops' check), the finish and lap triggers */
static void ai_fsm_handle_trigger(int i, int trigger)
{
    Fsm *f = &g_fsm[i];
    switch (trigger) {
    case 4: f->speed_limit = 12.5f; break;
    case 5: f->speed_limit = 17.5f; break;
    case 6: f->speed_limit = 25.0f; break;
    case 8: f->speed_limit = 30.0f; break;
    case 14:
        if (!R->track.city) { g_racers[i].finished = 1; racer_finished(f->object); f->state = 9; }
        else if (f->lap_trigger == 15) {
            f->laps++;
            f->lap_trigger = 14;
            if (f->laps >= g_nr_laps) { g_racers[i].finished = 1; racer_finished(f->object); f->state = 9; }
        }
        break;
    case 15: f->lap_trigger = 15; break;
    default: break;
    }
}

static void ai_fsm_update_chunk(int i)                                /* 0x409540: time in chunk, racer progress */
{
    Fsm *f = &g_fsm[i];
    int c = OBJ(f->object)->chunk;
    if (c == f->chunk) { f->time_in_chunk += dt(); return; }
    f->prev_chunk = f->chunk;
    f->chunk = c;
    f->time_in_chunk = 0;
    if (i >= 3) return;
    int *progress = &g_ai[OBJ(g_racers[i].object)->ai].progress;
    if (chunk_is_ahead(f->chunk, f->prev_chunk) == 1) *progress += rceng_chunk_distance(f->prev_chunk, f->chunk);
    else *progress -= rceng_chunk_distance(f->prev_chunk, f->chunk);
}

/* ---- FSM state handlers (ai_path.cpp) [7.4] ------------------------------------------------------------------- */
/* the steering and speed targets start ahead of the car, for when no path is found at all */
static void default_targets(int o, Vec3 *near, Vec3 *far)
{
    Vec3 f = obj_forward(o);
    *near = v3_add(OBJ(o)->pos, v3_scale(f, 256));
    *far = v3_add(OBJ(o)->pos, v3_scale(f, 512));
}

static void ai_state_race(int fsm)                                    /* 0x409d10: states 0, 1, 3 */
{
    Fsm *E = &g_fsm[fsm];
    Vec3 near, far;
    default_targets(E->object, &near, &far);
    int h = ai_wmap_add(E->object);
    if (h < 0) return;
    int wp = g_wm[h].waypoint, saved = E->twp;
    E->coast = 0;
    E->twp = ScanPath(fsm, h, 5, wp, &near, &far);
    if (E->twp > -1) { if (!ai_slower_car_ahead(wp, E->object)) ai_speed_control(fsm, far); }
    else E->twp = saved;
    CalculateStearing(fsm, near);
    ai_wmap_remove(h);
}

static void ai_state_block_player(int fsm)                            /* 0x409e00: state 2, the path of the player */
{
    Fsm *E = &g_fsm[fsm];
    Vec3 near, far;
    default_targets(E->object, &near, &far);
    int h = ai_wmap_add(E->target);
    if (h < 0) return;
    int saved = E->twp;
    E->twp = ScanPath(fsm, h, 5, g_wm[h].waypoint, &near, &far);
    if (E->twp > -1) { ai_speed_control(fsm, far); CalculateStearing(fsm, near); }
    else { E->brake = 0; E->throttle = 0; E->twp = saved; CalculateStearing(fsm, near); }
    E->timer += dt();
    ai_wmap_remove(h);
}

static void ai_state_cop_target_behind(int fsm)                       /* 0x409f60: state 4, block the target */
{
    Fsm *E = &g_fsm[fsm];
    Vec3 near, far;
    default_targets(E->object, &near, &far);
    int h = ai_wmap_add(E->object);
    if (h < 0) return;
    int saved = E->twp;
    E->twp = ScanPath(fsm, h, 5, OBJ(E->object)->waypoint, &near, &far);
    if (E->twp > -1) {
        if (OBJ(E->target)->vel.z < OBJ(E->object)->vel.z) { E->brake = 50.0f; E->throttle = 0.0f; }
        else {
            ai_speed_control(fsm, far);
            int a = ai_get_angle_to_point(E->object, OBJ(E->target)->pos);
            if (a > 1800 && a < 2200) E->brake = 100.0f;
        }
    } else { E->brake = 0; E->throttle = 0; E->twp = saved; }
    CalculateStearing(fsm, near);
    ai_wmap_remove(h);
}

static void ai_state_finished(int fsm)                                /* 0x409ed0: state 9, coast and spin */
{
    Fsm *E = &g_fsm[fsm];
    E->coast = 1;
    E->steer += 75.0f * dt();
    if (E->steer > 100.0f) E->steer = 100.0f;
    E->throttle = 0; E->brake = 0;
    ai_steer_physics(fsm);
    body_springs(E->object);
}

static void ai_state_stuck_reverse(int fsm)                           /* 0x40a190: state 10 */
{
    Fsm *E = &g_fsm[fsm];
    int o = E->object;
    if (!(E->time_in_chunk < 2.0f)) {
        E->steer = 0;
        OBJ(o)->vel.z = -15.0f;
        E->time_in_chunk = 0;
        E->reverse_t = 0;
        body_springs(o);
        return;
    }
    E->reverse_t += dt();
    car_body_roll(o); car_body_pitch(o); car_body_height(o);
    if (E->reverse_t > 1.0f) { E->state = fsm < 3 ? 0 : 3; E->reverse_t = 0; }
}

static void ai_state_racer_wait(int fsm)                              /* 0x40a290: state 11, rubber band */
{
    Fsm *E = &g_fsm[fsm];
    Vec3 near, far;
    default_targets(E->object, &near, &far);
    int h = ai_wmap_add(E->object);
    if (h < 0) return;
    int saved = E->twp;
    E->twp = ScanPath(fsm, h, 5, g_wm[h].waypoint, &near, &far);
    if (E->twp > -1) ai_set_steering(fsm, near); else E->twp = saved;
    ai_drive(fsm);
    ai_steer_physics(fsm);
    car_body_height(E->object); car_body_roll(E->object); car_body_pitch(E->object);
    OBJ(E->object)->vel.z = 16.666f;
    ai_wmap_remove(h);
}

static void ai_state_cop_arrest(int fsm)                              /* 0x40a390: state 5 */
{
    Fsm *E = &g_fsm[fsm];
    E->timer = 0;
    cop_siren(E->object, 0);
    if (E->target == R->plr_obj) { if (R->game_event == 0) ai_game_action_start(8); }
    else for (int i = 0; i < 3; i++) if (g_fsm[i].object == E->target) g_fsm[i].state = 7;
    for (int i = 3; i < NFSM; i++) if (g_fsm[i].target == E->target) g_fsm[i].state = 6;
    body_springs(E->object);
}

static void ai_state_hold(int fsm)                                    /* 0x40a480 state 6 / 0x40a610 state 7 */
{
    Fsm *E = &g_fsm[fsm];
    Obj *o = OBJ(E->object);
    if (E->state == 7) { E->throttle = 0; E->brake = 0; }
    o->vel.z = 0; o->vel.x = 0; o->yawrate = 0;
    body_springs(E->object);
}

static void ai_state_cop_parked(int fsm)                              /* 0x40a4f0: state 8 */
{
    Fsm *E = &g_fsm[fsm];
    Obj *o = OBJ(E->object);
    if (rceng_chunk_distance(o->chunk, OBJ(R->plr_obj)->chunk) > 30) {
        o->active = 0;
        ai_fsm_release_cop(fsm);
        return;
    }
    car_body_height(E->object); car_body_roll(E->object); car_body_pitch(E->object);
    o->vel.z -= dt() * (o->vel.z * 3.0f);
    o->vel.x -= dt() * (o->vel.x * 3.0f);
    o->yawrate -= dt() * (o->yawrate * 3.0f);
}

/* states 14 (0x40aab0, target against the road) and 16 (0x40a6a0, target on the other side): follow the path,
 * brake when ahead of the target and faster */
static void ai_state_cop_follow(int fsm)
{
    Fsm *E = &g_fsm[fsm];
    int o = E->object;
    Vec3 near, far;
    default_targets(o, &near, &far);
    int h = ai_wmap_add(o);
    if (h < 0) return;
    int saved = E->twp;
    E->twp = ScanPath(fsm, h, 5, g_wm[h].waypoint, &near, &far);
    if (E->twp > -1) {
        if (chunk_is_ahead(OBJ(o)->chunk, OBJ(E->target)->chunk)) {
            if (OBJ(o)->vel.z > OBJ(E->target)->vel.z) {
                E->brake += 400.0f * dt();
                if (E->brake > 100.0f) E->brake = 100.0f;
                E->throttle = 0;
            }
        } else ai_speed_control(fsm, far);
        ai_set_steering(fsm, near);
    } else E->twp = saved;
    ai_drive(fsm);
    ai_steer_physics(fsm);
    if (OBJ(o)->vel.z < 0.0f) { OBJ(o)->vel.z = 0; car_stop_damp(o); E->brake = 0; }
    if (E->state == 14 || !(OBJ(E->target)->vel.z > 0.0f)) E->time_in_chunk = 0;
    car_body_height(o); car_body_roll(o); car_body_pitch(o);
    ai_wmap_remove(h);
}

static void ai_state_cop_give_up(int fsm)                             /* 0x40a9d0: state 15 */
{
    Fsm *E = &g_fsm[fsm];
    Vec3 near, far;
    default_targets(E->object, &near, &far);
    E->coast = 1;
    int h = ai_wmap_add(E->object);
    if (h < 0) return;
    int wp = g_wm[h].waypoint, saved = E->twp;
    E->twp = ScanPath(fsm, h, 5, wp, &near, &far);
    if (E->twp > -1) { if (ai_slower_car_ahead(wp, E->object)) E->coast = 1; else ai_speed_control(fsm, far); }
    else E->twp = saved;
    CalculateStearing(fsm, near);
    ai_wmap_remove(h);
}

static void ai_state_cop_depart(int fsm)                              /* 0x40a8a0: state 17 */
{
    Fsm *E = &g_fsm[fsm];
    int o = E->object;
    OBJ(o)->no_vel_rotate = 0;
    int a = ai_get_angle_to_point(o, OBJ(E->target)->pos);
    if (a > 0x400 && a < 0xc00) {
        ai_state_race(fsm);
        OBJ(o)->vel.z = 10.0f;
        E->timer += dt();
        if (E->timer > 3.0f) OBJ(o)->vel.z = 0;
    }
    body_springs(o);
}

static void ai_fsm_handle_object(int i)                               /* HandleAIObject 0x408a50 */
{
    switch (g_fsm[i].state) {
    case 0: case 1: case 3: ai_state_race(i); break;
    case 2: ai_state_block_player(i); break;
    case 4: ai_state_cop_target_behind(i); break;
    case 5: ai_state_cop_arrest(i); break;
    case 6: case 7: ai_state_hold(i); break;
    case 8: ai_state_cop_parked(i); break;
    case 9: ai_state_finished(i); break;
    case 10: ai_state_stuck_reverse(i); break;
    case 11: ai_state_racer_wait(i); break;
    case 14: case 16: ai_state_cop_follow(i); break;
    case 15: ai_state_cop_give_up(i); break;
    case 17: ai_state_cop_depart(i); break;
    default: break;                                                   /* "Illegal state. Function: HandleAIObject" */
    }
    if (g_fsm[i].object != -1) ai_fsm_update_chunk(i);
}

/* ---- race position (0x408780, 0x408c00, 0x408cb0) ------------------------------------------------------------- */
static int ai_is_point_behind(int o, Vec3 p)                          /* 0x408780 */
{
    int w = OBJ(o)->waypoint, l;
    if (w < 0) return 0;
    if ((l = WP(w)->next) >= 0) ;
    else if ((l = WP(w)->next_outer) >= 0) ;
    else if ((l = WP(w)->next_inner) >= 0) ;
    else if ((l = WP(w)->prev) >= 0) ;
    else { int a = ai_get_angle_to_point(o, p); return a > 0x400 && a < 0xc00; }
    float dx = WP(l)->pos.x - WP(w)->pos.x, dz = WP(l)->pos.z - WP(w)->pos.z;
    float px = p.x - OBJ(o)->pos.x, pz = p.z - OBJ(o)->pos.z;
    float sum = sqrtf(dx * dx + dz * dz) + sqrtf(px * px + pz * pz);   /* sic: the sum of the lengths */
    return (px * dx + pz * dz) / sum < 0.0f;
}

static int racer_progress(int r) { return g_ai[OBJ(g_racers[r].object)->ai].progress; }

static int ai_get_player_position(void)                               /* 0x408c00 */
{
    int n = 0;
    for (int i = 0; i < 3; i++) {
        int prog = racer_progress(i);
        if (R->progress < prog) { n++; continue; }
        if (R->progress != prog) continue;
        if (!ai_is_point_behind(R->plr_obj, OBJ(g_fsm[i].object)->pos)) n++;
    }
    return n;
}

static void ai_rank_racers(void)                                      /* 0x408cb0 */
{
    for (int i = 0; i < 3; i++) {
        int pos = 0, pi = racer_progress(i);
        if (R->progress > pi) pos = 1;
        else if (R->progress == pi && !ai_is_point_behind(g_fsm[i].object, OBJ(R->plr_obj)->pos)) pos = 1;
        for (int j = 0; j < 3; j++) {
            if (j == i) continue;
            int pj = racer_progress(j);
            if (pj > pi) pos++;
            else if (pj == pi && !ai_is_point_behind(g_fsm[i].object, OBJ(g_fsm[j].object)->pos)) pos++;
        }
        g_racers[i].position = pos;
    }
    for (int p = 0; p < 4; p++) {                                     /* ties: the later ones move down */
        int cnt = 0;
        for (int k = 0; k < 3; k++) if (g_racers[k].position == p) cnt++;
        if (p == g_player_position) cnt++;
        cnt = cnt <= 1 ? 0 : cnt - 1;
        for (int n = 0, k = 0; n < cnt && k < 3; k++)
            if (g_racers[k].position == p) { g_racers[k].position = p + n + 1; n++; }
    }
}

int ai_player_position(void) { return g_player_position; }
int ai_difficulty(void) { return g_difficulty; }
int ai_visible_chunks(void) { return g_visible_chunks; }

static float g_dt_hist[4];              /* g_dt_hist 0x4e4928 / g_dt_hist_idx 0x4e4980 */
static int g_dt_idx;

void ai_frame_rate_governor(double frame_seconds, int racing)
{
    /* timer_update 0x441bd0: the game timer's dt is a weighted average of the last 4 frames (1/2, 1/4, 1/8, 1/8) */
    g_dt_hist[g_dt_idx] = (float)frame_seconds;
    float avg = g_dt_hist[g_dt_idx] * 0.5f + g_dt_hist[(g_dt_idx + 3) & 3] * 0.25f + g_dt_hist[(g_dt_idx + 2) & 3] * 0.125f
              + g_dt_hist[(g_dt_idx + 1) & 3] * 0.125f;
    g_dt_idx = (g_dt_idx + 1) & 3;
    /* timer_frame_dt 0x441cd0: 0 or more than 0.1 s -> 0.1f; handle_game 0x424c98: if g_game_event == 0 and
     * 1 / dt < 10 (true for 0.1f) and visiblechunks > (g_hw_accel ? 25 : 15): visiblechunks-- */
    float dt = avg == 0 || avg > 0.1 ? 0.1f : avg;
    if (racing && 1.0 / dt < 10.0 && g_visible_chunks > 25) g_visible_chunks--;
}

static void ai_fsm_handle_all(void)                                   /* 0x408ed0 */
{
    if (!R->control || !R->race_started) return;
    for (int i = 0; i < NFSM; i++) {
        Fsm *f = &g_fsm[i];
        if (f->object == -1) continue;
        ai_fsm_handle_object(i);
        if (f->object == -1) continue;
        const Chunk *c = CHUNK(OBJ(f->object)->chunk);
        int wt = OBJ(f->object)->waypoint >= 0 ? WP(OBJ(f->object)->waypoint)->flags2 : 0;
        if (i < 3) {
            ai_fsm_handle_trigger(i, c->trigger[0]);
            ai_fsm_handle_trigger(i, c->trigger[1]);
            ai_fsm_handle_trigger(i, wt);
        } else {
            if (R->game_event == 3 || R->game_event == 6) cop_siren(f->object, 0);
            for (int t = 0; t < 2; t++) if (c->trigger[t] >= 14 && c->trigger[t] <= 15 && !R->track.city) f->state = 9;
            if (wt >= 14 && wt <= 15 && !R->track.city) f->state = 9;
        }
    }
    if (!g_player_pos_frozen) g_player_position = ai_get_player_position();
    ai_rank_racers();
}

/* ---- racers: HANDLE_RACER_AI 0x40ac90 and the old model's triggers [7.5] -------------------------------------- */
static void racer_trigger(int trigger, int a, int o)                  /* 0x40b030 */
{
    AiState *A = &g_ai[a];
    switch (trigger) {
    case 2: A->max_speed_allowed = 0; OBJ(o)->vel.z = 0; A->racer_mode = 7; break;     /* BENZINE_STATION */
    case 14:                                                          /* FINISH_POSITION */
        if (A->finish_valid == 1) {
            A->laps++;
            if (A->laps == g_nr_laps && A->behavior != 0) {
                A->behavior = 0;
                A->max_speed_allowed = 0;
                OBJ(o)->update = handle_basic_ai;
                A->racer_state = 2;
                racer_finished(o);
            }
        }
        break;
    case 15: A->finish_valid = 1; break;
    case 46: A->racer_mode = 5; A->max_speed_allowed = BH(a)->top_speed * 0.5f; break;     /* FUEL_APPROACH */
    case 47: A->racer_mode = 1; A->max_speed_allowed = BH(a)->top_speed * 0.25f; break;    /* FINAL_APPROACH */
    default: break;     /* 9 ZERO_CHUNK_MARKER calls sub_42ae20 (not traced) */
    }
}

static void check_racer_trigger(int o)                                /* 0x40af90 */
{
    int a = OBJ(o)->ai;
    racer_trigger(CHUNK(OBJ(o)->chunk)->trigger[0], a, o);
    racer_trigger(CHUNK(OBJ(o)->chunk)->trigger[1], a, o);
    racer_trigger(WP(OBJ(o)->waypoint)->flags2, a, o);
}

/* with the new model the FSM drives and this handler only turns the wheels (car_update_wheels_ai 0x413be0, not
 * drawn here); with ai_model 1 the racers are basic AI with a rubber band on the progress gap */
static int handle_racer_ai(int o)
{
    if (g_ai_model != 1) return 1;
    if (!R->control || !R->race_started) return 1;
    int a = OBJ(o)->ai;
    AiState *A = &g_ai[a];
    ai_check_collision(o);
    if (A->collision == 0) {
        if (OBJ(o)->chunk != OBJ(o)->prev_chunk) check_racer_trigger(o);
        float top = BH(a)->top_speed;
        switch (A->racer_state) {
        case 0: {
            int d = A->progress - R->progress;
            A->max_speed_allowed = d > 5 ? top - (float)(2 * d - 10) : top;
            if (A->max_speed_allowed < 5.0f) A->max_speed_allowed = 5.0f;
            break;
        }
        case 1: {
            int d = OBJ(o)->chunk - OBJ(R->plr_obj)->chunk;
            if (d < -9) {
                A->max_speed_allowed = g_player_top_speed;
                if (top > A->max_speed_allowed) A->max_speed_allowed = top;
            } else if (d < -3) { A->max_speed_allowed = top; A->racer_state = 0; }
            else {
                A->max_speed_allowed = d > 0 ? (1.0f - (float)(d / 15) * 0.2f) * OBJ(R->plr_obj)->vel.z : OBJ(R->plr_obj)->vel.z;
                if (A->max_speed_allowed < 15.0f) A->max_speed_allowed = 15.0f;
            }
            break;
        }
        default: break;
        }
        change_lane(o, A->racer_mode, -1);
        if (A->racer_mode != 7) handle_basic_ai(o);
    }
    sndfx_play_object_sample(o, A->sample_flag);
    return 1;
}

/* ---- police (ai_cops.cpp) [8] --------------------------------------------------------------------------------- */
/* cop_fsm_lookout 0x409060: a racer at or above its speed limit, or the player above 12.5 / 17.5 / 25 / 30 m/s
 * for the speed trigger behind the cop, in the cop's chunk */
static void ai_cop_check_speeding(int cop)
{
    int chunk = OBJ(cop)->chunk;
    for (int k = 3; k < NFSM; k++) if (g_fsm[k].object == cop) return;
    for (int i = 0; i < 3; i++) {
        int r = g_fsm[i].object;
        if (r >= 0 && OBJ(r)->chunk == chunk && g_fsm[i].speed_limit <= OBJ(r)->vel.z) { ai_cop_start_chase(cop, r); return; }
    }
    if (chunk != OBJ(R->plr_obj)->chunk) return;
    int speed_trig, lane_trig;
    backtrack_speed_trigger(cop, &speed_trig, &lane_trig);
    float v = OBJ(R->plr_obj)->vel.z;
    if ((speed_trig == 4 && v > 12.5f) || (speed_trig == 5 && v > 17.5f) || (speed_trig == 6 && v > 25.0f)
        || (speed_trig == 8 && v > 30.0f))
        ai_cop_start_chase(cop, R->plr_obj);
}

static void player_autopilot_on(void)                                 /* 0x4258f0 */
{
    Obj *p = OBJ(R->plr_obj);
    if (g_player_ai == -1) { add_ai(R->plr_obj, 0, p->waypoint, 0); g_player_ai = p->ai; }
    if (p->ai >= 0) g_ai[p->ai].waypoint = p->waypoint;
    p->no_vel_rotate = 0;
    g_player_autopilot = 1;
}

static void player_autopilot_off(void)                                /* 0x425980 */
{
    OBJ(R->plr_obj)->no_vel_rotate = 0;
    g_player_autopilot = 0;
}

int ai_plr_autopilot(int o)                                           /* the autopilot branch of handle_player_car */
{
    if (!g_player_autopilot) return 0;
    Obj *c = OBJ(o);
    handle_basic_ai(o);
    c->pos.y = car_road_height_reset(o);
    c->vel.x = c->vel.y = 0;
    c->roll = 0;
    return 1;
}

static int cop_check_target(int o, int cop, int target)               /* 0x403130 */
{
    int side = -1, lane = 0;
    find_waypoint_side_lane(OBJ(target)->chunk, OBJ(target)->waypoint, &side, &lane);
    if (OBJ(o)->side != side) return 1;                               /* "Ghost!" */
    return g_cops[cop].max_speed * 1.1f < OBJ(target)->vel.z;          /* "Speeding!" */
}

static void cop_quit(int cop)
{
    int a = OBJ(g_cops[cop].obj)->ai;
    g_cops[cop].state = 3;
    g_ai[a].behavior = 2;
    g_ai[a].max_speed_allowed = g_bh[3].top_speed;
    g_cops[cop].timer = 0;
    cop_siren(g_cops[cop].obj, 0);
}

/* cop_pursuit 0x402b40 (old model): 40 s at most; behind / next to the target, arrest one chunk ahead of it in its
 * lane after 5 s */
static int cop_pursuit(int o, int cop)
{
    Cop *C = &g_cops[cop];
    int target = C->target, a = OBJ(o)->ai;
    C->timer += dt();
    C->chase_time += dt();
    if (!(C->chase_time < 40.0f)) cop_quit(cop);
    else if (OBJ(o)->chunk > OBJ(target)->chunk) {                    /* "Ahead of target!" */
        int ts = -1, tl = 0, ms = -2, ml = 0;
        find_waypoint_side_lane(OBJ(target)->chunk, OBJ(target)->waypoint, &ts, &tl);
        find_waypoint_side_lane(OBJ(o)->chunk, OBJ(o)->waypoint, &ms, &ml);
        C->target_lane = tl;
        if (ts == ms) {
            if (tl == ml) {
                if (OBJ(o)->chunk - OBJ(target)->chunk == 1 && C->timer > 5.0f && R->game_event == 0) {
                    C->state = 2;
                    C->timer = 0;
                    g_ai[a].behavior = 0;
                    OBJ(o)->vel.z = OBJ(target)->vel.z;
                    g_ai[a].max_speed_allowed = 0;
                    cop_siren(o, 0);
                    ai_game_action_start(8);
                    int last_used = -1;
                    for (int k = 0; k < NCOPS; k++) if (g_cop_used[k]) last_used = k;
                    for (int i = 0; i < last_used; i++)                  /* the highest index is skipped [quirk] */
                        if (i != cop && g_cops[i].state == 1 && g_cops[i].target == C->target) cop_quit(i);
                    player_autopilot_on();
                }
            } else {                                                  /* left and right do the same [quirk] */
                if (switch_lane(a, 1) == 1) g_ai[a].chosen_lane--;
                g_ai[a].max_speed_allowed = OBJ(target)->vel.z * 0.5f;
            }
        } else g_ai[a].max_speed_allowed = OBJ(target)->vel.z * 0.8f;
    } else if (OBJ(target)->chunk - OBJ(o)->chunk > 5) g_ai[a].max_speed_allowed = BH(a)->top_speed;
    else {
        g_ai[a].max_speed_allowed = OBJ(target)->vel.z * 1.25f;
        if (g_ai[a].max_speed_allowed < 15.0f) g_ai[a].max_speed_allowed = 15.0f;
    }
    if (!R->control || !R->race_started) return 1;
    /* "Handling AI.": a contact makes the cop a free body (it also hits rails) until 0.5 s after it ends */
    if (g_ai[a].collision == 1) {
        g_ai[a].collision_timer += dt();
        if (g_ai[a].collision_timer > 0.5f) { g_ai[a].collision = 0; g_ai[a].collision_timer = 0; OBJ(o)->no_vel_rotate = 1; }
    }
    if (OBJ(o)->in_collision == 1) { g_ai[a].collision = 1; g_ai[a].collision_timer = 0; OBJ(o)->no_vel_rotate = 0; }
    if (g_ai[a].collision == 0) {
        change_lane(o, 2, -1);
        handle_basic_ai(o);
    }
    sndfx_play_object_sample(o, 1);
    return 1;
}

/* HANDLE_COP_AI 0x402790: with the new model only the lookout (the FSM chases); a parked cop is braked */
static int handle_cop_ai(int o)
{
    if (g_ai_model != 1) {
        ai_cop_check_speeding(o);
        if (!fsm_find_object(o)) car_stop_damp(o);
        return 0;
    }
    int ret = 1, a = OBJ(o)->ai, cop = g_ai[a].cop;
    if (cop < 0) return 1;
    Cop *C = &g_cops[cop];
    ai_check_collision(o);
    if (g_ai[a].collision == 0) {
        switch (C->state) {
        case 0:                                                       /* "On the lookout" */
            if (abs(OBJ(C->target)->chunk - OBJ(o)->chunk) < 2 && cop_check_target(o, cop, C->target) == 1) {
                C->state = 1;
                g_ai[a].behavior = 14;
                g_ai[a].max_speed_allowed = BH(a)->top_speed;
                cop_siren(o, 1);
                C->timer = 0;
            }
            break;
        case 1: ret = cop_pursuit(o, cop); break;
        case 2:                                                       /* "Got him!" */
            g_ai[a].max_speed_allowed = OBJ(C->target)->vel.z * 0.5f;
            if (R->game_event != 8) {
                player_autopilot_off();
                C->state = 3;
                g_ai[a].behavior = 2;
                g_ai[a].max_speed_allowed = g_bh[3].top_speed;
                C->timer = 0;
            }
            change_lane(o, 2, -1);
            ret = handle_basic_ai(o);
            break;
        case 3: change_lane(o, 4, -1); ret = handle_basic_ai(o); break;   /* "Quiting chase!" */
        default: ret = handle_basic_ai(o); break;
        }
    }
    cop_siren_light_update();
    return ret;
}

/* car_cops_add_static 0x40c920: a type-9 .omp police car becomes a parked AI car on the waypoint graph */
static void car_add_cop(const TrackObject *t)
{
    int o = obj_add(t->anim, t->pos, -t->rot[1] * ANGLE_UNITS / 360.0f);
    if (o < 0) return;
    Obj *c = OBJ(o);
    c->chunk = c->prev_chunk = t->chunk;
    c->car_type = 7;
    c->side = g_direction == 0 ? 1 : 0;
    c->waypoint = drmap_find_closest_waypoint(o, c->side);
    c->update = handle_cop_ai;
    c->collide = mesh_damage_collision_func;
    int side = 0, lane = 0;
    find_waypoint_side_lane(c->chunk, c->waypoint, &side, &lane);
    obj_init_physics(c, 7500.0f, 1.7f, 4.5f, 0.3f);
    ai_wp_add(o);
    car_list_add(o, 4);
    add_ai(o, 14, c->waypoint, lane);
    c->always_update = 1;
}

/* ---- traffic (Car_main.cpp, ai_main.cpp) [9] ------------------------------------------------------------------ */
static int handle_traffic_ai(int o)                                   /* HANDLE_TRAFFIC_AI 0x40b770 */
{
    Obj *c = OBJ(o);
    car_body_pitch(o);
    car_body_roll(o);
    if (!R->race_started) return 1;
    int a = c->ai;
    if (g_ai[a].state == 1 && g_light_state != 0) { g_ai[a].max_speed_allowed = 500.0f; g_ai[a].state = 0; }
    ai_check_collision(o);
    if (g_ai[a].collision == 0) {
        if (c->chunk != c->prev_chunk) ai_check_triggers(o);
        int c10 = rceng_wrap_chunk(c->chunk + 10);
        ai_limit_lanes(a, c->side, CHUNK(c10)->trigger[0]);
        ai_limit_lanes(a, c->side, CHUNK(c10)->trigger[1]);
        change_lane(o, 0, g_ai[a].max_lanes);
        handle_basic_ai(o);
    }
    sndfx_play_object_sample(o, g_ai[a].sample_flag);
    c->pos.y = car_road_height_reset(o);
    return 1;
}

/* init_traffic 0x40ca70: same-side and oncoming cars in lane 0, 3 chunks apart, starting 5 chunks outside the
 * window, all switched off; the model and the behaviour are two independent draws from g_traffic_types [quirk] */
static void init_traffic(void)
{
    int side_a = g_direction == 0 ? 1 : 0, side_b = g_direction == 0 ? 0 : 1;
    for (int g = 0; g < 4; g++) {
        int side = g < 2 ? side_a : side_b, n = (g < 2 ? g_traffic_same_side : g_traffic_oncoming) / 2;
        for (int i = 0, d = 0; i < n; i++, d += 3) {
            int chunk = rceng_wrap_chunk(g & 1 ? g_startchunk + g_visible_chunks + d + 5 : g_startchunk - g_visible_chunks - d - 5);
            int type = g_traffic_types[rand() % 12][0], beh = g_traffic_types[rand() % 12][1];
            int o = add_car(type, chunk, 0, side, side, handle_traffic_ai, mesh_damage_collision_func, 2);
            if (o < 0) continue;
            add_ai(o, beh, rceng_get_waypoint_index(chunk, 0, side), 0);
            OBJ(o)->active = 0;
        }
    }
}

/* sub_446a10: the car overlaps another (bounding radii x 1.25, the other one half a step ahead) */
static int objects_overlap(int a, int b)
{
    const Model *ma = anim_model(OBJ(a)->anim, OBJ(a)->frame), *mb = anim_model(OBJ(b)->anim, OBJ(b)->frame);
    float ra = ma ? ma->radius : 0, rb = mb ? mb->radius : 0;
    float dx = OBJ(a)->pos.x - (OBJ(b)->pos.x + OBJ(b)->vel.x * 0.5f), dz = OBJ(a)->pos.z - (OBJ(b)->pos.z + OBJ(b)->vel.z * 0.5f);
    float sum = 1.25f * ra + 1.25f * rb + 0.5f * (OBJ(b)->vel.y + OBJ(b)->vel.x);
    return sum * sum > dx * dx + dz * dz;
}

static int can_respawn_at(int w, int o, int force)                    /* 0x40b650 */
{
    if (!force) {
        if (rand() % 30 != 0) return 0;
        if (1.0f / dt() < 14.5f) return 0;
    }
    if (w != -1) {
        const Waypoint *p = WP(w);
        if (g_wp_n[w]) return 0;
        if (p->next != -1 && g_wp_n[p->next]) return 0;
        if (p->prev != -1 && g_wp_n[p->prev]) return 0;
    }
    for (int i = 0; i < NCARLIST; i++) {
        const CarListEntry *e = &g_car_list[i];
        if (e->active && OBJ(e->object)->active && objects_overlap(o, e->object) && e->object != o) return 0;
    }
    return 1;
}

/* respawn_car 0x40b480: to lane 0 of a chunk at the other end of the window, keeping its speed */
static void respawn_car(int o, int chunk, int force)
{
    Obj *c = OBJ(o);
    chunk = rceng_wrap_chunk(chunk);
    if (c->ai == -1) return;
    int dir = c->side == 0 ? g_direction != 0 : g_direction == 0;
    if (CHUNK(chunk)->nwp[g_direction == 0 ? 1 : 0] == 1) return;     /* never into a single-lane chunk */
    int w = rceng_get_waypoint_index(chunk, 0, c->side);
    if (w < 0) return;
    if (!can_respawn_at(w, o, force)) { c->active = 0; return; }
    ai_wp_remove(o);
    int pw = rceng_get_waypoint_index(chunk, 0, dir);
    rceng_set_object_to_chunk_angle(o, chunk, dir);
    c->pos = c->draw_pos0 = WP(pw >= 0 ? pw : w)->pos;
    c->prev_chunk = c->chunk = chunk;
    ai_set_lane(o, 0);
    c->waypoint = w;
    g_ai[c->ai].waypoint = g_ai[c->ai].waypoint2 = w;
    ai_wp_add(o);
    c->active = 1;
}

/* ai_check_cars_outside_handling_range 0x40b2d0: traffic leaving player_chunk +-(visiblechunks - 1) is moved to
 * the other end; racers and cops stay, the ADAC is switched off */
static void ai_check_cars_outside_handling_range(void)
{
    int lo, hi, lo2, hi2, half = nchunks() / 2;
    int wrapped = calc_chunk_window(OBJ(R->plr_obj)->chunk, g_visible_chunks - 1, &lo, &hi, &lo2, &hi2);
    for (int i = 0; i < NCARLIST; i++) {
        const CarListEntry *e = &g_car_list[i];
        if (!e->active || e->object == R->plr_obj) continue;
        int o = e->object, c = OBJ(o)->chunk, behind;
        if (wrapped) {
            if (c < half) { if (c <= hi2) continue; behind = 0; }
            else { if (c >= lo) continue; behind = 1; }
        } else if (c < lo) behind = 1;
        else if (c > hi) behind = 0;
        else continue;
        switch (e->type) {
        case 3: continue;
        case 4:
            if (behind && g_ai_model == 1) {
                int cop = g_ai[OBJ(o)->ai].cop;
                if (cop >= 0 && g_cops[cop].state == 1) cop_siren(o, 0);
            }
            continue;
        case 5:                                                       /* turn_off_adac 0x40c5f0 */
            ai_wp_remove(g_adac_object);
            OBJ(g_adac_object)->visible = 0;
            OBJ(g_adac_object)->active = 0;
            continue;
        default: respawn_car(o, behind ? (wrapped ? hi2 : hi) : lo, 0); break;
        }
    }
}

/* init_racer_cars 0x411170: the grid positions left by the player (one chunk apart, lane 4 or the right-most), the
 * nearest unused car models, behaviour 7..13 by model */
static void init_racer_cars(void)
{
    static const int beh_of_car[7] = { 7, 9, 8, 10, 11, 12, 13 };
    int side = g_direction == 0, used_pos[4] = { 0 };
    used_pos[g_player_position & 3] = 1;
    for (int r = 0; r < 3; r++)
        for (int p = 0; p < 4; p++) if (!used_pos[p]) { g_racers[r].position = p; used_pos[p] = 1; break; }
    memset(g_car_model_used, 0, sizeof g_car_model_used);
    g_car_model_used[R->plr_car] = 1;
    for (int r = 0; r < 3; r++) {
        int m, found = 0;
        for (int pass = 0; pass < 2 && !found; pass++) {
            int up = (r & 1) ? pass == 1 : pass == 0;                 /* odd: down first; even: up first */
            if (up) { for (m = R->plr_car; m < 7; m++) if (!g_car_model_used[m]) { found = 1; break; } }
            else { for (m = R->plr_car; m >= 0; m--) if (!g_car_model_used[m]) { found = 1; break; } }
            if (found) { g_racers[r].car = m; g_car_model_used[m] = 1; }
        }
    }
    for (int r = 0; r < 3; r++) {
        int chunk = rceng_wrap_chunk(g_startchunk - g_racers[r].position);
        int lane = rceng_get_valid_lane(chunk, side, 4);
        int car = g_racers[r].car;
        int o = add_car(car, chunk, lane, side, 1, handle_racer_ai, mesh_damage_collision_func, 3);
        if (o < 0) continue;
        add_ai(o, car >= 0 && car < 7 ? beh_of_car[car] : 10, rceng_get_waypoint_index(chunk, lane, side), lane);
        OBJ(o)->no_vel_rotate = 0;
        OBJ(o)->always_update = 1;
        g_racers[r].object = o;
        g_racers[r].finished = 0;
        g_fsm[r].object = o;
        g_fsm[r].car = car;
    }
}

/* apply_difficulty_modifier 0x4108d0: dif_mod scales the racers' own CARS.DAT vmax - which the AI drive does not
 * read (it uses the player's car), so it only matters for the skill value [quirk] */
static void apply_difficulty_modifier(void)
{
    static const float mult[10] = { 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f };
    int upgrades = (R->upg_motor != 0) + (R->upg_gearbox != 0);
    for (int i = 0; i < 3; i++) {
        Fsm *e = &g_fsm[i];
        int gap = R->plr_car + upgrades - e->car;
        if (gap < 0) gap = 0;
        if (gap > 3) gap = 3;
        e->skill = (float)gap * 4.0f;
        if (e->car < 0 || e->car > 9) continue;
        float m = mult[g_dif_mod >= 0 && g_dif_mod <= 9 ? g_dif_mod : 4];
        if (R->upg_gearbox) for (int k = 0; k < 6; k++) R->cars[e->car].vmax[1][k] *= m;
        else for (int k = 0; k < 5; k++) R->cars[e->car].vmax[0][k] *= m;
    }
}

static void set_trans(Trans *t, int idx, int from, int to, int expected, int (*cond)(int))
{
    t[idx].from = from; t[idx].to = to; t[idx].expected = expected; t[idx].cond = cond;
}

static void ai_fsm_init(void)                                         /* 0x4093b0 */
{
    for (int k = 0; k < 25; k++) {
        g_racer_trans[k].from = g_racer_trans[k].to = -1; g_racer_trans[k].expected = 0; g_racer_trans[k].cond = NULL;
        g_cop_trans[k].from = g_cop_trans[k].to = -1; g_cop_trans[k].expected = 0; g_cop_trans[k].cond = NULL;
    }
    for (int i = 0; i < 3; i++) {
        Fsm *f = &g_fsm[i];
        if (f->object < 0) continue;
        f->state = 0; f->target = -1; f->timer = f->time_in_chunk = 0; f->laps = 0;
        f->twp = -1; f->chunk = OBJ(f->object)->chunk; f->prev_chunk = -1; f->lap_trigger = 0; f->speed_limit = 0;
        g_ai[i].progress = 0;                                         /* sic: indexed by the FSM slot */
        int speed_trig, lane_trig;
        backtrack_speed_trigger(f->object, &speed_trig, &lane_trig);
        ai_fsm_handle_trigger(i, speed_trig);
    }
    for (int i = 3; i < NFSM; i++) { memset(&g_fsm[i], 0, sizeof g_fsm[i]); g_fsm[i].object = g_fsm[i].target = -1; g_fsm[i].twp = -1; }
    if (!g_demo) {
        set_trans(g_racer_trans, 0, 0, 11, 2, cfun_racer_too_far);
        set_trans(g_racer_trans, 1, 11, 0, 1, cfun_racer_near_player);
    }
    set_trans(g_racer_trans, 2, 7, 0, 1, cfun_wait_4s);
    set_trans(g_racer_trans, 3, 0, 10, 1, cfun_stuck_3s);
    set_trans(g_racer_trans, 4, 0, 1, 1, cfun_player_behind_leader);
    set_trans(g_racer_trans, 5, 1, 2, 1, cfun_random_block);
    set_trans(g_racer_trans, 6, 2, 0, 0, cfun_player_behind_leader);
    /* ai_fsm_init_cop_transitions 0x409200 (index 12 is never set) */
    set_trans(g_cop_trans, 0, 3, 4, 1, cfun_target_behind_cop);
    set_trans(g_cop_trans, 1, 4, 3, 0, cfun_target_behind_cop);
    set_trans(g_cop_trans, 2, 4, 5, 1, cfun_can_arrest);
    set_trans(g_cop_trans, 3, 3, 5, 1, cfun_can_arrest);
    set_trans(g_cop_trans, 4, 5, 6, 1, return_true);
    set_trans(g_cop_trans, 5, 6, 17, 1, cfun_wait_4s);
    set_trans(g_cop_trans, 6, 17, 8, 1, cfun_stopped);
    set_trans(g_cop_trans, 7, 3, 16, 1, cfun_target_other_side);
    set_trans(g_cop_trans, 8, 4, 16, 1, cfun_target_other_side);
    set_trans(g_cop_trans, 9, 16, 3, 1, cfun_target_back_on_side);
    set_trans(g_cop_trans, 10, 4, 14, 1, cfun_target_not_forward);
    set_trans(g_cop_trans, 11, 16, 14, 1, cfun_target_not_forward);
    set_trans(g_cop_trans, 13, 14, 3, 1, cfun_target_forward_near);
    set_trans(g_cop_trans, 14, 16, 10, 1, cfun_cop_stuck);
    set_trans(g_cop_trans, 15, 3, 10, 1, cfun_stuck_3s);
    set_trans(g_cop_trans, 16, 3, 15, 1, cfun_give_up);
    set_trans(g_cop_trans, 17, 15, 8, 1, cfun_stopped);
    set_trans(g_cop_trans, 18, 3, 14, 1, cfun_target_reversing);
    set_trans(g_cop_trans, 19, 4, 14, 1, cfun_target_reversing);
    set_trans(g_cop_trans, 20, 16, 14, 1, cfun_target_reversing);
    apply_difficulty_modifier();
    for (int i = 0; i < 8; i++) { g_wm[i].object = g_wm[i].chunk = g_wm[i].waypoint = -1; g_wm[i].dir_fwd = g_wm[i].dir_side = 0; }
}

/* ---- traffic lights (objtype_traffic_light 0x429480): green 5 s, amber 0.5 s, red 5 s; frames 0 / 1 / 2 ------ */
static void traffic_lights_update(void)
{
    const ObjectSet *s = &R->objects;
    int lo, hi, lo2, hi2, any = 0;
    int wrapped = calc_chunk_window(OBJ(R->plr_obj)->chunk, 2 * g_visible_chunks, &lo, &hi, &lo2, &hi2);
    for (int i = 0; i < s->n; i++) {
        TrackObject *t = &s->obj[i];
        if (t->type != OT_TRAFFIC_LIGHT) continue;
        if (!((t->chunk >= lo && t->chunk <= hi) || (wrapped && t->chunk >= lo2 && t->chunk <= hi2))) continue;
        t->frame = g_light_state == 3 ? 0 : g_light_state == 2 ? 1 : 2;
        any = 1;
    }
    if (!any || now_ms() < g_light_timer_end) return;
    switch (g_light_state) {
    case 3: g_light_timer_end = now_ms() + 500; g_light_state = 2; break;
    case 2: g_light_timer_end = now_ms() + 5000; g_light_state = 0; break;
    case 0: g_light_timer_end = now_ms() + 5000; g_light_state = 3; break;
    default: break;
    }
}

/* ---- set-up and the per-tick entry ---------------------------------------------------------------------------- */
static void load_tables(void)
{
    static const float C[11] = { 4320, 3600, 1920, 2640, 2400, 3840, 3240, 4440, 4200, 4224, 4272 };   /* x pi */
    static const uint32_t CA[11] = { 0x47f130, 0x47f134, 0x47f138, 0x47f13c, 0x47f140, 0x47f144, 0x47f148, 0x47f14c,
                                     0x47f150, 0x47f17c, 0x47f188 };
    PeFile pe;
    int ok = pe_open(&pe, "spel.dat");
    for (int i = 0; i < 11; i++) {
        const uint8_t *p = ok ? pe_ptr(&pe, CA[i], 4) : NULL;
        if (p) memcpy(&g_steer_c[i], p, 4); else g_steer_c[i] = (float)(C[i] * 3.14159265);
    }
    const uint8_t *p;
    if (ok && (p = pe_ptr(&pe, 0x482258, sizeof g_rubber_tab))) memcpy(g_rubber_tab, p, sizeof g_rubber_tab);
    if (ok && (p = pe_ptr(&pe, 0x484a48, sizeof g_traffic_types))) memcpy(g_traffic_types, p, sizeof g_traffic_types);
    else for (int i = 0; i < 12; i++) { g_traffic_types[i][0] = 9 + i % 7; g_traffic_types[i][1] = 2; }
    if (ok && (p = pe_ptr(&pe, 0x486690, sizeof g_chase_samples))) memcpy(g_chase_samples, p, sizeof g_chase_samples);
    if (ok && (p = pe_ptr(&pe, 0x4866c8, sizeof g_quit_samples))) memcpy(g_quit_samples, p, sizeof g_quit_samples);
    static const uint32_t arrest[3] = { 0x486648, 0x486660, 0x486678 };
    for (int k = 0; k < 3; k++)
        if (ok && (p = pe_ptr(&pe, arrest[k], 4 * (size_t)g_arrest_count[k]))) memcpy(g_arrest_samples[k], p, 4 * (size_t)g_arrest_count[k]);
    if (ok) pe_close(&pe);
}

static void read_options(void)
{
    char path[1024];
    res_user_path(path, sizeof path, "Game.ini");
    Ini *g = ini_load(path);
    g_difficulty = ini_int(g, "OPTIONS", "Difficulty", 1) != 0;
    g_dif_mod = ini_int(g, "OPTIONS", "dif_mod", 4);
    g_ai_model = ini_int(g, "OPTIONS", "ai_model", 0);
    g_demo = ini_int(g, "OPTIONS", "demo", 0);
    g_visible_chunks = ini_int(g, "OPTIONS", "visiblechunks", 30);
    if (g_visible_chunks < 10) g_visible_chunks = 10;
    if (g_visible_chunks > 50) g_visible_chunks = 50;
    memset(g_dt_hist, 0, sizeof g_dt_hist);
    g_dt_idx = 0;
    g_nr_laps = ini_int(g, "OPTIONS", "Nrlaps", 2);
    if (!R->track.city) g_nr_laps = 1;
    g_startchunk = ini_int(g, "OPTIONS", "STARTCHUNK", 0);
    g_direction = ini_int(g, "ETAPPE", "DIRECTION", 0);
    g_player_position = ini_int(g, "PLAYER", "position", 0) & 3;
    ini_free(g);
}

void ai_init_cops(Race *r)
{
    R = r;
    ai_free();
    read_options();
    load_tables();
    g_finish_chunk = -1;
    g_traffic_same_side = g_difficulty ? 10 : 5;                      /* game_init 0x423df7 */
    g_traffic_oncoming = g_difficulty ? 14 : 7;
    memset(g_car_list, 0, sizeof g_car_list);
    memset(g_cops, 0, sizeof g_cops);
    memset(g_cop_used, 0, sizeof g_cop_used);
    g_cop_last = 0;
    memset(g_fsm, 0, sizeof g_fsm);
    for (int i = 0; i < NFSM; i++) g_fsm[i].object = g_fsm[i].target = -1;
    for (int i = 0; i < 20; i++) g_active_cops[i] = -1;
    g_siren_count = g_cop_siren_active = g_siren_light_frame = 0;
    g_player_ai = -1;
    g_player_autopilot = 0;
    g_player_pos_frozen = 0;
    g_times_caught = 0;
    g_cops_action = 0;
    g_cop_chase_voice = g_cop_quit_voice = -1;
    g_light_state = 0;                                                /* sub_4295a0: red, timer 1 at 5000 ms */
    g_light_timer_end = 5000;
    int nw = r->track.nwaypoints;
    g_wp_n = calloc((size_t)nw + 1, sizeof *g_wp_n);
    g_wp_obj = malloc(((size_t)nw + 1) * sizeof *g_wp_obj);
    for (int i = 0; i <= nw; i++) for (int k = 0; k < WP_MAXOBJ; k++) g_wp_obj[i][k] = -1;
    init_ai();
    if (g_startchunk == 0) {                                          /* find_start_chunk 0x42a460: trigger 13, -1 */
        for (int c = 0; c < nchunks(); c++)
            if (CHUNK(c)->trigger[0] == 13 || CHUNK(c)->trigger[1] == 13) { g_startchunk = rceng_wrap_chunk(c - 1); break; }
    }
    for (int i = 0; i < r->objects.n; i++)
        if (r->objects.obj[i].type == OT_COP_AI) car_add_cop(&r->objects.obj[i]);
}

int ai_init_player(int car, int (*handler)(int o), int (*collide)(int o))
{
    int o = add_car(car, g_startchunk - g_player_position, 4, g_direction == 0 ? 1 : 0, 1, handler, collide, 1);
    R->plr_obj = o;
    /* g_plr_vmax 0x4d9e68 comes from the old model's car_calc_vmax 0x40ce10 (drag against drive force); the top
     * gear of CARS.DAT stands in for it (it only matters to the old racer and cop models) */
    const CarsDat *d = &R->cars[car];
    g_player_top_speed = d->vmax[R->upg_gearbox != 0][4 + (R->upg_gearbox != 0)];
    return o;
}

int ai_start_chunk(void) { return g_startchunk; }

void ai_init_cars(Race *r)
{
    R = r;
    for (int i = 0; i < NAI; i++)                                     /* cop_new ran before the player existed */
        if (g_ai_used[i] && g_ai[i].cop >= 0) g_cops[g_ai[i].cop].target = r->plr_obj;
    init_traffic();
    init_racer_cars();
    ai_fsm_init();
    {   /* init_adac_car 0x40c4f0: created at chunk 0 and switched off for good (dormant in the shipped game) */
        int side = g_direction == 0, lane = rceng_get_valid_lane(0, side, 4);
        g_adac_object = add_car(8, 0, lane, side, side, NULL, mesh_damage_collision_func, 5);
        if (g_adac_object >= 0) {
            add_ai(g_adac_object, 15, rceng_get_waypoint_index(0, lane, side), lane);
            OBJ(g_adac_object)->visible = OBJ(g_adac_object)->active = 0;
            ai_wp_remove(g_adac_object);
        }
    }
    r->progress = OBJ(r->plr_obj)->chunk - g_finish_chunk;            /* plr_init 0x427fd0 */
}

void ai_free(void)
{
    free(g_wp_n);
    free(g_wp_obj);
    g_wp_n = NULL;
    g_wp_obj = NULL;
}

void ai_tick(void)
{
    if (R->plr_obj < 0) return;
    traffic_lights_update();
    ai_check_cars_outside_handling_range();
    game_actions_run();
    if (R->damage >= 99 && R->game_event == 0) { R->game_over_reason = 0; ai_game_action_start(6); }   /* wrecked */
    if (g_ai_model == 0) {
        ai_fsm_check_all_transitions();
        ai_fsm_handle_all();
    }
    if (getenv("A2_TRACE") && R->tick % 30 == 0) {                   /* developer trace, once a second */
        int on = 0, n = 0;
        for (int i = 0; i < NCARLIST; i++)
            if (g_car_list[i].active && g_car_list[i].type == 2) { n++; on += OBJ(g_car_list[i].object)->active; }
        fprintf(stderr, "ai t%u event %d pos %d/4 player %d c%d traffic %d/%d on, light %d |", R->tick, R->game_event,
                g_player_position + 1, R->plr_obj, OBJ(R->plr_obj)->chunk, on, n, g_light_state);
        for (int i = 0; i < NFSM; i++)
            if (g_fsm[i].object >= 0)
                fprintf(stderr, " %s%d>%d st%d c%d v%.0f", i < 3 ? "R" : "C", i, g_fsm[i].target, g_fsm[i].state, OBJ(g_fsm[i].object)->chunk,
                        OBJ(g_fsm[i].object)->vel.z * 3.6f);
        fprintf(stderr, "\n");
    }
}

/* racers_finish_estimate 0x4114a0 and the [RACERS] lines of sub_422d40 */
void ai_save_racers(Ini *g, int player_position)
{
    int total = nchunks() * g_nr_laps;
    for (int r = 0; r < 3; r++) {
        if (g_racers[r].finished) continue;
        g_racers[r].time = (int)R->race_ms;
        int diff = R->progress - racer_progress(r);
        if (total - diff != 0) g_racers[r].time += g_racers[r].time / (total - diff) * diff;
    }
    for (int r = 0; r < 3; r++)
        for (int q = r + 1; q < 3; q++) if (g_racers[q].time == g_racers[r].time) g_racers[q].time++;
    for (int r = 0; r < 3; r++) {
        int pos = 0;
        for (int q = 0; q < 3; q++) if (g_racers[q].time < g_racers[r].time) pos++;
        if (pos >= player_position) pos++;
        char key[8], val[64];
        snprintf(key, sizeof key, "%d", r);
        snprintf(val, sizeof val, "%d,%d,%d", g_racers[r].car, pos, g_racers[r].time);
        ini_set(g, "RACERS", key, val);
    }
    ini_set_int(g, "RACERS", "count", 3);
}
