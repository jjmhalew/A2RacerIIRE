/* rules.c - see rules.h; the rules are in docs/GAMEFLOW.md 5. Time is the race's own clock in ms, advanced by the game
 * ticks (the original reads GetTickCount; its pause dialog blocks the whole game, so stopping the ticks is the same).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "rules.h"
#include "effects.h"
#include "flag.h"
#include "fog.h"
#include "font3d.h"
#include "gfx.h"
#include "hud.h"
#include "ini.h"
#include "model.h"
#include "pad.h"
#include "plat.h"
#include "res.h"
#include "sfx.h"
#include "texture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEVER 0x0fffffff         /* "no record" */

/* gm_clock.cpp: 12 timers {duration, start, countdown}; here start = -1 = not running */
typedef struct { int duration, start, countdown, frozen, frozen_at; } Timer;

/* ev_g_ovr.cpp: a game-over text line (0x40-byte records at 0x4a7548 / 0x4a7588, ev_sweep_text_init 0x415c50) */
typedef struct {
    int active;                 /* +0x04: sweeping / drawn */
    int out;                    /* +0x08: 0 = sweeping in, 1 = back out */
    int frames, frame;          /* +0x20 = ftol(duration / frame dt), +0x24 */
    int y;                      /* +0x1c: top of the text */
    int text;                   /* +0x28: language text (drawn in font 14) */
} Sweep;

static struct {
    RulesHooks h;
    int state, prev_state;
    int clock;                  /* ms since the race was set up */
    int state_since;            /* clock when the state was entered */
    Timer t[12];
    /* start sequence */
    int countdown_step;         /* 0..4: "3", "2", "1", "GO", done */
    int countdown_at;
    /* laps and records */
    int nrlaps, lap, armed, started;
    int laptime[10], last_lap;
    int lap_record, race_record, tracktime;
    int nchecks, checks[10], rec_nchecks, rec_checks[10], last_check_chunk;
    int split, split_at;        /* city split time shown for 2 s */
    int extra_at;               /* highway "EXTRA TIJD" box */
    int limit_ms, last_tick_sec;
    /* money and events */
    int money, position, poles, radar_off_until_chunk;
    int speed_limit;            /* 0x48c480, -1 = none */
    int difficulty, copland, invincible;
    int gameover_reason;
    int flash;                  /* speed-camera flash, frames */
    int message_new;            /* 0x4882c0: posted, -1 = none; taken over by the dashboard outside events */
    int message, message_at;    /* 0x4882c4 and its start (0x4a7d84) */
    int pause;                  /* 1 pause, 2 quit question */
    int done;
    /* the end events */
    int ev_ticks;               /* game ticks (= the original's frames) since the state was entered */
    Vec3 fin_cam;               /* ev_finish_cam_init 0x4154d0: the still finish camera */
    int flag_started;           /* g_finish_flag_started 0x4a70ec: flag_start(1) done */
    int fin_key;                /* finish_update: a continue key went down; it ends when all are up again */
    Sweep sweep[2];             /* the game-over texts: reason (40..42) at H/4, "RACE OVER" (43) at H/2 */
    int smoke_at;               /* 0x4a75c4: the wreck smoke's 200 ms clock */
    char cd_text[4][11];        /* the countdown words and their font3d effect series (ev_start 0x4869d8) */
    int cd_serie[4];
    float prize, total;
} G;

static int timer_get(int i)                         /* timer_get 0x4214d0 */
{
    Timer *t = &G.t[i];
    if (t->start < 0) return t->countdown ? t->duration : 0;
    int el = (t->frozen ? t->frozen_at : G.clock) - t->start;
    if (t->countdown) return el >= t->duration ? 0 : t->duration - el;
    return el < t->duration ? el : t->duration;
}
static void timer_start(int i, int duration, int countdown)
{
    G.t[i].duration = duration;
    G.t[i].countdown = countdown;
    G.t[i].start = G.clock;
    G.t[i].frozen = 0;
}
static void timer_freeze(int i) { if (!G.t[i].frozen) { G.t[i].frozen = 1; G.t[i].frozen_at = G.clock; } }
static int timer_expired(int i) { return G.t[i].start >= 0 && G.t[i].countdown && timer_get(i) == 0; }

static void event_init(Race *r, int s);

static void set_state(Race *r, int s)                /* set_game_state 0x424d20 */
{
    G.prev_state = G.state;
    G.state = s;
    G.state_since = G.clock;
    G.ev_ticks = 0;
    r->game_event = s;
    /* g_event_running: no dashboard (and no messages) during the finish, the game over and the police stop */
    r->event_running = s == GS_FINISHED || s == GS_GAMEOVER || s == GS_COPSTOP;
    if (s == GS_RACING) { r->race_started = 1; r->control = 1; }
    if (s == GS_QUIT) G.done = 1;
    event_init(r, s);                                /* the init callback of the state's game action */
}

void rules_message(int text) { G.message_new = text; }

static Obj *player(Race *r) { return &r->obj[r->plr_obj]; }

static Vec3 car_fwd(const Obj *c)
{
    float a = c->yaw * 2 * PI_F / ANGLE_UNITS;
    return v3(-sinf(a), 0, cosf(a));
}

static Vec3 car_point(const Obj *c, Vec3 off)
{
    Vec3 f = car_fwd(c), rt = v3(f.z, 0, -f.x);
    return v3_add(c->pos, v3_add(v3_scale(rt, off.x), v3_add(v3(0, off.y, 0), v3_scale(f, off.z))));
}

/* ---- set-up ------------------------------------------------------------------------------------------------------ */
static int top_speed(Race *r)                        /* get_car_top_speed 0x410b00 */
{
    const CarsDat *d = &r->cars[r->plr_car];
    float v = r->upg_gearbox ? d->vmax[1][5] : d->vmax[0][4];
    return (int)(v + (r->upg_motor ? 4.0f : 0.0f));
}

void rules_init(Race *r, const RulesHooks *hooks)
{
    memset(&G, 0, sizeof G);
    if (hooks) G.h = *hooks;
    for (int i = 0; i < 12; i++) G.t[i].start = -1;
    char p[1024], k[64];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    ini_set_int(g, "PLAYER", "finished", 0);         /* at start-up, so a crash counts as not finished (0x421b40) */
    ini_save(g, p);
    G.difficulty = ini_int(g, "OPTIONS", "Difficulty", 1);
    G.nrlaps = r->track.city ? ini_int(g, "OPTIONS", "Nrlaps", 2) : 1;
    if (G.nrlaps < 1) G.nrlaps = 1;
    if (G.nrlaps > 10) G.nrlaps = 10;                /* the lap arrays hold 10 */
    G.money = ini_int(g, "PLAYER", "money", 0);
    G.copland = ini_int(g, "PLAYER", "copland", 0);
    r->damage = ini_int(g, "PLAYER", "damage", 0);
    snprintf(k, sizeof k, "LAP_RECORD_E%d", r->etappe);
    G.lap_record = ini_int(g, "BEST TIMES", k, 0);
    if (!G.lap_record) G.lap_record = NEVER;
    snprintf(k, sizeof k, "RACE_RECORD_E%d", r->etappe);
    G.race_record = ini_int(g, "BEST TIMES", k, 0);
    if (!G.race_record) G.race_record = NEVER;
    snprintf(k, sizeof k, "TIME_CHECK_COUNT_E%d", r->etappe);
    G.rec_nchecks = ini_int(g, "BEST TIMES", k, 0);
    if (G.rec_nchecks > 9) G.rec_nchecks = 9;
    for (int i = 0; i < G.rec_nchecks; i++) {
        snprintf(k, sizeof k, "TIME_CHECK_E%d_C%d", r->etappe, i);
        G.rec_checks[i] = ini_int(g, "BEST TIMES", k, 0);
    }
    flag_init(ini_int(g, "GFX OPTIONS", "finish_flag", 1), 1.0f / RACE_HZ);     /* init_game: flag_init 0x41aea0 */
    ini_free(g);
    G.speed_limit = -1;
    G.message = G.message_new = -1;
    G.last_check_chunk = -100;
    G.last_tick_sec = -1;
    /* the highway time limit (init_game 0x423d20) */
    static const int DIST[7] = { 0, 142, 0, 110, 0, 142, 0 };
    float kf = G.difficulty == 0 ? 0.4f : 0.65f;
    int ts = top_speed(r);
    G.limit_ms = (!r->track.city && r->etappe >= 0 && r->etappe < 7 && ts > 0) ? (int)(DIST[r->etappe] * 10 / (ts * kf)) * 1000 : 0;
    font3d_init(1.0f / RACE_HZ);
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {                  /* the countdown: {char text[10]; int sound, serie;} x 4 */
        for (int i = 0; i < 4; i++) {
            const uint8_t *e = pe_ptr(&pe, 0x4869d8 + i * 18, 18);
            if (!e) break;
            memcpy(G.cd_text[i], e, 10);
            G.cd_serie[i] = (int)pe_u32(&pe, 0x4869d8 + i * 18 + 14);
        }
        pe_close(&pe);
    }
    r->race_started = 0;
    r->control = 0;
    set_state(r, GS_START);
}

void rules_free(void) { font3d_free(); flag_free(); }

/* ---- triggers ---------------------------------------------------------------------------------------------------- */
static int moving_forward(int prev, int cur, int n)
{
    int d = cur - prev;
    if (d < -n / 2) d += n;
    if (d > n / 2) d -= n;
    return d > 0;
}

static void finish_lap(void)                         /* finish_lap 0x429860 */
{
    int t = timer_get(0);
    G.last_lap = t;
    if (G.lap < 10) G.laptime[G.lap] = t;
    G.lap = G.lap + 1 < G.nrlaps ? G.lap + 1 : G.nrlaps;
    if (t < G.lap_record) {
        G.lap_record = t;
        memcpy(G.rec_checks, G.checks, sizeof G.checks);
        G.rec_nchecks = G.nchecks;
        rules_message(31);
    } else rules_message(45);
    G.nchecks = 0;
    timer_start(0, 10000 * 1000, 0);
}

static void trigger(Race *r, int id, int prev, int cur)
{
    int n = r->track.nchunks, fwd = moving_forward(prev, cur, n);
    switch (id) {
    case 1:                                          /* speed camera 0x42a2e0 */
        if (cur < G.radar_off_until_chunk) break;
        if (r->speed_kmh > G.speed_limit && fwd) { sfx_play(SFX_FLITS); G.flash = 1; }
        break;
    case 2:                                          /* quick service 0x42a4d0 */
        r->damage = 0;
        if (r->upg_turbo) r->turbo = 100;
        sfx_play(SFX_REPAIR);
        rules_message(48);
        break;
    case 4: G.speed_limit = 50; break;               /* speed limits */
    case 5: G.speed_limit = 70; break;
    case 6: G.speed_limit = 100; break;
    case 8: G.speed_limit = 120; break;
    case 13:                                         /* trigger_race_start 0x42a420, once */
        if (G.started) break;
        G.started = 1;
        timer_start(0, 10000 * 1000, 0);
        timer_start(3, 0x7fffffff, 0);
        if (G.limit_ms) timer_start(11, G.limit_ms, 1);
        break;
    case 15: G.armed = 1; break;                     /* trigger_arm_finish 0x429a10 */
    case 16: fog_trigger_fade_start(); break;        /* 0x429ad0 / 0x429b20 (fog.c; on no track of the CD) */
    case 17: fog_trigger_fade_end(); break;
    case 14:                                         /* trigger_finish 0x429950 */
        if (!(G.state == GS_RACING || G.state == 7 || G.state == GS_COPSTOP) || !G.armed) break;
        if (!fwd) { G.armed = 0; break; }            /* "trying to finish illegal" */
        G.armed = 0;
        finish_lap();
        if (G.lap >= G.nrlaps) {
            G.tracktime = timer_get(3);
            timer_freeze(3);
            if (G.tracktime < G.race_record) G.race_record = G.tracktime;
            G.position = G.h.player_position ? G.h.player_position() : 0;
            set_state(r, GS_FINISHED);
            sfx_play(SFX_JUICH);
        }
        break;
    case 45:                                         /* trigger_time_check 0x42a530 */
        if (abs(cur - G.last_check_chunk) <= 1) break;
        G.last_check_chunk = cur;
        if (!r->track.city) {
            if (G.t[11].start >= 0) timer_start(11, timer_get(11) + G.limit_ms, 1);
            sfx_play(SFX_CHECKP);
            G.extra_at = G.clock;
        } else {
            int t = timer_get(0);
            int rec = G.nchecks < G.rec_nchecks && G.lap_record != NEVER ? G.rec_checks[G.nchecks] : 0;
            G.split = t - rec;
            G.split_at = G.clock;
            if (G.nchecks < 10) G.checks[G.nchecks++] = t;
        }
        rules_message(44);
        break;
    default: break;                                  /* lane / speed triggers of the traffic: ai.c */
    }
}

void rules_player_chunk_changed(Race *r, int prev, int cur)
{
    int n = r->track.nchunks;
    /* rceng_check_chunk_triggers 0x442590: the higher of the two chunks, chunk 0 at the wrap; every chunk passed */
    if (prev == cur) return;
    int k = (prev == n - 1 && cur == 0) || (prev == 0 && cur == n - 1) ? 0 : (prev > cur ? prev : cur);
    for (int i = 0; i < 2; i++) if (r->track.chunks[k].trigger[i]) trigger(r, r->track.chunks[k].trigger[i], prev, cur);
}

void rules_radar_bonus(Race *r)
{
    G.poles++;
    rules_message(51);
    G.radar_off_until_chunk = player(r)->chunk + 20;
}

/* ---- per tick ---------------------------------------------------------------------------------------------------- */
static void write_results(Race *r)                   /* exit_game 0x421af0 -> ini_save_all 0x422d30 */
{
    char p[1024], k[64], v[64];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    ini_set_int(g, "PLAYER", "finished", 1);
    ini_set_int(g, "PLAYER", "money", G.money);
    ini_set_int(g, "PLAYER", "damage", r->damage);
    ini_set_int(g, "PLAYER", "tracktime", G.tracktime);
    ini_set_int(g, "PLAYER", "position", G.position);
    if (G.h.save_racers) G.h.save_racers(g, G.position);
    else {
        ini_set_int(g, "RACERS", "count", 3);
        for (int i = 0; i < 3; i++) {                /* no racers: behind the player, a second apart */
            snprintf(k, sizeof k, "%d", i);
            snprintf(v, sizeof v, "%d,%d,%d", i, i + (i >= G.position), G.tracktime + 1000 * (i + 1));
            ini_set(g, "RACERS", k, v);
        }
    }
    snprintf(k, sizeof k, "LAP_RECORD_E%d", r->etappe);
    ini_set_int(g, "BEST TIMES", k, G.lap_record == NEVER ? 0 : G.lap_record);
    snprintf(k, sizeof k, "RACE_RECORD_E%d", r->etappe);
    ini_set_int(g, "BEST TIMES", k, G.race_record == NEVER ? 0 : G.race_record);
    if (r->track.city) {
        snprintf(k, sizeof k, "TIME_CHECK_COUNT_E%d", r->etappe);
        ini_set_int(g, "BEST TIMES", k, G.rec_nchecks);
        for (int i = 0; i < G.rec_nchecks; i++) {
            snprintf(k, sizeof k, "TIME_CHECK_E%d_C%d", r->etappe, i);
            ini_set_int(g, "BEST TIMES", k, G.rec_checks[i]);
        }
    }
    ini_save(g, p);
    ini_free(g);
}

static int prize_money(Race *r)                      /* get_prize_money 0x4280c0 */
{
    static const int P[4] = { 55000, 35000, 15000, 0 };
    int pos = G.position < 0 ? 0 : G.position > 3 ? 3 : G.position;
    int m = P[pos] * (r->etappe == 6 ? 4 : 1);
    if (pos == 3 && G.difficulty == 0) m = 5000;
    return m;
}

/* Esc, Enter or Space held (key_down 0x466910 = GetAsyncKeyState), or a controller's A / B / Start / pause action */
static int continue_key_down(void)
{
    return plat_key(VK_ESCAPE) || plat_key(VK_RETURN) || plat_key(VK_SPACE)
        || (plat_pad()->buttons & (PAD_A | PAD_B | PAD_START)) || pad_held(PA_PAUSE);
}

/* ---- the end events (ev_main.cpp game actions 4 and 12) ---------------------------------------------------------- */
#define SWEEP_UNITS 1024                             /* the sweep's phase runs 0..1024 of 4096 = a quarter turn */
#define ORBIT_TURN_S 8                               /* gameover_init: 4096 / ftol(8 / dt) per frame */

static void sweep_init(Sweep *s, int text, int y, float seconds, int active)    /* ev_sweep_text_init 0x415c50 */
{
    s->text = text;
    s->y = y;
    s->frames = (int)(seconds * RACE_HZ + 0.5f);     /* ftol(seconds / frame dt) */
    if (s->frames < 1) s->frames = 1;
    s->frame = 0;
    s->out = 0;
    s->active = active;
}

/* ev_sweep_text_draw 0x415e80, its counting part (the drawing is in sweep_draw): 1 = in place / gone */
static int sweep_step(Sweep *s)
{
    if (!s->active) return 0;
    if (s->frame == s->frames - 1 && !s->out) return 1;           /* in place: drawn whole */
    if (!s->out) {
        if (++s->frame >= s->frames) { s->frame = s->frames - 1; return 1; }
    } else if (--s->frame < 0) { s->frame = 0; return 1; }
    return 0;
}

static void event_init(Race *r, int s)
{
    const Obj *c = player(r);
    switch (s) {
    case GS_FINISHED: {                              /* ev_finish_cam_init 0x4154d0 */
        /* camera slot 5: fixed at the road point of the chunk 3 ahead of the player + 40 up, looking at (0, 20, 0)
         * on the car, world up; juich.wav (played by trigger_finish here) */
        const Chunk *ch = &r->track.chunks[track_wrap(&r->track, c->chunk + 3)];
        G.fin_cam = v3(ch->pos.x, ch->pos.y + 40, ch->pos.z);
        G.fin_key = 0;
        break;
    }
    case GS_GAMEOVER: {                              /* ev_gameover_init 0x416060 */
        r->cam_mode = 1;                             /* set_player_cam_mode(1): the orbit starts from the chase view */
        int reason = G.gameover_reason < 0 || G.gameover_reason > 2 ? 0 : G.gameover_reason;
        sweep_init(&G.sweep[0], 40 + reason, 480 / 4, 1.5f, 1);
        sweep_init(&G.sweep[1], 43, 480 / 2, 3.0f, 0);
        G.smoke_at = G.clock - 200;
        break;
    }
    default: break;
    }
}

void rules_tick(Race *r)
{
    G.clock += 1000 / RACE_HZ;
    if (G.flash > 0) G.flash--;
    if (r->game_event != G.state) {                  /* an action started by ai.c: the arrest, its end, game over */
        if (r->game_event == GS_GAMEOVER) G.gameover_reason = r->game_over_reason;
        set_state(r, r->game_event);
    }
    if (r->hud_message >= 0) { rules_message(r->hud_message); r->hud_message = -1; }
    if (G.invincible) r->damage = 0;
    int in_state = G.clock - G.state_since;
    switch (G.state) {
    case GS_START:                                   /* ev_stcam + countdown (ev_start, table 0x4869d8) */
        if (in_state >= 3000) {
            int step = (in_state - 3000) / 1000;
            if (step != G.countdown_step || G.countdown_at == 0) {
                if (step < 4 && (step != G.countdown_step || !G.countdown_at)) {
                    sfx_play(step < 3 ? SFX_GETSET : SFX_GO);
                    /* countdown_init 0x416b00: a = 400 along the car's z axis + 30 up, b = that axis flat */
                    const Obj *c = player(r);
                    Mat4 m = font3d_inst_matrix(c->pos, c->pitch, c->yaw, c->roll);
                    Vec3 f = v3(m.m[8], m.m[9], m.m[10]);
                    font3d_text(G.cd_text[step], v3_add(v3_add(c->pos, v3_scale(f, 400)), v3(0, 30, 0)),
                                v3(f.x, 0, f.z), G.cd_serie[step]);      /* countdown_update 0x416bd0 */
                }
                G.countdown_step = step;
                G.countdown_at = G.clock;
            }
            if (step >= 3) {                        /* "GO?": the race starts in the same frame (0x416bd0) */
                set_state(r, GS_RACING);
                r->cam_mode = 1;
                if (G.copland) rules_message(47);
            }
        }
        break;
    case GS_RACING:
        if (G.t[11].start >= 0 && !r->track.city) {  /* highway time limit */
            int left = timer_get(11);
            int sec = left / 1000;
            if (left < 6000 && sec != G.last_tick_sec) { G.last_tick_sec = sec; sfx_play(SFX_TICK); }
            if (timer_expired(11)) { G.gameover_reason = 1; set_state(r, GS_GAMEOVER); }
        }
        if (G.h.player_position) G.position = G.h.player_position();
        if (getenv("A2_TEST_FINISH") && in_state >= atoi(getenv("A2_TEST_FINISH")) * 1000) {  /* tests: finish now */
            G.lap = G.nrlaps - 1;
            G.armed = 1;
            trigger(r, 14, 0, 1);
        }
        if (getenv("A2_TEST_GAMEOVER") && in_state >= atoi(getenv("A2_TEST_GAMEOVER")) * 1000) {
            /* tests: "N[,reason]" = game over N s after the start (reason 0 total loss, 1 time up, 2 arrested) */
            const char *k = strchr(getenv("A2_TEST_GAMEOVER"), ',');
            G.gameover_reason = r->game_over_reason = k ? atoi(k + 1) : 0;
            if (G.gameover_reason == 0) r->damage = 100;
            set_state(r, GS_GAMEOVER);
        }
        break;
    /* GS_COPSTOP: the 4 s stop and the third arrest's game over are run by ai.c (game_actions_run) */
    case GS_FINISHED:                                /* finish_update 0x4155d0 */
        /* the results panel after 1500 ms (rules_draw_hud); then Esc / Enter / Space, and the event ends when all
         * three are released again (it waits for that in a loop); A2_TEST_FINISH: 3 s after the finish */
        if (in_state >= 1500) {
            if (!G.flag_started) { flag_start(1); G.flag_started = 1; }   /* the waving flag (hw accel only) */
            if (continue_key_down()) G.fin_key = 1;
            else if (G.fin_key || (getenv("A2_TEST_FINISH") && in_state >= 3000)) {
                int prize = prize_money(r);
                G.money += prize + 2000 * G.poles;   /* finish_done 0x415690 */
                if (G.money < 0) G.money = 0;
                if (G.money > 999999) G.money = 999999;
                write_results(r);
                set_state(r, GS_QUIT);
            }
        }
        break;
    case GS_GAMEOVER: {                              /* gameover_update 0x4162e0 */
        if (r->damage >= 99 && G.clock - G.smoke_at >= 200) {      /* 0x4159a0: the wreck smokes */
            G.smoke_at = G.clock;
            const Obj *c = player(r);
            float fwd = (float)(rand() % 6 - 3 + 16), side = (float)(rand() % 18 - 9);
            Vec3 f = car_fwd(c), rt = v3(f.z, 0, -f.x);
            Vec3 p = v3_add(c->pos, v3_add(v3_scale(rt, side), v3_scale(f, fwd)));
            p.y = c->pos.y + 5;
            fx_cloud(p, v3(0, 0, 0), r->dt);
        }
        /* the reason sweeps in (1.5 s); then "RACE OVER" (3 s); then the reason sweeps back out */
        int r1 = sweep_step(&G.sweep[0]), r2 = sweep_step(&G.sweep[1]);
        if (r2) G.sweep[0].out = 1;
        if (r1) G.sweep[1].active = 1;
        /* Esc / Enter / Space held, from 1000 ms on (no release wait); A2_TEST_GAMEOVER: after 8 s */
        if (in_state >= 1000 && (continue_key_down() || (getenv("A2_TEST_GAMEOVER") && in_state >= 8000)))
            set_state(r, GS_QUIT);
        break;
    }
    default: break;
    }
    G.ev_ticks++;
    font3d_tick(player(r)->pos);                     /* the 3D letters (run by font3d_draw 0x417870 per frame) */
    flag_tick();                                     /* the flag's wave (flag_draw 0x41aee0 counts the frames) */
}

void rules_keys(Race *r)                             /* check_game_keys 0x424ee0 */
{
    if (G.pause) {                                   /* handle_pause 0x4248d0 */
        /* PORT: the pads answer the quit question with A (yes) and B / Start / the pause action (no) */
        unsigned pp = plat_pad()->pressed;
        if (G.pause == 1 && (plat_any_key_pressed() || pp)) { G.pause = 0; sfx_pause(0); }
        else if (G.pause == 2) {
            if (plat_key_pressed('J') || plat_key_pressed('Y') || (pp & PAD_A)) {
                G.pause = 0; sfx_pause(0); set_state(r, GS_QUIT);
            } else if (plat_key_pressed('N') || plat_key_pressed(VK_ESCAPE) || (pp & (PAD_B | PAD_START))
                       || pad_pressed(PA_PAUSE)) {
                G.pause = 0; sfx_pause(0);
            }
        }
        return;
    }
    if (G.state != GS_FINISHED && G.state != GS_GAMEOVER) {
        if (plat_key_pressed('P')) { G.pause = 1; sfx_pause(1); }
        if (plat_key_pressed(VK_ESCAPE) || pad_pressed(PA_PAUSE)) { G.pause = 2; sfx_pause(1); }
    }
    if (plat_key('T') || pad_held(PA_HORN)) sfx_play(SFX_TOETER);
    if (plat_key(VK_CONTROL) && plat_key(VK_SPACE)) {
        if (plat_key_pressed('T') && !r->track.city && G.t[11].start >= 0) { timer_start(11, 180000, 1); rules_message(44); }
        if (plat_key_pressed('I')) { G.invincible = 1; rules_message(46); }
    }
}

int rules_paused(void) { return G.pause != 0; }
int rules_done(void) { return G.done; }
int rules_hud_on(void) { return G.state == GS_RACING || G.state == GS_START || G.state == GS_COPSTOP; }
int rules_race_ms(void) { return timer_get(3); }
int rules_lap_ms(void) { return timer_get(0); }
int rules_lap(void) { return G.lap; }
int rules_nrlaps(void) { return G.nrlaps; }
int rules_lap_record(void) { return G.lap_record; }
int rules_race_record(void) { return G.race_record; }
int rules_last_lap(void) { return G.lap ? G.last_lap : NEVER; }
int rules_position(void) { return G.position; }

/* ---- cameras ----------------------------------------------------------------------------------------------------- */
int rules_camera(Race *r, Vec3 *eye, Vec3 *target)
{
    const Obj *c = player(r);
    int in_state = G.clock - G.state_since;
    switch (G.state) {
    case GS_START: {                                 /* ev_stcam 0x416ce0: 1 s still, then 2 s flying in */
        Vec3 a = v3(-120, 100, 200), b = v3(0, 20, -60);
        float t = in_state < 1000 ? 0 : (in_state - 1000) / 2000.0f;
        if (t > 1) t = 1;
        *eye = car_point(c, v3_lerp(a, b, t));
        *target = car_point(c, v3(0, 10, 0));
        return in_state < 3000;
    }
    case GS_FINISHED:                                /* ev_finish_cam_init 0x4154d0: still, follows the car */
        *eye = G.fin_cam;
        *target = car_point(c, v3(0, 20, 0));
        return 1;
    case GS_GAMEOVER: {
        /* game_over_camera 0x415b20 + gameover_update 0x4162e0: camera slot 6 locked to the car with the chase
         * offsets of view 1, (0, 25, -60) -> radius 60 at height 25, looking at (0, 10, 0); undamped. The offset
         * angle starts at -1024 (behind) and grows by 4096 / ftol(8 / dt) per frame: one turn in 8 s. */
        int a = -1024 + 4096 / (ORBIT_TURN_S * RACE_HZ) * G.ev_ticks;
        *eye = car_point(c, v3(race_cos(a) * 60, 25, race_sin(a) * 60));
        *target = car_point(c, v3(0, 10, 0));
        return 1;
    }
    default: return 0;
    }
}

/* ---- the last passes of render_game: the finish flag (gfx_flag.cpp, src/flag.c; started by the finish in
 * rules_tick), then the 3D countdown letters (gfx_3dft.cpp, src/font3d.c; started by the countdown) ------------- */
void rules_draw_3d(Race *r)
{
    float a = (float)(r->accum * RACE_HZ);           /* the same interpolation as race_draw */
    int w, h;
    plat_size(&w, &h);
    flag_draw(v3_lerp(r->cam_draw0, r->cam_draw1, a), v3_lerp(r->tgt_draw0, r->tgt_draw1, a),
              h > 0 ? (float)w / h : 4.0f / 3.0f, a);
    font3d_draw(a);
}

/* ---- overlay ----------------------------------------------------------------------------------------------------- */
static void fmt_time(char *b, size_t n, int ms)
{
    if (ms >= NEVER) snprintf(b, n, "--:--:-");
    else snprintf(b, n, "%02d:%02d:%d", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
}

/* ev_sweep_text_draw 0x415e80, the drawing: the text (font 14, centred on x, its top at y) whole when in place;
 * else every scanline on its own, the odd ones coming from the left edge, the even ones from the right, both
 * reaching the centre at s = 1, with s = sin((1024 / frames) * frame) (4096 units per turn). alpha (0..1)
 * interpolates between the game ticks */
static void sweep_draw(const Sweep *s, float alpha)
{
    if (!s->active) return;
    const char *t = hud_text(s->text);
    int W = 640, w = hud_text_width(14, t), cx = (W - w) / 2;
    if (s->frame == s->frames - 1 && !s->out) { hud_print(14, cx, s->y, 0, t); return; }
    float f = s->frame + (s->out ? -alpha : alpha);
    if (f < 0) f = 0;
    if (f > s->frames - 1) f = (float)(s->frames - 1);
    float k = sinf((float)(SWEEP_UNITS / s->frames) * f * 2 * PI_F / ANGLE_UNITS);
    for (int i = 0, h = hud_font_height(14); i < h; i++) {
        int x = i & 1 ? (int)(k * (cx + w)) - w : W - (int)(k * (W - cx));
        if (x + w > 0 && x < W) hud_print_row(14, x, s->y, i, t);
    }
}

void rules_draw_hud(Race *r)
{
    char b[96];
    int D = r->detail;
    if (G.flash) gfx_2d_rect(0, 0, 640, 480, 0xebebebffu);          /* speed-camera flash (palette colour 15) */
    if (!r->event_running) {                         /* draw_dashboard 0x41a8a0: nothing of it during an event */
        if (!r->track.city && D >= 3 && G.t[11].start >= 0) {      /* 4.4 checkpoint box and the time left */
            hud_item(73, 0);
            hud_item_text(74, 10, 1, "CHECKPOINT");
            fmt_time(b, sizeof b, timer_get(11));
            hud_item_text(75, 11, 1, b);
            if (G.extra_at && G.clock - G.extra_at < 2000 && ((G.clock - G.extra_at) / 100 & 1))
                hud_item_text(76, 10, 1, "EXTRA TIJD");
        }
        if (r->track.city && G.split_at && G.clock - G.split_at < 2000 && ((G.clock - G.split_at) / 100 & 1)) {
            int s = abs(G.split);
            snprintf(b, sizeof b, "%c%02d.%d", G.split < 0 ? '-' : '+', s / 1000 % 60, s / 100 % 10);
            hud_item_text(77, 15, 1, b);
        }
        /* 4.5 the U-turn sign when driving against the track */
        const Obj *c = player(r);
        const Chunk *ch = &r->track.chunks[c->chunk];
        if (v3_dot(ch->fwd, car_fwd(c)) < 0 && (G.clock / 300 & 1)) hud_item(29, 0);
        /* 4.5 messages: a posted message starts its 2 s here, so one posted during an event (the police warnings,
         * the finish lap) waits until the event is over */
        if (G.message_new >= 0) { G.message = G.message_new; G.message_at = G.clock; G.message_new = -1; }
        if (G.message >= 0 && G.clock - G.message_at <= 2000) {
            if ((G.clock - G.message_at) / 100 & 1) hud_item_text(76, 10, 1, hud_text(G.message));
        } else G.message = -1;
    }
    if (G.state == GS_FINISHED && G.clock - G.state_since >= 1500) {   /* 4.7 results */
        int prize = prize_money(r), bonus = 2000 * G.poles;
        hud_print(14, 68, 78, 0, hud_text(17));
        if (r->track.city) {
            hud_item(62, 0);
            fmt_time(b, sizeof b, G.laptime[0]); hud_item_text(65, 15, 0, b);
            fmt_time(b, sizeof b, G.nrlaps > 1 ? G.laptime[1] : NEVER); hud_item_text(66, 15, 0, b);
            fmt_time(b, sizeof b, G.tracktime); hud_item_text(67, 15, 0, b);
            snprintf(b, sizeof b, " %d", G.money); hud_item_text(68, 15, 0, b);
            snprintf(b, sizeof b, "+%d", prize); hud_item_text(69, 15, 0, b);
            snprintf(b, sizeof b, "+%d", bonus); hud_item_text(70, 15, 0, b);
            snprintf(b, sizeof b, " %d", G.money + prize + bonus); hud_item_text(71, 15, 0, b);
        } else {
            hud_item(72, 0);
            fmt_time(b, sizeof b, G.tracktime); hud_item_text(82, 15, 0, b);
            snprintf(b, sizeof b, " %d", G.money); hud_item_text(78, 15, 0, b);
            snprintf(b, sizeof b, "+%d", prize); hud_item_text(79, 15, 0, b);
            snprintf(b, sizeof b, "+%d", bonus); hud_item_text(80, 15, 0, b);
            snprintf(b, sizeof b, " %d", G.money + prize + bonus); hud_item_text(81, 15, 0, b);
        }
        hud_item(64, G.position);
    }
    if (G.state == GS_GAMEOVER) {                    /* ev_g_ovr.cpp: the two texts sweep in */
        float alpha = (float)(r->accum * RACE_HZ);
        sweep_draw(&G.sweep[0], alpha);
        sweep_draw(&G.sweep[1], alpha);
    }
    if (G.pause) {                                   /* 4.6 pause / quit dialog */
        hud_item(34, 0);
        int base = G.pause == 1 ? 24 : 27;
        hud_item_text(35, 10, 1, hud_text(base));
        hud_item_text(36, 6, 1, hud_text(base + 1));
        hud_item_text(37, 6, 1, hud_text(base + 2));
    }
}

void rules_loading_screen(int etappe, int step)       /* handle_progress_bar 0x41c410, gfx_draw_wait_screen 0x41bde0 */
{
    char p[1024], k[16], name[64];
    res_user_path(p, sizeof p, "Raser.ini");
    Ini *ini = ini_load(p);
    snprintf(k, sizeof k, "%d", etappe);
    ini_str(ini, "wait_hi", k, "e01_w_hi.pcx", name, sizeof name);
    snprintf(k, sizeof k, "percent%d", step);
    int pct = ini_int(ini, "progress", k, step * 10);
    ini_free(ini);
    const Texture *t = tex_get(name);                  /* the 16-bit mode loads the name with .tga */
    int w, h;
    plat_size(&w, &h);
    gfx_begin(w, h, 0, 0, 0);
    gfx_2d_begin(640, 480);
    if (t) gfx_2d_quad(t->id, 0, 0, 640, 480, 0, 0, 1, 1, 0xffffffffu, BLEND_NONE);
    hud_item_part(14, 0, pct / 100.0f);
    gfx_2d_end();
    plat_swap();
}
