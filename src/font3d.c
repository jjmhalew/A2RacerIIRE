/* font3d.c - see font3d.h; docs/RENDER.md "3D font (gfx_3dft.cpp)" has the original's code.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "font3d.h"
#include "exetab.h"
#include "gfx.h"
#include "model.h"
#include "track.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAXLET 30               /* t_3d_effect_object[30] at 0x4a7c00 */
#define NSERIES 3
#define NFX 5                   /* the effects (init, update) at 0x486ab8 */
#define AMASK (ANGLE_UNITS - 1)
#define TWO_PI_F 6.28319f       /* the original's 2 pi (0x47fc70) */

/* t_3d_effect_object (0x44 bytes) and the fields of its 3D instance (stride 0x99) that the effects use */
typedef struct {
    int used;                   /* 0x4a7c10[i] */
    Vec3 center;                /* +0x00: the word's centre (a); serie 2 moves it to the player */
    Vec3 pos;                   /* +0x0c: the letter's start position */
    float angle;                /* +0x1c: fx 4, angle around the centre (ANGLE_UNITS) */
    int amp;                    /* +0x24: fx 0 height, fx 4 distance from the centre */
    int phase;                  /* +0x28: fx 0 */
    int index;                  /* +0x2c: letter number in the word (spaces not counted) */
    int frames;                 /* +0x34: frames of the running effect (left, for fx 1-4) */
    int serie, step;            /* +0x38 / +0x3c: effect serie and the running effect in it */
    int count;                  /* +0x40: fx 0, frames done */
    int fx;                     /* the running effect (its update is instance +0x91) */
    int frame;                  /* instance +0x08: the character = FONT3D_ANIM frame */
    Vec3 ipos, vel;             /* instance +0x50 position, +0x5c velocity per frame */
    Vec3 rot, rotvel;           /* instance +0x74 pitch, yaw, roll and +0x68 their change per frame (ANGLE_UNITS) */
    Vec3 ipos0, rot0;           /* the state before the last tick, for drawing */
} Letter;

static struct {
    int ok;
    int nfx[NSERIES], fx[NSERIES][10];      /* 0x486ae0: {int count; int fx[10];} x 3 */
    float spacing;                          /* 0x486b64: 12.5 */
    char charset[48];                       /* 0x486b68, 48 characters, not 0-terminated */
    int anim;                               /* FONT3D_ANIM (0x4a7c48) */
    float dt;                               /* seconds per tick: the original's timer_frame_dt */
    Letter l[MAXLET];
    int last;                               /* T_ARRAY's last new index (array +8) */
    Vec3 player;
    int tables;
    float sin[ANGLE_UNITS], cos[ANGLE_UNITS];
} F;

static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static void tables(void)                            /* the SIN / COS tables of math3d_init_tables 0x450280 */
{
    if (F.tables) return;
    for (int i = 0; i < ANGLE_UNITS; i++) {
        F.sin[i] = sinf(i * (2 * PI_F / ANGLE_UNITS));
        F.cos[i] = cosf(i * (2 * PI_F / ANGLE_UNITS));
    }
    F.tables = 1;
}

int font3d_init(float tick_dt)
{
    font3d_free();
    F.dt = tick_dt > 0 ? tick_dt : 1.0f / 30;
    tables();
    PeFile pe;
    if (!pe_open(&pe, "spel.dat")) return 0;
    const uint8_t *s = pe_ptr(&pe, 0x486ae0, NSERIES * 44);
    const uint8_t *sp = pe_ptr(&pe, 0x486b64, 4);
    const uint8_t *cs = pe_ptr(&pe, 0x486b68, 48);
    if (s && sp && cs) {
        for (int i = 0; i < NSERIES; i++) {
            F.nfx[i] = (int)pe_u32(&pe, 0x486ae0 + i * 44);
            if (F.nfx[i] < 1 || F.nfx[i] > 9) F.nfx[i] = 0;          /* 'Illegal value for nr of fx' */
            for (int k = 0; k < 10; k++) F.fx[i][k] = (int)pe_u32(&pe, 0x486ae4 + i * 44 + k * 4);
        }
        F.spacing = rdf(sp);
        memcpy(F.charset, cs, 48);
        F.ok = 1;
    }
    pe_close(&pe);
    F.anim = anim_find("FONT3D_ANIM");
    return F.ok;
}

void font3d_free(void) { font3d_clear(); F.ok = 0; }

void font3d_clear(void)
{
    memset(F.l, 0, sizeof F.l);
    F.last = 0;
}

int font3d_count(void)
{
    int n = 0;
    for (int i = 0; i < MAXLET; i++) n += F.l[i].used;
    return n;
}

/* seconds -> frames: ftol(sec / timer_frame_dt()); PORT: + a little, so that 0.8 s at 30 Hz is 24 and not 23 */
static int frames_of(float sec)
{
    int n = (int)(sec / F.dt + 0.001f);
    return n > 0 ? n : 1;
}

/* ---- the effects (0x486ab8: {init, update} x 5) ------------------------------------------------------------------- */
static void fx_next(Letter *l);

static void inst_move(Letter *l)                    /* the instance's default move 0x458fd0 */
{
    l->ipos = v3_add(l->ipos, l->vel);
    l->rot = v3_add(l->rot, l->rotvel);
}

static void fx0_init(Letter *l)                     /* 0x417a90: hop */
{
    int ph = l->index * 75;
    if (ph > 0x7ff) ph -= 0x7ff;
    l->amp = 80;
    l->phase = ph;
    l->frames = frames_of(3.0f);
    l->count = 0;
}

static void fx0_update(Letter *l)                   /* 0x417b20 */
{
    l->phase += ANGLE_UNITS / l->frames;
    if (l->phase >= 0x7ff) l->phase = 0;            /* half a sine: the letters hop */
    if (l->amp > 25) {
        int d = 60 / l->frames;
        l->amp -= d > 0 ? d : 1;
    }
    l->ipos.y = F.sin[l->phase & AMASK] * l->amp + l->pos.y;
    if (++l->count >= l->frames) fx_next(l);
}

static void fx1_init(Letter *l)                     /* 0x417c30: burst apart */
{
    l->vel = v3((l->ipos.x - l->center.x) * 0.2f, 15.0f, (l->ipos.z - l->center.z) * 0.2f);
    l->rotvel.x = (float)(rand() % 200);
    l->rotvel.y = (float)(rand() % 200);
    l->rotvel.z = (float)(rand() % 200);
    l->frames = 25;
}

static void fx_move_count(Letter *l)                /* 0x417d50 (fx 1 and 2) */
{
    inst_move(l);
    if (--l->frames == 0) fx_next(l);
}

static void fx2_init(Letter *l)                     /* 0x417da0: fly to the player, one flip over */
{
    l->frames = frames_of(0.8f);
    l->vel = v3((l->ipos.x - F.player.x) * -0.9f / l->frames, 0, (l->ipos.z - F.player.z) * -0.9f / l->frames);
    l->rotvel.x = (float)(ANGLE_UNITS / l->frames);
}

static void fx3_init(Letter *l) { l->frames = frames_of(0.2f); }   /* 0x417ec0: stand still */

static void fx3_update(Letter *l)                   /* 0x417f10 */
{
    if (--l->frames == 0) fx_next(l);
}

static void fx4_init(Letter *l)                     /* 0x417f50: the word turns once around its centre */
{
    l->frames = frames_of(1.0f);
    l->rotvel = v3(0, (float)(-ANGLE_UNITS / l->frames), 0);
    float dx = l->pos.x - l->center.x, dz = l->pos.z - l->center.z;
    l->amp = (int)sqrtf(dx * dx + dz * dz);
    if (dx != 0 || dz != 0) l->angle = (atan2f(dz, dx) - PI_F * 0.5f) * ANGLE_UNITS / TWO_PI_F;
}

static void fx4_update(Letter *l)                   /* 0x418050 */
{
    l->angle += (float)(int)l->rotvel.y;
    int a = (int)l->angle + ANGLE_UNITS / 4;
    l->ipos.x = F.cos[a & AMASK] * l->amp + l->center.x;
    l->ipos.z = F.sin[a & AMASK] * l->amp + l->center.z;
    l->center.x += (F.player.x - l->center.x) / l->frames;      /* the centre reaches the player at the end */
    l->center.z += (F.player.z - l->center.z) / l->frames;
    inst_move(l);
    if (--l->frames == 0) fx_next(l);
}

static void (*const FX_INIT[NFX])(Letter *) = { fx0_init, fx1_init, fx2_init, fx3_init, fx4_init };
static void (*const FX_UPDATE[NFX])(Letter *) = { fx0_update, fx_move_count, fx_move_count, fx3_update, fx4_update };

static void fx_start(Letter *l, int fx)
{
    l->fx = fx >= 0 && fx < NFX ? fx : -1;
    if (l->fx >= 0) FX_INIT[l->fx](l);
}

static void fx_next(Letter *l)                      /* 0x4179e0 */
{
    if (++l->step >= F.nfx[l->serie]) {
        l->used = 0;                                /* the record and its instance are freed */
        return;
    }
    fx_start(l, F.fx[l->serie][l->step]);
}

/* ---- words --------------------------------------------------------------------------------------------------------- */
static int letter_new(void)                         /* T_ARRAY::get_new 0x418200: from the last new one on, wrapping */
{
    for (int i = F.last; i < MAXLET; i++) if (!F.l[i].used) return F.last = i;
    for (int i = 0; i < F.last; i++) if (!F.l[i].used) return F.last = i;
    return -1;
}

void font3d_text(const char *text, Vec3 a, Vec3 b, int serie)
{
    if (!F.ok || !text || serie < 0 || serie >= NSERIES || F.nfx[serie] == 0) return;
    Vec3 d = v3(b.z, 0, -b.x);                      /* the right of b */
    float len = v3_len(d);
    if (len <= 0) return;
    d = v3_scale(d, 1.0f / len);
    int n = (int)strlen(text);
    Vec3 p = v3_sub(a, v3_scale(d, F.spacing * (float)(n / 2)));
    if (!(n & 1)) p = v3_add(p, v3_scale(d, F.spacing * 0.5f));
    int top = -1;                                   /* the highest used record + the length must fit in 30 */
    for (int i = 0; i < MAXLET; i++) if (F.l[i].used) top = i;
    if (top + n > MAXLET) return;
    float yaw = (atan2f(b.z, b.x) - PI_F * 0.5f) * ANGLE_UNITS / TWO_PI_F;    /* the letters turned to b */
    int index = 0;
    for (int i = 0; i < n; i++, p = v3_add(p, v3_scale(d, F.spacing))) {
        const char *at = memchr(F.charset, text[i], sizeof F.charset);
        if (!at) continue;                          /* a space (or unknown): only the advance */
        int k = letter_new();
        if (k < 0) return;                          /* 'not enough objects' */
        Letter *l = &F.l[k];
        memset(l, 0, sizeof *l);
        l->used = 1;
        l->index = index++;
        l->pos = p;
        l->center = a;
        l->serie = serie;
        l->step = 0;
        l->frame = (int)(at - F.charset);
        l->ipos = p;
        l->rot = v3(0, yaw, 0);
        l->ipos0 = l->ipos;
        l->rot0 = l->rot;
        fx_start(l, F.fx[serie][0]);
    }
}

void font3d_tick(Vec3 player)
{
    F.player = player;
    for (int i = 0; i < MAXLET; i++) {
        Letter *l = &F.l[i];
        if (!l->used) continue;
        l->ipos0 = l->ipos;
        l->rot0 = l->rot;
        if (l->fx >= 0) FX_UPDATE[l->fx](l);        /* none: the default update 0x458fc0 does nothing */
    }
}

/* ---- drawing --------------------------------------------------------------------------------------------------------- */
Mat4 font3d_inst_matrix(Vec3 pos, float pitch, float yaw, float roll)
{
    /* the original's row-major D3D matrices with row vectors, the same memory layout as Mat4 here:
     * M = Rz(roll) * Rx(pitch) * Ry(yaw), rows 0-2 the model's x, y, z axes in the world, row 3 the position */
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    int r = (int)roll & AMASK, p = (int)pitch & AMASK, y = (int)yaw & AMASK;
    tables();
    float cr = F.cos[r], sr = F.sin[r], cp = F.cos[p], sp = F.sin[p], cy = F.cos[y], sy = F.sin[y];
    /* Rz: [0]=c [1]=-s [4]=s [5]=c; Rx: [5]=c [6]=-s [9]=s [10]=c; Ry: [0]=c [2]=s [8]=-s [10]=c */
    float a[3][3] = { { cr, -sr, 0 }, { sr, cr, 0 }, { 0, 0, 1 } };
    float b[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
    float c[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
    float ab[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) ab[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i * 4 + j] = ab[i][0] * c[0][j] + ab[i][1] * c[1][j] + ab[i][2] * c[2][j];
    m[12] = pos.x; m[13] = pos.y; m[14] = pos.z;
    Mat4 out;
    memcpy(out.m, m, sizeof m);
    return out;
}

void font3d_draw(float alpha)                       /* 0x417870 */
{
    if (!F.ok) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    gfx_set_depth_off(1);                           /* ZENABLE 0 */
    for (int i = 0; i < MAXLET; i++) {
        const Letter *l = &F.l[i];
        if (!l->used) continue;
        Vec3 p = v3_lerp(l->ipos0, l->ipos, alpha), r = v3_lerp(l->rot0, l->rot, alpha);
        Mat4 m = font3d_inst_matrix(p, r.x, r.y, r.z);
        model_draw_matrix(anim_model(F.anim, l->frame), &m);
    }
    gfx_set_depth_off(0);                           /* ZENABLE 1 */
    gfx_set_model(NULL);
}
