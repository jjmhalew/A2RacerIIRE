/* hud.c - see hud.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "hud.h"
#include "exetab.h"
#include "gfx.h"
#include "image.h"
#include "ini.h"
#include "plat.h"
#include "res.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palette and sprites (RC.IMG / RC.IND: {u16 w, h; s16 hotx, hoty; w*h indices}, 255 transparent) ---------- */
static uint8_t g_pal[768];                 /* e02_pl16.pcx: the palette of the 16-bit HUD sprites and fonts */
static ResArchive *g_rc;
typedef struct { unsigned tex; int w, h, hx, hy, tried; } Sprite;
static Sprite g_spr[460];
typedef struct { int n; int id[16]; } HudAnim;
static HudAnim g_anim[45];                 /* g_anim_table 0x4961e8: {count, 1, frames {delay, sprite id}} */

static void rgba_of(uint8_t c, uint8_t *q)
{
    q[0] = g_pal[c * 3]; q[1] = g_pal[c * 3 + 1]; q[2] = g_pal[c * 3 + 2];
    q[3] = c == 255 ? 0 : 255;
    tex_apply_gamma(q, 1);
}

static Sprite *sprite(int id)                /* sprite id N = bank entry N-1 (sprite_load_bank 0x46c1c0) */
{
    if (id < 1 || id > (int)(sizeof g_spr / sizeof g_spr[0])) return NULL;
    Sprite *s = &g_spr[id - 1];
    if (!s->tried) {
        s->tried = 1;
        size_t n = 0;
        uint8_t *d = res_read(g_rc, id - 1, &n);
        if (d && n >= 8) {
            int w = d[0] | d[1] << 8, h = d[2] | d[3] << 8;
            s->hx = (int16_t)(d[4] | d[5] << 8);
            s->hy = (int16_t)(d[6] | d[7] << 8);
            if (w > 0 && h > 0 && 8 + (size_t)w * h <= n) {
                uint8_t *px = malloc((size_t)w * h * 4);
                for (int i = 0; i < w * h; i++) rgba_of(d[8 + i], px + i * 4);
                s->tex = gfx_texture(px, w, h, TEX_CLAMP | TEX_NEAREST);
                s->w = w;
                s->h = h;
                free(px);
            }
        }
        free(d);
    }
    return s->tex ? s : NULL;
}

/* ---- fonts (.fnt: 256 x {s16 w, h; u32 offset}, s16[4], 8-bit glyph pixels, 255 transparent) ------------------- */
typedef struct { unsigned tex; int tw, th; int w[256], h[256], x[256], y[256]; int xspace, yspace, loaded; } Font;
static Font g_font[17];
/* the 16-bit set of 0x486d08 (records of 0x94 bytes, set 1) and the spacing gfx_asci_init_fonts 0x418330 gives them */
static const int FONT_XSPACE[17] = { -8, 0, -4, -4, 0, -4, -2, 0, 0, 0, -2, -4, -3, -2, -14, -2, 0 };

static void font_load(Font *f, const char *name)
{
    size_t n = 0;
    uint8_t *d = res_load(name, &n);
    if (!d || n < 256 * 8 + 8) { free(d); fprintf(stderr, "font %s missing\n", name); return; }
    /* pack the glyphs in rows of a 512-wide atlas */
    int x = 0, y = 0, rowh = 0, W = 512, H = 0;
    for (int c = 0; c < 256; c++) {
        int w = (int16_t)(d[c * 8] | d[c * 8 + 1] << 8), h = (int16_t)(d[c * 8 + 2] | d[c * 8 + 3] << 8);
        uint32_t off = d[c * 8 + 4] | d[c * 8 + 5] << 8 | d[c * 8 + 6] << 16 | (uint32_t)d[c * 8 + 7] << 24;
        if (w <= 0 || h <= 0 || w > W || off + (size_t)w * h > n) { f->w[c] = f->h[c] = 0; continue; }
        if (x + w + 1 > W) { x = 0; y += rowh + 1; rowh = 0; }
        f->w[c] = w; f->h[c] = h; f->x[c] = x; f->y[c] = y;
        x += w + 1;
        if (h > rowh) rowh = h;
    }
    H = y + rowh + 1;
    int th = 1;
    while (th < H) th *= 2;
    uint8_t *px = calloc((size_t)W * th, 4);
    for (int c = 0; c < 256; c++) {
        if (!f->w[c]) continue;
        uint32_t off = d[c * 8 + 4] | d[c * 8 + 5] << 8 | d[c * 8 + 6] << 16 | (uint32_t)d[c * 8 + 7] << 24;
        for (int j = 0; j < f->h[c]; j++)
            for (int i = 0; i < f->w[c]; i++)
                rgba_of(d[off + (size_t)j * f->w[c] + i], px + ((size_t)(f->y[c] + j) * W + f->x[c] + i) * 4);
    }
    f->tex = gfx_texture(px, W, th, TEX_CLAMP | TEX_NEAREST);
    f->tw = W;
    f->th = th;
    f->loaded = 1;
    free(px);
    free(d);
}

/* ---- texts (language.ini) ---------------------------------------------------------------------------------------- */
static char g_text[56][96];

const char *hud_text(int n) { return n >= 0 && n < 56 ? g_text[n] : ""; }

static void load_texts(void)
{
    char p[1024];
    res_path(p, sizeof p, "language.ini");
    Ini *ini = ini_load(p);
    int lang = ini_int(ini, "SETTINGS", "LANGUAGE", 0);
    static const char *sec[3] = { "NEDERLANDS", "ENGELS", "DUITS" };
    for (int i = 0; i < 56; i++) {
        char k[8];
        snprintf(k, sizeof k, "%d", i);
        ini_str(ini, sec[lang >= 0 && lang < 3 ? lang : 0], k, "", g_text[i], sizeof g_text[i]);
    }
    ini_free(ini);
}

/* ---- layout: g_hud_layout row 1 (640x480; entries 34..83 are filled by the static initializer 0x41b860) -------- */
typedef struct { int anim, x, y; } Item;
static Item g_item[84];
static const int HI[][4] = {        /* {item, 16-bit anim (-1 = text), x, y}, docs/HUD.md 4 */
    { 3, 23, 541, 349 }, { 7, -1, 571, 329 }, { 14, 19, 489, 425 }, { 15, 44, 0, 0 }, { 29, 17, 320, 120 },
    { 34, 14, 320, 240 }, { 35, -1, 320, 170 }, { 36, -1, 320, 220 }, { 37, -1, 320, 246 },
    { 38, -1, 546, 4 }, { 39, -1, 502, 24 }, { 40, -1, 532, 54 }, { 41, -1, 484, 75 }, { 42, -1, 553, 75 },
    { 43, -1, 484, 92 }, { 44, -1, 553, 92 }, { 45, -1, 484, 109 }, { 46, -1, 553, 109 }, { 47, -1, 567, 139 },
    { 48, -1, 571, 159 }, { 49, -1, 580, 189 }, { 50, -1, 571, 209 }, { 51, 9, 474, 15 }, { 52, 11, 484, 340 },
    { 53, -1, 582, 368 }, { 54, 21, 540, 388 }, { 55, -1, 559, 409 }, { 56, -1, 505, 428 }, { 57, 12, 485, 15 },
    { 58, -1, 522, 56 }, { 59, -1, 514, 76 }, { 60, -1, 564, 107 }, { 61, -1, 567, 127 }, { 62, 6, 49, 46 },
    { 63, -1, 68, 78 }, { 64, 3, 494, 83 }, { 65, -1, 256, 159 }, { 66, -1, 256, 186 }, { 67, -1, 256, 237 },
    { 68, -1, 388, 281 }, { 69, -1, 388, 309 }, { 70, -1, 388, 337 }, { 71, -1, 388, 390 }, { 72, 4, 49, 46 },
    { 73, 0, 235, 15 }, { 74, -1, 320, 12 }, { 75, -1, 320, 24 }, { 76, -1, 320, 80 }, { 77, -1, 320, 20 },
    { 78, -1, 388, 207 }, { 79, -1, 388, 235 }, { 80, -1, 388, 263 }, { 81, -1, 388, 316 }, { 82, -1, 294, 157 },
    { 83, -1, 580, 349 } };

int hud_init(void)
{
    Image pal;
    if (!image_load("e02_pl16.pcx", &pal)) return 0;
    memcpy(g_pal, pal.pal, 768);
    image_free(&pal);
    g_rc = res_open("RC");
    if (!g_rc) return 0;
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {
        for (int i = 0; i < 45; i++) {
            uint32_t a = 0x4961e8 + (uint32_t)i * 12;
            int n = (int)pe_u32(&pe, a);
            uint32_t fr = pe_u32(&pe, a + 8);
            g_anim[i].n = n > 16 ? 16 : n < 0 ? 0 : n;
            for (int k = 0; k < g_anim[i].n; k++) g_anim[i].id[k] = (int)pe_u32(&pe, fr + (uint32_t)k * 8 + 4);
        }
        for (int s = 0; s < 17; s++) {
            const char *nm = pe_str(&pe, 0x486d08 + (uint32_t)(17 + s) * 0x94);
            if (!nm) continue;
            int dup = -1;
            for (int k = 0; k < s; k++)
                if (g_font[k].loaded && !_stricmp(pe_str(&pe, 0x486d08 + (uint32_t)(17 + k) * 0x94), nm)) dup = k;
            if (dup >= 0) { g_font[s] = g_font[dup]; }
            else font_load(&g_font[s], nm);
            g_font[s].xspace = FONT_XSPACE[s];
        }
        pe_close(&pe);
    }
    for (int i = 0; i < 84; i++) g_item[i].anim = -1;
    for (size_t i = 0; i < sizeof HI / sizeof HI[0]; i++) {
        Item *it = &g_item[HI[i][0]];
        it->anim = HI[i][1]; it->x = HI[i][2]; it->y = HI[i][3];
    }
    load_texts();
    return 1;
}

void hud_free(void)
{
    for (size_t i = 0; i < sizeof g_spr / sizeof g_spr[0]; i++) gfx_texture_free(g_spr[i].tex);
    memset(g_spr, 0, sizeof g_spr);
    unsigned freed[17] = { 0 };
    for (int i = 0; i < 17; i++) {
        int dup = 0;
        for (int k = 0; k < i; k++) if (freed[k] == g_font[i].tex) dup = 1;
        if (!dup) gfx_texture_free(g_font[i].tex);
        freed[i] = g_font[i].tex;
    }
    memset(g_font, 0, sizeof g_font);
    res_close(g_rc);
    g_rc = NULL;
}

void hud_sprite(int anim, int frame, int x, int y, int at_hotspot)
{
    if (anim < 0 || anim >= 45 || g_anim[anim].n == 0) return;
    if (frame < 0) frame = 0;
    if (frame >= g_anim[anim].n) frame = g_anim[anim].n - 1;
    Sprite *s = sprite(g_anim[anim].id[frame]);
    if (!s) return;
    if (at_hotspot) { x -= s->hx; y -= s->hy; }
    gfx_2d_quad(s->tex, (float)x, (float)y, (float)s->w, (float)s->h, 0, 0, 1, 1, 0xffffffffu, BLEND_ALPHA);
}

void hud_item(int item, int frame)
{
    if (item < 0 || item >= 84) return;
    /* the frames, badges and bars are drawn by their top-left corner; the U-turn sign and the dialog box at their
     * hot spot (anchor argument of hud_draw_item) */
    int hs = item == 29 || item == 34 || item == 64;
    hud_sprite(g_item[item].anim, frame, g_item[item].x, g_item[item].y, hs);
}

void hud_item_pos(int item, int *x, int *y)
{
    *x = item >= 0 && item < 84 ? g_item[item].x : 0;
    *y = item >= 0 && item < 84 ? g_item[item].y : 0;
}

int hud_text_width(int font, const char *s)
{
    if (font < 0 || font >= 17 || !g_font[font].loaded) return 0;
    const Font *f = &g_font[font];
    int w = 0, n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) w += f->w[*p];
    return n ? w + (n - 1) * f->xspace : 0;
}

void hud_print(int font, int x, int y, int center, const char *s)
{
    if (font < 0 || font >= 17 || !g_font[font].loaded) return;
    const Font *f = &g_font[font];
    if (center) { x -= hud_text_width(font, s) / 2; y -= f->h['A'] / 2; }
    int x0 = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '\n') { x = x0; y += f->h['A'] + f->yspace; continue; }
        int c = *p;
        if (f->w[c]) {
            float u0 = (float)f->x[c] / f->tw, v0 = (float)f->y[c] / f->th;
            float u1 = (float)(f->x[c] + f->w[c]) / f->tw, v1 = (float)(f->y[c] + f->h[c]) / f->th;
            gfx_2d_quad(f->tex, (float)x, (float)y, (float)f->w[c], (float)f->h[c], u0, v0, u1, v1, 0xffffffffu, BLEND_ALPHA);
        }
        x += f->w[c] + f->xspace;
    }
}

void hud_item_text(int item, int font, int center, const char *s)
{
    int x, y;
    hud_item_pos(item, &x, &y);
    hud_print(font, x, y, center, s);
}
