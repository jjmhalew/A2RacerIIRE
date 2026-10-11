/* fog.c - the fog (gfx_bkg.cpp, ev_fog; docs/RENDER.md 8).
 *
 * init_bkg 0x418da0 (from init_game, accelerated path only): if Game.ini [OPTIONS] fog (g_opt_fog 0x4d9abc) is not 0
 * and rand() & 1, then unless fog == 2 TEXTUREMAPBLEND = 7 (COPY) and the FILTER setting is applied again, fog_enable
 * 0x429e30 (FOGENABLE 1, the light-state fog LINEAR, colour 0xb4b4c8, start 3.5, end 1000), and the fog's base colour
 * (0x48815c) becomes (220, 125, 100). Every frame draw_bkg 0x4190d0 -> bkg_update_colors 0x418f00 ->
 * bkg_set_fog_params 0x418830 recomputes colour, start and end (only applied while FOGENABLE is on). The backdrop's
 * vertices carry their own fog factor (specular alpha, set by bkg_update_colors whether fog is on or not).
 *
 * The device states map to gfx_set_fog_on / gfx_set_decal (the D3D5 docs: COPY "is equivalent to DECAL" on a HAL
 * device: the texture's colour and alpha replace the vertex colour, so every vertex-alpha effect turns opaque or, for
 * the skid marks' inverted blend, invisible - the "conflicts with alpha blending" of Game.ini). Fog is computed on the
 * eye depth (D3D's vertex fog of the lighting module) (?).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "fog.h"
#include "ini.h"
#include "res.h"
#include "rules.h"
#include "screen.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int g_enabled;                   /* g_fog_enable 0x4ea81c (RS 28 FOGENABLE) */
static int g_decal;                     /* g_texturemapblend 0x4ea844 == 7 (COPY) */
static uint32_t g_color = 0xffffff;     /* g_fog_color 0x4ea820 (RS 34), 0xRRGGBB */
static float g_start = 3.25f, g_end = 50.0f;   /* g_fog_start / g_fog_end 0x4ea82c / 0x4ea830 (LS 5 / 6) */
static int g_base[3];                   /* 0x48815c: the fog colour at the start of the track, (220, 125, 55) */
/* the fog fade of trigger 16 (0x4dc9a8..0x4dc9c0); its update ev_fog_fade_update 0x4157e0 (game action 5, registered
 * by 0x429ab0) only runs while 0x4dc9a0 is set, and nothing sets it: the fade never happens */
static struct { int period, count, r, g, b; float start, end; } g_fade;

static int ftol(float f) { return (int)f; }   /* MSVC _ftol: truncation toward zero */

static void apply(void);
void fog_states(void) { apply(); }

static void apply(void)
{
    gfx_set_fog_on(g_enabled);
    gfx_set_decal(g_decal);
    if (g_enabled)
        gfx_set_fog((g_color >> 16 & 0xff) / 255.0f, (g_color >> 8 & 0xff) / 255.0f, (g_color & 0xff) / 255.0f, g_start, g_end);
}

static void fog_enable(void)                                        /* 0x429e30 */
{
    g_enabled = 1;
    g_color = 0xb4b4c8;
    g_start = 3.5f;
    g_end = 1000.0f;
}

void fog_init(void)                                                 /* init_bkg 0x418da0 */
{
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    int fog = ini_int(g, "OPTIONS", "fog", 0);
    ini_free(g);
    g_enabled = g_decal = 0;
    g_color = 0xffffff;
    g_start = 3.25f;
    g_end = 50.0f;
    g_base[0] = 220; g_base[1] = 125; g_base[2] = 55;               /* 0x48815c .. 0x488164 */
    memset(&g_fade, 0, sizeof g_fade);
    /* the software modes (screen.h): init_bkg returns at once, no fog, no coin; set_game_render_states 0x423e94 put
     * the device in TEXTUREMAPBLEND COPY for the whole race */
    if (!scr_hw3d()) { g_decal = 1; return; }
    /* A2_TEST_FOG (tests): the coin always says yes */
    if (fog && ((rand() & 1) || getenv("A2_TEST_FOG"))) {
        if (fog != 2) g_decal = 1;                                  /* TEXTUREMAPBLEND COPY */
        fog_enable();
        g_base[0] = 0xdc; g_base[1] = 0x7d; g_base[2] = 0x64;
    }
}

/* bkg_update_colors 0x418f00 (the fog part) and bkg_set_fog_params 0x418830 */
void fog_frame(float a)
{
    int s = rules_lap_ms() / 1000;                                  /* timer_get(0) / 1000 */
    float q = fabsf(sinf((float)s * 0.00416667f * 0.785399f));     /* |sin(s pi / 960)| */
    float pf = 0.08f - a * -0.8f;                                   /* [0x488184] - a * [0x47f644] */
    /* colour = X0 + (255 - X0) a per channel, X0 = base + q * (0, -50, 0) ([0x4a7d6c], [0x488168], [0x4a7d70]) */
    float x0[3] = { (float)g_base[0], g_base[1] + q * -50.0f, (float)g_base[2] };
    int c[3];
    for (int i = 0; i < 3; i++) c[i] = ftol((255 - x0[i]) * a) + ftol(x0[i]);
    g_color = (uint32_t)(c[0] << 16 | c[1] << 8 | c[2]);
    g_start = 435.0f - pf * -750.0f;
    g_end = (0.3f - -1.0f) * 750.0f - pf * -7500.0f;               /* ([0x488180] + 1) * 750 + 7500 p */
    apply();
}

void fog_sky(const GfxVertex *v, int n, float a)
{
    if (!g_enabled || n <= 0 || n > 12) return;
    /* the fog factors of the backdrop vertices: top edge 0x80 - ftol(-127 a), horizon and bottom ftol(255 a); D3D
     * fogs a TL vertex to f/255 of its colour plus (1 - f/255) of the fog colour: here the fog colour blended over
     * the sky with alpha 255 - f */
    int top = 0x80 - ftol(a * -127.0f), rest = ftol(a * 255.0f);
    GfxVertex t[12];
    for (int i = 0; i < n; i++) {
        t[i] = v[i];
        int f = v[i].v == 0 ? top : rest;
        f = f < 0 ? 0 : f > 255 ? 255 : f;
        t[i].r = (uint8_t)(g_color >> 16);
        t[i].g = (uint8_t)(g_color >> 8);
        t[i].b = (uint8_t)g_color;
        t[i].a = (uint8_t)(255 - f);
    }
    GfxState st = { BLEND_ALPHA, 0, 0, 0, 0, 0 };
    gfx_set_decal(0);                                               /* the overlay is not a texture */
    gfx_draw_tris(t, n, 0, &st);
    gfx_set_decal(g_decal);
}

void fog_trigger_fade_start(void)                                   /* 0x429ad0 (player only) */
{
    g_fade.r = 0; g_fade.g = 0; g_fade.b = 0xff;
    g_fade.start = 3.5f;
    g_fade.end = 2000.0f;
    g_fade.period = 1;
    g_fade.count = 0;
}

void fog_trigger_fade_end(void)                                     /* 0x429b20 (player only): fog off for the race */
{
    g_enabled = 0;
    g_color = 0xc8c8ff;
    g_start = 3.5f;
    g_end = 1000.0f;
    apply();
}
