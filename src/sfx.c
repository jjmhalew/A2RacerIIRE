/* sfx.c - see sfx.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "sfx.h"
#include "audio.h"
#include "exetab.h"
#include "ini.h"
#include "res.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_name[SFX_COUNT][16];
static Sample g_smp[SFX_COUNT];
static int g_tried[SFX_COUNT];
static int g_on = 1, g_sfxvol, g_3d = 1;
static int g_motor = -1, g_toeren = -1, g_slip = -1, g_rattle1 = -1, g_rattle2 = -1, g_turbo = -1;
static Vec3 g_lpos, g_lright;
typedef struct { int key, idx, voice, freq_detune; } Slot;
static Slot g_slot[20];                  /* 0x4da1b0: 20 positional sounds at once */

/* 1/100 dB (0 = full, -10000 = silent) -> amplitude */
static float db(int v) { return v <= -10000 ? 0 : powf(10.0f, v / 2000.0f); }

static const Sample *smp(int i)
{
    if (i <= 0 || i >= SFX_COUNT || !g_name[i][0]) return NULL;
    if (!g_tried[i]) {
        g_tried[i] = 1;
        char n[24];
        snprintf(n, sizeof n, "%s", g_name[i]);
        if (!sample_load(n, &g_smp[i])) fprintf(stderr, "sound %s missing\n", n);
    }
    return g_smp[i].data ? &g_smp[i] : NULL;
}

void sfx_init(void)
{
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {
        for (int i = 0; i < SFX_COUNT; i++) {        /* {char name[15]; int preload;} x 86 */
            const char *s = pe_str(&pe, 0x48baf0 + (uint32_t)i * 19);
            snprintf(g_name[i], sizeof g_name[i], "%s", s ? s : "");
        }
        pe_close(&pe);
    }
    char p[1024];
    res_user_path(p, sizeof p, "Raser.ini");
    Ini *r = ini_load(p);
    g_on = ini_int(r, "SOUND", "ON", 1);
    g_sfxvol = ini_int(r, "SOUND", "SFXVOLUME", 100);
    ini_free(r);
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    g_3d = ini_int(g, "OPTIONS", "3dsound", 1);
    ini_free(g);
    if (g_sfxvol == 0) g_on = 0;                    /* "All return at once when SFXVOLUME = 0" */
    memset(g_slot, 0, sizeof g_slot);
    for (int i = 0; i < 20; i++) g_slot[i].key = -1;
}

void sfx_free(void)
{
    sfx_stop_all();
    for (int i = 0; i < SFX_COUNT; i++) { if (g_smp[i].data) sample_free(&g_smp[i]); g_tried[i] = 0; }
}

void sfx_stop_all(void)
{
    audio_stop_all();
    g_motor = g_toeren = g_slip = g_rattle1 = g_rattle2 = g_turbo = -1;
    for (int i = 0; i < 20; i++) g_slot[i].key = -1;
}

void sfx_pause(int paused) { audio_set_master(paused ? 0.0f : 1.0f, 1.0f); }

int sfx_play_vol(int idx, int vol)
{
    const Sample *s = g_on ? smp(idx) : NULL;
    return s ? audio_play(s, db(vol - g_sfxvol), 0, 1, 0) : 0;
}

int sfx_play(int idx) { return sfx_play_vol(idx, -500); }

int sfx_loop(int idx)
{
    const Sample *s = g_on ? smp(idx) : NULL;
    return s ? audio_play(s, db(-500 - g_sfxvol), 0, 1, 1) : 0;
}

void sfx_stop(int voice) { if (voice > 0) audio_stop(voice); }
int sfx_playing(int voice) { return voice > 0 && audio_playing(voice); }

static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* a loop with a frequency in Hz (the DirectSound buffer's SetFrequency) and a volume in 1/100 dB */
static void loop_set(int *voice, int idx, int on, int freq, int vol)
{
    if (!g_on || !on) { if (*voice > 0) { audio_stop(*voice); *voice = -1; } return; }
    const Sample *s = smp(idx);
    if (!s) return;
    float pitch = (float)freq / (float)s->rate;
    if (*voice <= 0 || !audio_playing(*voice)) *voice = audio_play(s, db(vol), 0, pitch, 1);
    else audio_set(*voice, db(vol), 0, pitch);
}

void sfx_engine(float rpm, int motor_upgrade)
{
    int r = (int)rpm;
    loop_set(&g_motor, SFX_MOTOR, 1, clampi(24000 - r, 11000, 24000), clampi(-(r / 8), -2000, -100) - g_sfxvol - 200);
    int f = 10000 + 3 * r;
    loop_set(&g_toeren, motor_upgrade ? SFX_TOEREN2 : SFX_TOEREN1, 1, f > 40000 ? 40000 : f,
             clampi(r / 2 - 2000, -2000, -100) - g_sfxvol - 200);
}

void sfx_squeal(int n)
{
    loop_set(&g_slip, SFX_SLIPLOOP, n >= 4, clampi(15000 + 700 * n, 15000, 28000),
             clampi((n - 11) * 100, -1100, -100) - g_sfxvol - 100);
}

void sfx_rattle(int damage)
{
    const Sample *a = smp(SFX_RAMMEL), *b = smp(SFX_RAMMEL2);
    loop_set(&g_rattle1, SFX_RAMMEL, damage >= 65, a ? a->rate : 22050, -500 - g_sfxvol);
    loop_set(&g_rattle2, SFX_RAMMEL2, damage >= 75, b ? b->rate : 22050, -500 - g_sfxvol);
}

void sfx_turbo(int on)
{
    const Sample *s = smp(SFX_TURBO);
    loop_set(&g_turbo, SFX_TURBO, on, s ? s->rate : 22050, -500 - g_sfxvol);
}

void sfx_listener(Vec3 pos, Vec3 right) { g_lpos = pos; g_lright = right; }

int sfx_object(int key, int idx, Vec3 src, int loop)
{
    const Sample *s = g_on ? smp(idx) : NULL;
    int dx = (int)(src.x - g_lpos.x), dz = (int)(src.z - g_lpos.z);
    int dist = abs(dx) + abs(dz);                              /* Manhattan distance, height ignored */
    Slot *sl = NULL;
    for (int i = 0; i < 20; i++) if (g_slot[i].key == key) { sl = &g_slot[i]; break; }
    if (!s || dist >= 2500) { if (sl) { audio_stop(sl->voice); sl->key = -1; } return 0; }
    float pan = 0;
    if (g_3d) {
        float len = sqrtf((float)dx * dx + (float)dz * dz);
        if (len > 0) {
            int p = (int)((dx / len * g_lright.x + dz / len * g_lright.z) * 90);
            pan = clampi(p, -90, 90) / 90.0f;
        }
    }
    int v = -(int)(100 * powf(30.0f, dist / 2500.0f));
    v = clampi(v, -3000, -100) - g_sfxvol;
    if (sl && sl->idx == idx && audio_playing(sl->voice)) {
        audio_set(sl->voice, db(v), pan, (float)(s->rate + sl->freq_detune) / s->rate);
        return 1;
    }
    if (sl && sl->idx == idx && !loop) { sl->key = -1; return 0; }   /* a one-shot that has ended */
    if (sl) audio_stop(sl->voice);
    if (!sl) for (int i = 0; i < 20; i++) if (g_slot[i].key < 0) { sl = &g_slot[i]; break; }
    if (!sl) return 0;                                             /* none free: dropped */
    sl->key = key;
    sl->idx = idx;
    int f = s->rate + rand() % 500;                                /* random detune */
    f = clampi(f, 8000, 40000);
    sl->freq_detune = f - s->rate;
    sl->voice = audio_play(s, db(v - 100), pan, (float)f / s->rate, loop);
    return 1;
}

void sfx_object_stop(int key)
{
    for (int i = 0; i < 20; i++)
        if (g_slot[i].key == key) { audio_stop(g_slot[i].voice); g_slot[i].key = -1; }
}
