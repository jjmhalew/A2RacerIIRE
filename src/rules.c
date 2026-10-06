/* rules.c - see rules.h; the rules are in docs/GAMEFLOW.md 5. Time is the race's own clock in ms, advanced by the game
 * ticks (the original reads GetTickCount; its pause dialog blocks the whole game, so stopping the ticks is the same).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "rules.h"
#include "gfx.h"
#include "hud.h"
#include "ini.h"
#include "model.h"
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
    int caught, cop_obj;
    int gameover_reason;
    int flash;                  /* speed-camera flash, frames */
    int message, message_at;    /* 0x4882c0 / 0x4882c4 */
    int results_done, finish_sound;
    int pause;                  /* 1 pause, 2 quit question */
    int done;
    char font3d[64];            /* the charset of FONT3D_ANIM (0x486b66) */
    int anim_font3d;
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

static void set_state(Race *r, int s)                /* set_game_state 0x424d20 */
{
    G.prev_state = G.state;
    G.state = s;
    G.state_since = G.clock;
    r->game_event = s;
    if (s == GS_RACING) { r->race_started = 1; r->control = 1; }
    if (s == GS_QUIT) G.done = 1;
}

void rules_message(int text) { G.message = text; G.message_at = G.clock; }

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
    ini_free(g);
    G.speed_limit = -1;
    G.message = -1;
    G.last_check_chunk = -100;
    G.last_tick_sec = -1;
    /* the highway time limit (init_game 0x423d20) */
    static const int DIST[7] = { 0, 142, 0, 110, 0, 142, 0 };
    float kf = G.difficulty == 0 ? 0.4f : 0.65f;
    int ts = top_speed(r);
    G.limit_ms = (!r->track.city && r->etappe >= 0 && r->etappe < 7 && ts > 0) ? (int)(DIST[r->etappe] * 10 / (ts * kf)) * 1000 : 0;
    G.anim_font3d = anim_find("FONT3D_ANIM");
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {
        const char *cs = pe_str(&pe, 0x486b66);
        snprintf(G.font3d, sizeof G.font3d, "%s", cs ? cs : "");
        pe_close(&pe);
    }
    r->race_started = 0;
    r->control = 0;
    set_state(r, GS_START);
}

void rules_free(void) {}

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
        if (G.h.racers_start_clocks) G.h.racers_start_clocks();
        break;
    case 15: G.armed = 1; break;                     /* trigger_arm_finish 0x429a10 */
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
            if (G.h.racers_finish_estimate) G.h.racers_finish_estimate(G.tracktime);
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

void rules_add_damage(Race *r, float v)              /* add_damage 0x428390 */
{
    if (G.state != GS_RACING || G.invincible) return;
    if (v < 18000) return;
    if (v > 35000) v = 35000;
    int maxd = G.difficulty == 0 ? 6 : 12;
    int d = (int)(maxd * 0.01f * (int)(v * 0.0028571f));
    if (d < 0) d = 0;
    if (d > maxd) d = maxd;
    r->damage += d;
    if (r->damage > 100) r->damage = 100;
}

void rules_cop_caught(Race *r, int cop_obj)          /* cop_stop_init 0x415300 */
{
    if (G.state != GS_RACING) return;
    G.caught++;
    G.cop_obj = cop_obj;
    if (G.caught == 1) rules_message(49);
    if (G.caught == 2) rules_message(50);
    int base = G.caught == 1 ? SFX_COP1 : G.caught == 2 ? SFX_COP2 : SFX_COP3, cnt = G.caught == 1 ? 6 : 5;
    sfx_play_vol(base + rand() % cnt, -100);
    set_state(r, GS_COPSTOP);
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
    ini_set_int(g, "RACERS", "count", 3);
    for (int i = 0; i < 3; i++) {
        int car = 0, pos = i + 1, t = G.tracktime + 1000 * (i + 1);
        if (G.h.racer_result) G.h.racer_result(i, &car, &pos, &t);
        snprintf(k, sizeof k, "%d", i);
        snprintf(v, sizeof v, "%d,%d,%d", car, pos, t);
        ini_set(g, "RACERS", k, v);
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

static int continue_key(void)
{
    return plat_key_pressed(VK_ESCAPE) || plat_key_pressed(VK_RETURN) || plat_key_pressed(VK_SPACE)
        || (plat_pad()->pressed & (PAD_A | PAD_B | PAD_START));
}

void rules_tick(Race *r)
{
    G.clock += 1000 / RACE_HZ;
    if (G.flash > 0) G.flash--;
    int in_state = G.clock - G.state_since;
    switch (G.state) {
    case GS_START:                                   /* ev_stcam + countdown (ev_start, table 0x4869d8) */
        if (in_state >= 3000) {
            int step = (in_state - 3000) / 1000;
            if (step != G.countdown_step || G.countdown_at == 0) {
                if (step < 4 && (step != G.countdown_step || !G.countdown_at)) sfx_play(step < 3 ? SFX_GETSET : SFX_GO);
                G.countdown_step = step;
                G.countdown_at = G.clock;
            }
            if (step >= 4) {
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
        if (r->damage >= 99 && G.state == GS_RACING) { G.gameover_reason = 0; set_state(r, GS_GAMEOVER); }
        if (G.h.player_position) G.position = G.h.player_position();
        break;
    case GS_COPSTOP:                                 /* cop_stop_update: 4 s, the third time is an arrest */
        if (in_state >= 4000) {
            if (G.caught >= 3) { G.gameover_reason = 2; set_state(r, GS_GAMEOVER); }
            else set_state(r, GS_RACING);
        }
        break;
    case GS_FINISHED:                                /* finish_update 0x4155d0 */
        if (in_state >= 1500 && continue_key()) {
            int prize = prize_money(r);
            G.money += prize + 2000 * G.poles;       /* finish_done 0x415690 */
            if (G.money < 0) G.money = 0;
            if (G.money > 999999) G.money = 999999;
            write_results(r);
            set_state(r, GS_QUIT);
        }
        break;
    case GS_GAMEOVER:                                /* gameover_update 0x4162e0 */
        if (in_state >= 2500 && continue_key()) set_state(r, GS_QUIT);
        break;
    default: break;
    }
}

void rules_keys(Race *r)                             /* check_game_keys 0x424ee0 */
{
    if (G.pause) {                                   /* handle_pause 0x4248d0 */
        if (G.pause == 1 && plat_any_key_pressed()) { G.pause = 0; sfx_pause(0); }
        else if (G.pause == 2) {
            if (plat_key_pressed('J') || plat_key_pressed('Y') || (plat_pad()->pressed & PAD_A)) {
                G.pause = 0; sfx_pause(0); set_state(r, GS_QUIT);
            } else if (plat_key_pressed('N') || plat_key_pressed(VK_ESCAPE) || (plat_pad()->pressed & PAD_B)) {
                G.pause = 0; sfx_pause(0);
            }
        }
        return;
    }
    if (G.state != GS_FINISHED && G.state != GS_GAMEOVER) {
        if (plat_key_pressed('P')) { G.pause = 1; sfx_pause(1); }
        if (plat_key_pressed(VK_ESCAPE) || (plat_pad()->pressed & PAD_START)) { G.pause = 2; sfx_pause(1); }
    }
    if (plat_key('T')) sfx_play(SFX_TOETER);
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
    case GS_FINISHED: case GS_GAMEOVER: {            /* orbit around the car */
        float a = in_state * 0.0004f;
        *eye = v3_add(c->pos, v3(cosf(a) * 90, 35, sinf(a) * 90));
        *target = v3_add(c->pos, v3(0, 10, 0));
        return 1;
    }
    case GS_COPSTOP:
        if (G.cop_obj > 0 && G.cop_obj < MAX_OBJS && r->obj[G.cop_obj].used) {
            const Obj *k = &r->obj[G.cop_obj];
            *eye = v3_add(k->pos, v3(0, 30, 0));
            *eye = v3_add(*eye, v3_scale(v3_norm(v3_sub(k->pos, c->pos)), 50));
            *target = v3_add(c->pos, v3(0, 10, 0));
            return 1;
        }
        return 0;
    default: return 0;
    }
}

/* ---- 3D countdown letters (gfx_3dft.cpp: FONT3D_ANIM, 400 units in front of the car, spacing 12.5) ------------- */
void rules_draw_3d(Race *r)
{
    if (G.state != GS_START || G.clock - G.state_since < 3000 || G.countdown_step > 3) return;
    static const char *TXT[4] = { "3", "2", "1", "GO" };
    const char *s = TXT[G.countdown_step];
    const Obj *c = player(r);
    int n = (int)strlen(s);
    float yaw = -(c->yaw * 2 * PI_F / ANGLE_UNITS) + PI_F;        /* turned to face the car */
    for (int i = 0; i < n; i++) {
        const char *at = strchr(G.font3d, s[i]);
        int frame = at ? (int)(at - G.font3d) : 0;
        Vec3 p = car_point(c, v3((i - (n - 1) * 0.5f) * -12.5f, 15, 400));
        model_draw(anim_model(G.anim_font3d, frame), p, yaw);
    }
    gfx_set_model(NULL);
}

/* ---- overlay ----------------------------------------------------------------------------------------------------- */
static void fmt_time(char *b, size_t n, int ms)
{
    if (ms >= NEVER) snprintf(b, n, "--:--:-");
    else snprintf(b, n, "%02d:%02d:%d", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
}

void rules_draw_hud(Race *r)
{
    char b[96];
    int D = r->detail;
    if (G.flash) gfx_2d_rect(0, 0, 640, 480, 0xebebebffu);          /* speed-camera flash (palette colour 15) */
    if (G.state == GS_RACING || G.state == GS_COPSTOP) {
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
    }
    if (G.message >= 0 && G.clock - G.message_at < 2000) {          /* 4.5 messages */
        if ((G.clock - G.message_at) / 100 & 1) hud_item_text(76, 10, 1, hud_text(G.message));
    } else G.message = -1;
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
    if (G.state == GS_GAMEOVER) {                    /* ev_g_ovr.cpp: the two texts slide in */
        int t = G.clock - G.state_since;
        float k = t < 1000 ? t / 1000.0f : 1;
        int w1 = hud_text_width(14, hud_text(40 + G.gameover_reason)), w2 = hud_text_width(14, hud_text(43));
        hud_print(14, (int)(-w1 + (320 - w1 / 2 + w1) * k), 120, 0, hud_text(40 + G.gameover_reason));
        hud_print(14, (int)(640 - (320 + w2 / 2) * k), 240, 0, hud_text(43));
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
