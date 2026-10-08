/* screen.c - see screen.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "screen.h"
#include "gfx.h"
#include "hud.h"
#include "image.h"
#include "ini.h"
#include "plat.h"
#include "res.h"
#include "texture.h"
#include <stdio.h>
#include <string.h>

static int g_res = 3, g_res_index = 1, g_8bit, g_hw3d = 1;
static uint8_t g_pal[768];
static int g_target;                 /* this frame is drawn into the offscreen target */
static int g_no_target;              /* there is none (no framebuffer objects): the window instead */
static int g_fw = 640, g_fh = 480;   /* the size of this frame's drawing */

/* load_game_palette 0x420900: Raser.ini [PALET] ETAPPEn = <8-bit>,<16-bit> (skelet2d_read_ini 0x4209b0 copies the
 * two names into g_pal_low_name 0x489601 / g_pal_high_name 0x489611); the 8-bit one for RESOLUTION 0 and 1 */
static void load_palette(const Ini *r, int etappe)
{
    char k[16], v[64];
    snprintf(k, sizeof k, "ETAPPE%d", etappe);
    ini_str(r, "PALET", k, "e00_pal.pcx", v, sizeof v);
    char *c = strchr(v, ',');
    if (c) *c = 0;
    Image img;
    if (image_load(v, &img) || image_load("e00_pal.pcx", &img)) {
        memcpy(g_pal, img.pal, sizeof g_pal);
        image_free(&img);
    } else fprintf(stderr, "palette %s missing\n", v);
    g_pal[254 * 3] = 255;            /* rcpcx_read_palette_keyed 0x46af40: index 254 is the colour key */
    g_pal[254 * 3 + 1] = 0;
    g_pal[254 * 3 + 2] = 127;
}

void scr_init(int etappe)
{
    char p[1024];
    res_user_path(p, sizeof p, "Raser.ini");
    Ini *r = ini_load(p);
    /* skelet2d_read_ini 0x4209b0: [PROGRAM] RESOLUTION (a missing key reads 0 there; PORT: 3, the CD's value) */
    g_res = ini_int(r, "PROGRAM", "RESOLUTION", 3);
    /* set_game_display, jump table 0x4243d4: 0 -> 0x423d54, 1 -> 0x423d6c, 2 and 3 -> 0x423d83, 5 -> 0x423d9a (16-bit
     * software emulation at g_res_index 0), 4 and others "Invalid resolution" (fatal). PORT: all but 0 and 1 are the
     * 640x480x16 mode */
    if (g_res == 0) { g_res_index = 0; g_8bit = 1; g_hw3d = 0; }
    else if (g_res == 1) { g_res_index = 1; g_8bit = 1; g_hw3d = 0; }
    else { g_res_index = 1; g_8bit = 0; g_hw3d = 1; }
    if (g_8bit) load_palette(r, etappe);
    ini_free(r);
    tex_set_mode(g_8bit, g_pal);     /* texture_set_source_type(1) (.tga) only with g_hw_accel (init_3d_library 0x42359f) */
    hud_set_mode(g_8bit, g_res_index, g_pal);
}

int scr_resolution(void) { return g_res; }
int scr_res_index(void) { return g_res_index; }
int scr_8bit(void) { return g_8bit; }
int scr_hw3d(void) { return g_hw3d; }
int scr_w(void) { return g_res_index ? 640 : 320; }
int scr_h(void) { return g_res_index ? 480 : 200; }
const uint8_t *scr_palette(void) { return g_pal; }

void scr_frame_begin(float r, float g, float b, int *w, int *h)
{
    g_target = g_8bit && !g_no_target && gfx_target_begin(scr_w(), scr_h(), r, g, b);
    if (g_8bit && !g_target) g_no_target = 1;
    if (g_target) { g_fw = scr_w(); g_fh = scr_h(); }
    else {
        plat_size(&g_fw, &g_fh);
        gfx_begin(g_fw, g_fh, r, g, b);
    }
    if (w) *w = g_fw;
    if (h) *h = g_fh;
}

void scr_frame_size(int *w, int *h)
{
    *w = g_fw;
    *h = g_fh;
}

/* the largest 4:3 rectangle in the window, centred */
static void box(int *x, int *y, int *w, int *h)
{
    int W, H;
    plat_size(&W, &H);
    *w = W;
    *h = W * 3 / 4;
    if (*h > H) { *h = H; *w = H * 4 / 3; }
    *x = (W - *w) / 2;
    *y = (H - *h) / 2;
}

void scr_frame_end(void)
{
    if (!g_target) return;
    int x, y, w, h, W, H;
    box(&x, &y, &w, &h);
    plat_size(&W, &H);
    gfx_target_present(W, H, x, y, w, h);
    g_target = 0;
    g_fw = W;
    g_fh = H;
}

void scr_2d_begin(void) { gfx_2d_begin(scr_w(), scr_h()); }

void scr_to_window(float x, float y, float *sx, float *sy)
{
    if (g_8bit && !g_no_target) {    /* the target stretched into the 4:3 rectangle */
        int bx, by, bw, bh;
        box(&bx, &by, &bw, &bh);
        *sx = bx + x * bw / scr_w();
        *sy = by + y * bh / scr_h();
        return;
    }
    int W, H;                        /* gfx_2d_begin(scr_w, scr_h) in the window: letterboxed, one scale */
    plat_size(&W, &H);
    float s = (float)W / scr_w() < (float)H / scr_h() ? (float)W / scr_w() : (float)H / scr_h();
    *sx = (W - scr_w() * s) * 0.5f + x * s;
    *sy = (H - scr_h() * s) * 0.5f + y * s;
}
