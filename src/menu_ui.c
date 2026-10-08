/* menu_ui.c - the 2D machinery of menu.dat: winlib (surfaces, clipped blits, .img fonts, forms of owner-drawn
 * controls, mouse, sound handles, Smacker) and the menu's own special_anim.cpp / Menuanim.cpp / Help.cpp
 * (sliding title bar and buttons, rotating and scrolling car, typewriter text, property bars, sliders, the spoken
 * help tour). See menu_int.h for the surface model; docs/MENU.md for what each screen draws.
 *
 * Input. The original's controls are Win32 child windows: the mouse clicks them, Enter activates the focused one,
 * Alt + the hot key letter activates that one, Esc and the cursor keys come back as events 0x2710..0x2714 and the
 * screens move the focus themselves (focus_next/focus_prev over the first n controls). A game controller adds the
 * d-pad (cursor keys), A (Enter) and B (Esc); touch arrives as the mouse. Port extra: the cursor keys walk a "ring"
 * that also holds the screen's clickable areas (slot rows, sliders, check boxes, tabs), so every screen can be used
 * without a mouse; a focused area gets an outline.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "menu_int.h"
#include "audio.h"
#include "exetab.h"
#include "film.h"
#include "gfx.h"
#include "image.h"
#include "plat.h"
#include "res.h"
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_surf[3][SCR_W * SCR_H];
static uint8_t g_pal[768];
static unsigned g_tex, g_cursor_tex;      /* the screen and the hand as textures */
static ResArchive *g_mrcs, *g_menurc;
int g_mui_kbd;
jmp_buf g_menu_quit;                         /* set by menu_run */

double mui_ticks(void) { return plat_time() * 1000.0; }

/* ---- sprites: MENURC entry N-1 = sprite id N, {u16 w, h; s16 hotx, hoty; w*h indices}, 255 transparent ---------- */
typedef struct { int w, h, hx, hy, tried; uint8_t *px; } Spr;
#define NSPR 490
static Spr g_spr[NSPR];

static Spr *spr(int id)
{
    if (id < 1 || id >= NSPR) return NULL;
    Spr *s = &g_spr[id];
    if (!s->tried) {
        s->tried = 1;
        size_t n = 0;
        uint8_t *d = res_read(g_menurc, id - 1, &n);
        if (d && n >= 8) {
            int w = d[0] | d[1] << 8, h = d[2] | d[3] << 8;
            if (w > 0 && h > 0 && 8 + (size_t)w * h <= n) {
                s->w = w;
                s->h = h;
                s->hx = (int16_t)(d[4] | d[5] << 8);
                s->hy = (int16_t)(d[6] | d[7] << 8);
                s->px = malloc((size_t)w * h);
                memcpy(s->px, d + 8, (size_t)w * h);
            }
        }
        free(d);
    }
    return s->px ? s : NULL;
}

int mui_sprite_w(int sprite) { Spr *s = spr(sprite); return s ? s->w : 0; }
int mui_sprite_h(int sprite) { Spr *s = spr(sprite); return s ? s->h : 0; }

/* the menu anim table 0x433e60 of menu.dat: 60 records {u32 count, u32 1, u32 frames}, frames {u32 delay, u32 id} */
#define NANIMS 60
static struct { int n; int id[48]; } g_anim[NANIMS];

int mui_anim_frame(int anim, int frame)
{
    if (anim < 0 || anim >= NANIMS || g_anim[anim].n == 0) return 0;
    if (frame < 0) frame = 0;
    if (frame >= g_anim[anim].n) frame = g_anim[anim].n - 1;
    return g_anim[anim].id[frame];
}

int mui_anim_count(int anim) { return anim >= 0 && anim < NANIMS ? g_anim[anim].n : 0; }

/* ---- surfaces ------------------------------------------------------------------------------------------------------ */
void mui_load_pcx(int surf, const char *name)
{
    size_t n = 0;
    uint8_t *d = res_read(g_mrcs, res_find(g_mrcs, name), &n);
    Image img;
    if (!d || !image_decode_pcx(d, n, &img) || !img.index) { free(d); fprintf(stderr, "menu: %s missing\n", name); return; }
    for (int y = 0; y < SCR_H && y < img.h; y++)
        memcpy(g_surf[surf] + y * SCR_W, img.index + (size_t)y * img.w, (size_t)(img.w < SCR_W ? img.w : SCR_W));
    if (memcmp(g_pal, img.pal, 768)) {    /* rcpcx sets the palette of the screen it draws */
        memcpy(g_pal, img.pal, 768);
        gfx_texture_free(g_cursor_tex);   /* the hand is converted with it */
        g_cursor_tex = 0;
    }
    image_free(&img);
    free(d);
}

void mui_blit(int surf, int sprite, int x, int y, int flags)
{
    Spr *s = spr(sprite);
    if (!s) return;
    if (!(flags & 2)) { x -= s->hx; y -= s->hy; }
    uint8_t *dst = g_surf[surf];
    for (int j = 0; j < s->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SCR_H) continue;
        const uint8_t *row = s->px + (size_t)j * s->w;
        for (int i = 0; i < s->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SCR_W || (row[i] == 255 && !(flags & 4))) continue;
            dst[yy * SCR_W + xx] = row[i];
        }
    }
}

static void clamp_rect(int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > SCR_W) *x1 = SCR_W;
    if (*y1 > SCR_H) *y1 = SCR_H;
}

void mui_copy(int src, int dst, int x0, int y0, int x1, int y1)
{
    if (src == dst) return;
    clamp_rect(&x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++)
        if (x1 > x0) memcpy(g_surf[dst] + y * SCR_W + x0, g_surf[src] + y * SCR_W + x0, (size_t)(x1 - x0));
}

void mui_stretch(int src, int dst, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1, int flags)
{
    clamp_rect(&sx0, &sy0, &sx1, &sy1);
    if (sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return;
    /* through a temporary: source and destination may be the same surface */
    int dw = dx1 - dx0, dh = dy1 - dy0, sw = sx1 - sx0, sh = sy1 - sy0;
    uint8_t *tmp = malloc((size_t)dw * dh);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++)
            tmp[y * dw + x] = g_surf[src][(sy0 + y * sh / dh) * SCR_W + sx0 + x * sw / dw];
    for (int y = 0; y < dh; y++) {
        int yy = dy0 + y;
        if (yy < 0 || yy >= SCR_H) continue;
        for (int x = 0; x < dw; x++) {
            int xx = dx0 + x;
            uint8_t p = tmp[y * dw + x];
            if (xx < 0 || xx >= SCR_W || ((flags & 8) && p == 255)) continue;
            g_surf[dst][yy * SCR_W + xx] = p;
        }
    }
    free(tmp);
}

/* a w x h block between two surfaces, each side clipped to the screen */
static void copy_block(int src, int sx, int sy, int dst, int dx, int dy, int w, int h)
{
    for (int j = 0; j < h; j++) {
        int ys = sy + j, yd = dy + j;
        if (ys < 0 || ys >= SCR_H || yd < 0 || yd >= SCR_H) continue;
        for (int i = 0; i < w; i++) {
            int xs = sx + i, xd = dx + i;
            if (xs >= 0 && xs < SCR_W && xd >= 0 && xd < SCR_W) g_surf[dst][yd * SCR_W + xd] = g_surf[src][ys * SCR_W + xs];
        }
    }
}

void mui_fill(int surf, int x0, int y0, int x1, int y1, int c)
{
    clamp_rect(&x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++)
        if (x1 > x0) memset(g_surf[surf] + y * SCR_W + x0, c, (size_t)(x1 - x0));
}

/* the 8x8 pattern 0x437bd8 (255 = key, 0 = black) tiled over the surface: the "dimmed" background of the dialogs */
void mui_dither(int surf)
{
    for (int y = 0; y < SCR_H; y++)
        for (int x = (y & 1) ? 0 : 1; x < SCR_W; x += 2) g_surf[surf][y * SCR_W + x] = 0;
}

void mui_present(void) { memcpy(g_surf[0], g_surf[1], sizeof g_surf[0]); }

/* ---- .img fonts: u8 present[256]; s16 w, h, colour, key; 256 cells of w*h ------------------------------------------- */
static const char *const FONT_FILE[F_COUNT] = { "ascii12.img", "cyber.img", "butfnt.img", "stdfont.img", "midfont.img",
                                                "carved.img", "smallbt.img" };
typedef struct { int w, h, c, key, ok; uint8_t present[256]; uint8_t *cells; } Font;
static Font g_font[F_COUNT];
static int g_cur_font = F_STD;

static void font_load(Font *f, const char *name)
{
    size_t n = 0;
    uint8_t *d = res_read(g_mrcs, res_find(g_mrcs, name), &n);
    if (!d || n < 264) { free(d); fprintf(stderr, "menu: font %s missing\n", name); return; }
    memcpy(f->present, d, 256);
    f->w = (int16_t)(d[256] | d[257] << 8);
    f->h = (int16_t)(d[258] | d[259] << 8);
    f->c = (int16_t)(d[260] | d[261] << 8);
    f->key = (int16_t)(d[262] | d[263] << 8);
    size_t cells = (size_t)256 * f->w * f->h;
    if (f->w > 0 && f->h > 0 && 264 + cells <= n) {
        f->cells = malloc(cells);
        memcpy(f->cells, d + 264, cells);
        f->ok = 1;
    }
    free(d);
}

void mui_set_font(int font) { if (font >= 0 && font < F_COUNT) g_cur_font = font; }
int mui_get_colour(void) { return g_font[g_cur_font].c; }
int mui_font_w(void) { return g_font[g_cur_font].w; }
int mui_font_h(void) { return g_font[g_cur_font].h; }

/* sub_40f6d0: the colours live in the glyph pixels; changing one replaces it in every present glyph, in place (so a
 * shading pixel that happens to have the new colour follows later changes too, as in the original) */
static void font_recolour(Font *f, int from, int to)
{
    if (!f->ok || from == to) return;
    size_t cell = (size_t)f->w * f->h;
    for (int c = 0; c < 256; c++) {
        if (!f->present[c]) continue;
        uint8_t *p = f->cells + c * cell;
        for (size_t i = 0; i < cell; i++)
            if (p[i] == (uint8_t)from) p[i] = (uint8_t)to;
    }
}

void mui_set_colour(int c)
{
    Font *f = &g_font[g_cur_font];
    if (c == f->c) return;
    font_recolour(f, f->c, c);
    f->c = c;
}

void mui_set_key(int c)
{
    Font *f = &g_font[g_cur_font];
    if (c == f->key) return;
    font_recolour(f, f->key, c);
    f->key = c;
}

/* ascii_draw_string 0x40f580: fixed cells, '\n' goes back to x = 0 (sic), missing characters take no room */
static void draw_string(int surf, int x, int y, const char *s)
{
    Font *f = &g_font[g_cur_font];
    if (!f->ok) return;
    size_t cell = (size_t)f->w * f->h;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c == '\n') {
            y += f->h;
            if (y >= SCR_H) break;
            x = 0;
            continue;
        }
        if (c == '\t') { x += f->present[9] ? f->w : 0; continue; }
        if (!f->present[c]) continue;
        const uint8_t *p = f->cells + c * cell;
        for (int j = 0; j < f->h; j++) {
            int yy = y + j;
            if (yy < 0 || yy >= SCR_H) continue;
            for (int i = 0; i < f->w; i++) {
                int xx = x + i;
                uint8_t v = p[j * f->w + i];
                if (v != 255 && xx >= 0 && xx < SCR_W) g_surf[surf][yy * SCR_W + xx] = v;
            }
        }
        x += f->w;
        if (x > SCR_W) break;
    }
}

void mui_print(int surf, int x, int y, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    draw_string(surf, x, y, buf);
}

void mui_get_centered(int l, int t, int r, int b, int font, const char *text, int *x, int *y)
{
    mui_set_font(font);
    int tw = (int)strlen(text) * mui_font_w(), th = mui_font_h();
    *x = l + (r - l + 1 - tw) / 2;
    *y = t + (b - t + 1 - th) / 2;
}

void mui_print_centered(int surf, int l, int t, int r, int b, int font, const char *text)
{
    int x, y;
    mui_get_centered(l, t, r, b, font, text, &x, &y);
    draw_string(surf, x, y, text);
}

/* sub_40d130: word by word into a box of the given width; '\n' starts a line */
int mui_print_wrapped(int surf, int x0, int y, int width, const char *text)
{
    int cw = mui_font_w(), ch = mui_font_h(), x = x0;
    const char *p = text;
    while (*p) {
        if (*p == '\n') { y += ch; x = x0; p++; continue; }
        const char *e = p;
        while (*e && *e != ' ' && *e != '\n') e++;
        while (*e == ' ') e++;
        int len = (int)(e - p), ww = len * cw;
        if (x != x0 && x + ww >= x0 + width) { y += ch; x = x0; }
        char word[128];
        if (len > 127) len = 127;
        memcpy(word, p, (size_t)len);
        word[len] = 0;
        draw_string(surf, x, y, word);
        x += len * cw;
        p = e;
    }
    return y;
}

/* ---- sound ----------------------------------------------------------------------------------------------------------- */
int g_music = -1;
int g_sound_ok = 1;
typedef struct { char name[24]; Sample s; int ok; } Snd;
static Snd g_snd[96];
static int g_nsnd;
static struct { int voice, ds; } g_vol[64];

static const Sample *snd_sample(const char *name)
{
    for (int i = 0; i < g_nsnd; i++)
        if (!_stricmp(g_snd[i].name, name)) return g_snd[i].ok ? &g_snd[i].s : NULL;
    if (g_nsnd >= (int)(sizeof g_snd / sizeof g_snd[0])) return NULL;
    Snd *s = &g_snd[g_nsnd++];
    snprintf(s->name, sizeof s->name, "%s", name);
    size_t n = 0;
    uint8_t *d = res_read(g_mrcs, res_find(g_mrcs, name), &n);
    s->ok = d && sample_decode_wav(d, n, &s->s);
    if (!s->ok) fprintf(stderr, "menu: sound %s missing\n", name);
    free(d);
    return s->ok ? &s->s : NULL;
}

/* DirectSound volume in 1/100 dB (0 = full, -10000 = silent) as a linear gain */
static float ds_gain(int ds) { return ds >= 0 ? 1.0f : (float)pow(10.0, ds / 2000.0); }

static void vol_remember(int voice, int ds)
{
    int free_slot = -1;
    for (int i = 0; i < 64; i++) {
        if (g_vol[i].voice == voice) { g_vol[i].ds = ds; return; }
        if (free_slot < 0 && (g_vol[i].voice <= 0 || !audio_playing(g_vol[i].voice))) free_slot = i;
    }
    if (free_slot >= 0) { g_vol[free_slot].voice = voice; g_vol[free_slot].ds = ds; }
}

static int start_voice(const char *wav, int ds, int loop)
{
    const Sample *s = g_sound_ok ? snd_sample(wav) : NULL;
    if (!s) return -1;
    int v = audio_play(s, ds_gain(ds), 0, 1, loop);
    if (v <= 0) return -1;
    vol_remember(v, ds);
    return v;
}

int snd_busy(int voice) { return voice > 0 && audio_playing(voice); }
void snd_kill(int voice) { if (voice > 0) audio_stop(voice); }

void snd_stop(int *h)
{
    if (*h > 0) audio_stop(*h);
    *h = -1;
}

void play_sfx(int *h, const char *wav)
{
    if (!g_options.sfx_on) { *h = -1; return; }
    snd_stop(h);
    *h = start_voice(wav, g_options.sfx_vol * 30 - 3100, 0);
}

void play_music(int *h, const char *wav)
{
    if (!g_options.music_on) { *h = -1; return; }
    snd_stop(h);
    *h = start_voice(wav, g_options.music_vol * 30 - 3100, 1);
}

int snd_play_plain(const char *wav) { return start_voice(wav, 0, 0); }

int snd_set_volume(int *h, int ds)
{
    if (*h > 0 && audio_playing(*h)) {
        audio_set(*h, ds_gain(ds), 0, 1);
        vol_remember(*h, ds);
        return 1;
    }
    snd_stop(h);
    return 0;
}

void snd_set_volume_pct(int *h, int pct) { snd_set_volume(h, pct * 30 - 3100); }

int snd_get_volume(int *h)
{
    if (*h > 0 && audio_playing(*h))
        for (int i = 0; i < 64; i++)
            if (g_vol[i].voice == *h) return g_vol[i].ds;
    return -3100;
}

/* ---- the screen: surface 0 through the palette, plus the hand and the focus outline ------------------------------ */
static int g_cursor_w, g_cursor_h;
static uint8_t g_rgba[SCR_W * SCR_H * 4];
static int g_mouse_on = 1;
static float g_mx, g_my;
static double g_last_frame;
static const char *g_shot_screen, *g_shot_png;
static char g_tag[32];
static double g_tag_time;

static MuiForm *g_cur;
static int g_focus = -1, g_capture = -1;
static int g_edit_insert;                  /* the edit box: Insert toggles insert / overwrite */
static void check_shot(void);

void mui_mouse_show(int on) { g_mouse_on = on; }
void mui_screen_tag(const char *tag) { snprintf(g_tag, sizeof g_tag, "%s", tag); g_tag_time = mui_ticks(); }
void mui_set_shot(const char *screen, const char *png) { g_shot_screen = screen; g_shot_png = png; }
void mui_quit(void) { longjmp(g_menu_quit, 1); }

static void surface_to_rgba(const uint8_t *s, uint8_t *out)
{
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        const uint8_t *c = g_pal + s[i] * 3;
        out[i * 4] = c[0]; out[i * 4 + 1] = c[1]; out[i * 4 + 2] = c[2]; out[i * 4 + 3] = 255;
    }
}

static int write_png(const char *path)
{
    Image img = { SCR_W, SCR_H, malloc((size_t)SCR_W * SCR_H * 4), NULL, { 0 }, 32 };
    surface_to_rgba(g_surf[0], img.rgba);
    int ok = image_write_png(path, &img);
    free(img.rgba);
    return ok;
}

static void render(void)
{
    surface_to_rgba(g_surf[0], g_rgba);
    if (!g_tex) g_tex = gfx_texture(g_rgba, SCR_W, SCR_H, TEX_CLAMP | TEX_NEAREST);
    else gfx_texture_update(g_tex, g_rgba, SCR_W, SCR_H);
    int w, h;
    plat_size(&w, &h);
    gfx_begin(w, h, 0, 0, 0);
    gfx_2d_begin(SCR_W, SCR_H);
    gfx_2d_quad(g_tex, 0, 0, SCR_W, SCR_H, 0, 0, 1, 1, 0xffffffffu, BLEND_NONE);
    /* port extra: a focused area (slot row, slider, tab...) has no picture of its own in the original */
    if (g_cur && g_mui_kbd && g_focus >= 0 && g_focus < g_cur->n && g_cur->c[g_focus].type == CT_AREA) {
        const MuiCtrl *c = &g_cur->c[g_focus];
        uint32_t col = 0xffd23cffu;
        gfx_2d_rect((float)c->x - 2, (float)c->y - 2, (float)c->w + 4, 2, col);
        gfx_2d_rect((float)c->x - 2, (float)(c->y + c->h), (float)c->w + 4, 2, col);
        gfx_2d_rect((float)c->x - 2, (float)c->y, 2, (float)c->h, col);
        gfx_2d_rect((float)(c->x + c->w), (float)c->y, 2, (float)c->h, col);
    }
    /* the mouse: menu anim 2 (the hand), drawn at its top-left corner as draw_mouse 0x4108c0 does */
    if (g_mouse_on && !g_mui_kbd) {
        if (!g_cursor_tex) {
            Spr *s = spr(mui_anim_frame(2, 0));
            if (s) {
                uint8_t *px = malloc((size_t)s->w * s->h * 4);
                for (int i = 0; i < s->w * s->h; i++) {
                    const uint8_t *c = g_pal + s->px[i] * 3;
                    px[i * 4] = c[0]; px[i * 4 + 1] = c[1]; px[i * 4 + 2] = c[2]; px[i * 4 + 3] = s->px[i] == 255 ? 0 : 255;
                }
                g_cursor_tex = gfx_texture(px, s->w, s->h, TEX_CLAMP | TEX_NEAREST);
                g_cursor_w = s->w;
                g_cursor_h = s->h;
                free(px);
            }
        }
        if (g_cursor_tex)
            gfx_2d_quad(g_cursor_tex, g_mx, g_my, (float)g_cursor_w, (float)g_cursor_h, 0, 0, 1, 1, 0xffffffffu, BLEND_ALPHA);
    }
    gfx_2d_end();
    plat_swap();
}

static unsigned g_mbuttons, g_mclicked;
static int g_stick_dir;                     /* the left stick as a d-pad: -1 up, 1 down, 0 */

void mui_pump(void)
{
    check_shot();
    /* keep a frame from running much faster than the display when vsync is off */
    double now = plat_time();
    if (now - g_last_frame < 0.008) plat_sleep_ms((int)((0.008 - (now - g_last_frame)) * 1000) + 1);
    g_last_frame = plat_time();
    if (!plat_poll()) mui_quit();
    int sx, sy;
    plat_mouse(&sx, &sy, &g_mbuttons, &g_mclicked);
    float vx, vy;
    gfx_screen_to_2d(sx, sy, &vx, &vy);
    if ((vx != g_mx || vy != g_my) && g_last_frame > 0.5) g_mui_kbd = 0;
    if (g_mclicked) g_mui_kbd = 0;
    g_mx = vx;
    g_my = vy;
    if (plat_any_key_pressed() || plat_pad()->pressed) g_mui_kbd = 1;
    screenshot_key();
    render();
}

void mui_handle_mouse(void) { mui_pump(); }

int mui_key_held(int vk) { return plat_key(vk); }

int mui_mouse(int *x, int *y)
{
    if (x) *x = (int)g_mx;
    if (y) *y = (int)g_my;
    return (int)(g_mbuttons & 3);
}

void mui_wait_mouse_released(void)
{
    while (mui_mouse(NULL, NULL) & 1) mui_pump();
}

/* screenshot_key 0x409660: Ctrl+F10 -> scr%02d.pcx; here a PNG in the user folder */
void screenshot_key(void)
{
    static int n;
    if (plat_key(VK_CONTROL) && plat_key_pressed(VK_F1 + 9)) {
        char name[32], path[1024];
        snprintf(name, sizeof name, "scr%02d.png", n++);
        snprintf(path, sizeof path, "%s%s", plat_user_dir(), name);
        if (write_png(path)) printf("screenshot: %s\n", path);
    }
}

/* ---- forms ---------------------------------------------------------------------------------------------------------- */
MuiForm g_form_main, g_form_nspeler, g_form_garage, g_form_kopen, g_form_bedien, g_form_opties, g_form_hiscore,
        g_form_janee, g_form_nsp;

MuiForm *mui_form(void) { return g_cur; }
int mui_get_focus(void) { return g_focus; }

static int font_of(const char *name)
{
    for (int i = 0; i < F_COUNT; i++)
        if (name && !_stricmp(name, FONT_FILE[i])) return i;
    return -1;
}

static int32_t pe_s32(const PeFile *pe, uint32_t va) { return (int32_t)pe_u32(pe, va); }

/* a type-1 control struct {int anim, w, h; char key; char *text; int text_type; char *font; int align; int focus_anim} */
static void read_button(const PeFile *pe, uint32_t ctrl, MuiCtrl *c)
{
    c->anim = pe_s32(pe, ctrl);
    c->w = pe_s32(pe, ctrl + 4);
    c->h = pe_s32(pe, ctrl + 8);
    const uint8_t *k = pe_ptr(pe, ctrl + 12, 1);
    c->key = k ? (char)*k : 0;
    const char *t = pe_str(pe, pe_u32(pe, ctrl + 13));
    snprintf(c->text, sizeof c->text, "%s", t ? t : "");
    c->font = pe_s32(pe, ctrl + 0x11) == 1 && t ? font_of(pe_str(pe, pe_u32(pe, ctrl + 0x15))) : -1;
    int spr0 = mui_anim_frame(c->anim, 0);
    if (c->w == -1) c->w = mui_sprite_w(spr0);
    if (c->h == -1) c->h = mui_sprite_h(spr0);
}

/* the static forms of menu.dat: 28-byte entries {x, y, type, ctrl*, style, 0, 0} ending with type -1 */
static void load_form(const PeFile *pe, uint32_t va, MuiForm *f)
{
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 24; i++) {
        uint32_t a = va + (uint32_t)i * 28;
        int type = pe_s32(pe, a + 8);
        if (type == -1) break;
        MuiCtrl *c = &f->c[f->n++];
        c->x = pe_s32(pe, a);
        c->y = pe_s32(pe, a + 4);
        c->type = type;
        c->style = pe_s32(pe, a + 16);
        c->font = -1;
        uint32_t ctrl = pe_u32(pe, a + 12);
        if (type == CT_BUTTON) read_button(pe, ctrl, c);
        else if (type == CT_AREA) {
            c->w = pe_s32(pe, ctrl);
            c->h = pe_s32(pe, ctrl + 4);
            const uint8_t *k = pe_ptr(pe, ctrl + 0x10, 1);
            c->key = k ? (char)*k : 0;
        }
    }
}

static void add_button(const PeFile *pe, MuiForm *f, int x, int y, uint32_t ctrl, int style)
{
    MuiCtrl *c = &f->c[f->n++];
    memset(c, 0, sizeof *c);
    c->x = x; c->y = y; c->type = CT_BUTTON; c->style = style;
    read_button(pe, ctrl, c);
}

static void ring_default(MuiForm *f, int n)
{
    f->nring = n;
    for (int i = 0; i < n; i++) f->ring[i] = i;
}

static void ring_set(MuiForm *f, const int *idx, int n)
{
    f->nring = n;
    for (int i = 0; i < n; i++) f->ring[i] = idx[i];
}

static void build_forms(const PeFile *pe)
{
    load_form(pe, 0x42ed40, &g_form_main);
    load_form(pe, 0x430080, &g_form_nspeler);
    load_form(pe, 0x42b628, &g_form_garage);
    load_form(pe, 0x42da50, &g_form_kopen);
    load_form(pe, 0x42a238, &g_form_bedien);
    load_form(pe, 0x430f60, &g_form_opties);
    load_form(pe, 0x42c2f8, &g_form_hiscore);
    /* janee_init_form 0x402670 / nsp_req_init_form 0x40a590 build theirs at start-up from the screen size */
    int px = (SCR_W - 401) / 2, py = (SCR_H - 222) / 2;
    memset(&g_form_janee, 0, sizeof g_form_janee);
    add_button(pe, &g_form_janee, px + 0x1a, py + 0xb8, 0x429bf0, 1);     /* JA */
    add_button(pe, &g_form_janee, px + 0xd8, py + 0xb8, 0x429c15, 3);     /* NEE */
    memset(&g_form_nsp, 0, sizeof g_form_nsp);
    add_button(pe, &g_form_nsp, px + 0x18, py + 0xb7, 0x434a88, 1);       /* OK */
    add_button(pe, &g_form_nsp, px + 0xd6, py + 0xb7, 0x434aad, 3);       /* Annuleren */
    MuiCtrl *e = &g_form_nsp.c[g_form_nsp.n++];                           /* the name: owner-drawn edit box 0x434ad8 */
    memset(e, 0, sizeof *e);
    e->x = 0x118; e->y = 0xff; e->type = CT_EDIT; e->style = 1;
    e->maxlen = pe_s32(pe, 0x434ad8);
    if (e->maxlen <= 0 || e->maxlen > 15) e->maxlen = 8;
    e->font = font_of(pe_str(pe, pe_u32(pe, 0x434ad8 + 12)));
    const char *def = pe_str(pe, pe_u32(pe, 0x434ad8 + 8));
    snprintf(e->text, sizeof e->text, "%s", def ? def : "");
    e->w = e->maxlen * g_font[e->font >= 0 ? e->font : F_MID].w;
    e->h = g_font[e->font >= 0 ? e->font : F_MID].h;
    /* the focus rings: the original's focus_next(n) range, plus (port extra) the clickable areas */
    ring_default(&g_form_main, 7);
    static const int R_NSP[] = { 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 1, 3 };
    ring_set(&g_form_nspeler, R_NSP, 12);
    ring_default(&g_form_garage, 10);
    ring_default(&g_form_kopen, 5);
    static const int R_BED[] = { 3, 4, 5, 6, 7, 8, 0, 1 };
    ring_set(&g_form_bedien, R_BED, 8);
    static const int R_OPT[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1 };
    ring_set(&g_form_opties, R_OPT, 11);
    static const int R_HI[] = { 2, 3, 4, 5, 6, 7, 8, 9, 0 };
    ring_set(&g_form_hiscore, R_HI, 9);
    ring_default(&g_form_janee, 2);
    static const int R_NSPQ[] = { 2, 0, 1 };
    ring_set(&g_form_nsp, R_NSPQ, 3);
}

/* draw_button 0x41b3c0: anim frame 0 normal, 1 pressed, 2 disabled (laid over the text), 3 focused; the caption in
 * the control's .img font centred in the window (sub_411280), which puts the current font back afterwards
 * (ascii_get_font 0x411318 / ascii_set_font 0x41147e): text printed after a button redraw keeps its own font (the
 * key help of the controls screen right after mui_focus would otherwise come out in butfnt) */
enum { BS_NORMAL, BS_FOCUS, BS_PRESSED, BS_DISABLED };
static void draw_button(const MuiCtrl *c, int st)
{
    int frame = st == BS_PRESSED ? 1 : st == BS_FOCUS ? 3 : st == BS_DISABLED && c->font < 0 ? 2 : 0;
    mui_blit(0, mui_anim_frame(c->anim, frame), c->x + c->w / 2, c->y + c->h / 2, 0);
    if (c->font >= 0 && c->text[0]) {
        int keep = g_cur_font;
        mui_set_font(c->font);
        int tw = (int)strlen(c->text) * mui_font_w();
        mui_print(0, c->x + (c->w + 1) / 2 - tw / 2, c->y + (c->h + 1) / 2 - mui_font_h() / 2, "%s", c->text);
        g_cur_font = keep;
        if (st == BS_DISABLED) mui_blit(0, mui_anim_frame(c->anim, 2), c->x + c->w / 2, c->y + c->h / 2, 0);
    }
}

/* the owner-drawn edit box (Ownedit.cpp): maxlen+1 cells with an opaque background, the cursor cell in its colours.
 * Its draw (sub_4176d0) never selects a font (it prints in the current one, the midfont its screens set); here the
 * control's font is set for it and the current one put back afterwards, so text printed after a redraw keeps its font */
static void draw_edit(const MuiCtrl *c, int focused)
{
    int keep_font = g_cur_font;
    mui_set_font(c->font >= 0 ? c->font : F_MID);
    int keep = mui_get_colour();
    int cw = mui_font_w();
    char pad[32];
    int cells = c->maxlen + 1;
    memset(pad, ' ', (size_t)cells);
    pad[cells] = 0;
    mui_set_colour(focused ? c->col[2] : c->col[0]);
    mui_set_key(focused ? c->col[3] : c->col[1]);
    mui_print(0, c->x, c->y, "%s", pad);
    mui_print(0, c->x, c->y, "%s", c->buf);
    if (focused) {
        mui_set_colour(c->col[4]);
        mui_set_key(c->col[5]);
        char ch[2] = { c->buf[c->cursor] ? c->buf[c->cursor] : ' ', 0 };
        mui_print(0, c->x + c->cursor * cw, c->y, "%s", ch);
    }
    mui_set_key(255);
    mui_set_colour(keep);
    g_cur_font = keep_font;
}

void mui_redraw_ctrl(int idx)
{
    if (!g_cur || idx < 0 || idx >= g_cur->n) return;
    MuiCtrl *c = &g_cur->c[idx];
    if (c->type == CT_BUTTON)
        draw_button(c, !c->enabled ? BS_DISABLED : g_capture == idx ? BS_PRESSED : g_focus == idx ? BS_FOCUS : BS_NORMAL);
    else if (c->type == CT_EDIT) draw_edit(c, g_focus == idx);
}

void mui_create_form(MuiForm *f)
{
    g_cur = f;
    g_focus = -1;
    g_capture = -1;
    for (int i = 0; i < f->n; i++) {
        MuiCtrl *c = &f->c[i];
        c->enabled = 1;
        if (c->type == CT_EDIT) {
            snprintf(c->buf, sizeof c->buf, "%.*s", c->maxlen, c->text);
            c->cursor = 0;
            g_edit_insert = 0;
            static const int COL[6] = { 0xfe, 1, 2, 0xfe, 0xc8, 0xc9 };     /* sub_4179a0 defaults of the create */
            memcpy(c->col, COL, sizeof COL);
        }
    }
    for (int i = 0; i < f->n; i++)
        if (f->c[i].style == 1) g_focus = i;           /* control state 1 = focused (the last one wins) */
    for (int i = 0; i < f->n; i++) mui_redraw_ctrl(i);
}

void mui_destroy_form(void)
{
    plat_text_mode(0);
    g_cur = NULL;
    g_focus = -1;
    g_capture = -1;
}

void mui_focus(int idx)
{
    if (!g_cur || idx < 0 || idx >= g_cur->n || !g_cur->c[idx].enabled || idx == g_focus) return;
    int old = g_focus;
    g_focus = idx;
    mui_redraw_ctrl(old);
    mui_redraw_ctrl(idx);
}

void mui_enable(int idx, int on)
{
    if (!g_cur || idx < 0 || idx >= g_cur->n) return;
    g_cur->c[idx].enabled = on;
    if (!on && g_focus == idx) g_focus = -1;
    mui_redraw_ctrl(idx);
}

/* focus_next / focus_prev: (focus +- 1) mod n over the first n controls; here over the form's ring (see the top) */
static int ring_step(int n, int dir)
{
    if (!g_cur) return 0;
    int cnt = g_cur->nring > 0 ? g_cur->nring : n;
    int pos = -1;
    for (int i = 0; i < cnt; i++)
        if ((g_cur->nring > 0 ? g_cur->ring[i] : i) == g_focus) pos = i;
    for (int k = 0; k < cnt; k++) {
        pos = pos < 0 ? (dir > 0 ? 0 : cnt - 1) : (pos + dir + cnt) % cnt;
        int idx = g_cur->nring > 0 ? g_cur->ring[pos] : pos;
        if (idx < g_cur->n && g_cur->c[idx].enabled) return idx;
    }
    return g_focus < 0 ? 0 : g_focus;
}

int focus_next(int n) { return ring_step(n, 1); }
int focus_prev(int n) { return ring_step(n, -1); }

static int ctrl_at(int x, int y)
{
    if (!g_cur) return -1;
    /* the last created window is on top */
    for (int i = g_cur->n - 1; i >= 0; i--) {
        const MuiCtrl *c = &g_cur->c[i];
        if (c->enabled && x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) return i;
    }
    return -1;
}

/* the owner-drawn edit box keys (sub_4179d0): a character the font has goes in while both the cursor and the length
 * are below the maximum - overwriting by default (sub_4178d0), inserting after Insert (sub_417810) - so the default
 * "Speler" is typed over. Only the characters of Ownedit's set go in (0x43a58c, set by sub_417520; strstr at 0x417cf2). */
static const char EDIT_CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890.<,/?![]\\%$&*+-=>:;' ";

static void edit_keys(MuiCtrl *c)
{
    int len = (int)strlen(c->buf), changed = 0;
    const Font *f = &g_font[c->font >= 0 ? c->font : F_MID];
    if (plat_key_pressed(VK_INSERT)) g_edit_insert = !g_edit_insert;
    for (const char *t = plat_text_input(); *t; t++) {
        unsigned char ch = (unsigned char)*t;
        if (ch < 32 || !strchr(EDIT_CHARS, ch) || !f->present[ch] || c->cursor >= c->maxlen || len >= c->maxlen) continue;
        if (g_edit_insert) {
            memmove(c->buf + c->cursor + 1, c->buf + c->cursor, (size_t)(len - c->cursor + 1));
            len++;
        } else if (c->cursor == len) c->buf[++len] = 0;
        c->buf[c->cursor++] = (char)ch;
        changed = 1;
    }
    if (plat_key_pressed(VK_BACK) && c->cursor > 0) {
        memmove(c->buf + c->cursor - 1, c->buf + c->cursor, (size_t)(len - c->cursor + 1));
        c->cursor--;
        len--;
        changed = 1;
    }
    if (plat_key_pressed(VK_DELETE) && c->cursor < len) {
        memmove(c->buf + c->cursor, c->buf + c->cursor + 1, (size_t)(len - c->cursor));
        changed = 1;
    }
    if (plat_key_pressed(VK_LEFT) && c->cursor > 0) { c->cursor--; changed = 1; }
    if (plat_key_pressed(VK_RIGHT) && c->cursor < len) { c->cursor++; changed = 1; }
    if (plat_key_pressed(VK_HOME)) { c->cursor = 0; changed = 1; }
    if (plat_key_pressed(VK_END)) { c->cursor = len; changed = 1; }
    if (changed) draw_edit(c, 1);
}

/* PORT: the edit box with a pad (no keyboard on a Steam Deck or a TV): up / down (d-pad or left stick, repeating
 * while held) turn the character under the cursor through the letters the font has, left / right move the cursor,
 * X deletes the character before the cursor; A / Start accept, B cancels (get_menu_event). 1 = the pad was used. */
static int edit_pad(MuiCtrl *c, const PlatPad *pad, int stick)
{
    static const char SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.!";
    static double next;
    static const MuiCtrl *owner;                     /* the box the held direction was pressed on */
    unsigned held = pad->buttons & (PAD_UP | PAD_DOWN);
    int step = (pad->pressed & PAD_UP) || stick < 0 ? 1 : (pad->pressed & PAD_DOWN) || stick > 0 ? -1 : 0;
    double now = mui_ticks();
    if (!held) owner = NULL;
    if (step) { next = now + 400; owner = c; }
    else if (held && owner == c && now >= next) { step = (held & PAD_UP) ? 1 : -1; next = now + 70; }
    int len = (int)strlen(c->buf), used = step != 0;
    const Font *f = &g_font[c->font >= 0 ? c->font : F_MID];
    if (step && c->cursor < c->maxlen) {
        const char *p = strchr(SET, c->cursor < len ? c->buf[c->cursor] : ' ');
        int i = p && *p ? (int)(p - SET) : 0, n = (int)sizeof SET - 1;
        for (int k = 0; k < n; k++) {               /* the next character the font can draw */
            i = (i + step + n) % n;
            if (SET[i] == ' ' || f->present[(unsigned char)SET[i]]) break;
        }
        if (c->cursor == len) { if (SET[i] != ' ') { c->buf[len] = SET[i]; c->buf[len + 1] = 0; } }
        else c->buf[c->cursor] = SET[i];
        len = (int)strlen(c->buf);
        while (len > 0 && c->buf[len - 1] == ' ' && c->cursor < len - 1) c->buf[--len] = 0;  /* no trailing blanks */
    }
    if ((pad->pressed & PAD_LEFT) && c->cursor > 0) { c->cursor--; used = 1; }
    if ((pad->pressed & PAD_RIGHT) && c->cursor < len && c->cursor < c->maxlen - 1) { c->cursor++; used = 1; }
    if ((pad->pressed & PAD_X) && c->cursor > 0) {
        memmove(c->buf + c->cursor - 1, c->buf + c->cursor, (size_t)(len - c->cursor + 1));
        c->cursor--;
        used = 1;
    }
    if (pad->pressed & (PAD_LEFT | PAD_RIGHT | PAD_X)) used = 1;
    if (used) draw_edit(c, 1);
    return used;
}

/* --menu-shot SCREEN: once the form is up and 2.5 s have passed; SCREEN@MS: MS milliseconds after the screen opened,
 * form or not (to look at the animations) */
static void check_shot(void)
{
    if (!g_shot_screen) return;
    const char *at = strchr(g_shot_screen, '@');
    size_t n = at ? (size_t)(at - g_shot_screen) : strlen(g_shot_screen);
    if (strlen(g_tag) != n || _strnicmp(g_tag, g_shot_screen, n)) return;
    if (at ? mui_ticks() - g_tag_time < atoi(at + 1) : (!g_cur || mui_ticks() - g_tag_time < 2500)) return;
    int ok = write_png(g_shot_png);
    printf("menu-shot %s: %s %s\n", g_tag, g_shot_png, ok ? "written" : "FAILED");
    mui_quit();
}

/* development (--menu-script): a list of events fed to get_menu_event instead of the input, one per call once a
 * form is up: a control index, esc up down left right enter, wN (N frames nothing), text:NAME (into the focused edit
 * box), shot:FILE.png (the screen now), quit */
static char g_script[1024];
static int g_script_pos, g_script_wait;

void mui_set_script(const char *s) { snprintf(g_script, sizeof g_script, "%s", s); g_script_pos = 0; }

static int script_event(void)
{
    if (g_script_wait > 0) { g_script_wait--; return EV_NONE; }
    const char *p = g_script + g_script_pos;
    while (*p == ' ' || *p == ',') p++;
    if (!*p) { g_script[0] = 0; return EV_NONE; }
    char tok[256];
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != ',' && n < 255) { tok[n] = p[n]; n++; }
    tok[n] = 0;
    g_script_pos = (int)(p + n - g_script);
    printf("script [%s] %s\n", g_tag, tok);
    if (tok[0] >= '0' && tok[0] <= '9') return atoi(tok);
    g_mui_kbd = strcmp(tok, "esc") && strncmp(tok, "shot:", 5) && tok[0] != 'w' ? 1 : g_mui_kbd;
    if (!strcmp(tok, "esc")) return EV_ESC;
    if (!strcmp(tok, "up")) return EV_UP;
    if (!strcmp(tok, "down")) return EV_DOWN;
    if (!strcmp(tok, "left")) return EV_LEFT;
    if (!strcmp(tok, "right")) return EV_RIGHT;
    if (!strcmp(tok, "enter")) {
        MuiCtrl *fc = g_focus >= 0 ? &g_cur->c[g_focus] : NULL;
        return fc && fc->enabled && fc->type != CT_EDIT ? g_focus : EV_ENTER;
    }
    if (tok[0] == 'w') { g_script_wait = atoi(tok + 1); return EV_NONE; }
    if (!strncmp(tok, "text:", 5) && g_focus >= 0 && g_cur->c[g_focus].type == CT_EDIT) {
        MuiCtrl *c = &g_cur->c[g_focus];
        snprintf(c->buf, sizeof c->buf, "%.*s", c->maxlen, tok + 5);
        c->cursor = (int)strlen(c->buf);
        draw_edit(c, 1);
        return EV_NONE;
    }
    if (!strncmp(tok, "shot:", 5)) { printf("script shot %s: %d\n", tok + 5, write_png(tok + 5)); return EV_NONE; }
    if (!strcmp(tok, "quit")) mui_quit();
    return EV_NONE;
}

/* get_menu_event 0x409d00 / handle_form 0x411e90: one frame, then what happened */
int get_menu_event(void)
{
    mui_pump();
    if (!g_cur) return EV_NONE;
    if (g_script[0]) return script_event();
    const PlatPad *pad = plat_pad();
    MuiCtrl *fc = g_focus >= 0 && g_focus < g_cur->n ? &g_cur->c[g_focus] : NULL;
    /* the mouse: a button fires when released over it, an area as soon as it is pressed */
    int mx = (int)g_mx, my = (int)g_my;
    if (g_mclicked & 1) {
        int i = ctrl_at(mx, my);
        if (i >= 0) {
            MuiCtrl *c = &g_cur->c[i];
            if (c->type == CT_BUTTON) {
                int old = g_focus;
                g_focus = i;
                g_capture = i;
                mui_redraw_ctrl(old);
                mui_redraw_ctrl(i);
                if (!(g_mbuttons & 1)) {            /* pressed and let go within one frame (a tap) */
                    g_capture = -1;
                    mui_redraw_ctrl(i);
                    return i;
                }
            } else {
                mui_focus(i);
                if (c->type == CT_AREA) return i;
            }
        }
    }
    if (g_capture >= 0 && !(g_mbuttons & 1)) {
        int i = g_capture;
        g_capture = -1;
        mui_redraw_ctrl(i);
        if (ctrl_at(mx, my) == i) return i;
    }
    /* keys and the pad */
    plat_text_mode(fc && fc->type == CT_EDIT);   /* Android: the soft keyboard only for the name */
    if (fc && fc->type == CT_EDIT) edit_keys(fc);
    if (plat_key_pressed(VK_ESCAPE) || (pad->pressed & PAD_B)) return EV_ESC;
    int up = plat_key_pressed(VK_UP) || (pad->pressed & PAD_UP);
    int down = plat_key_pressed(VK_DOWN) || (pad->pressed & PAD_DOWN);
    int sd = pad->ly < -0.6f ? -1 : pad->ly > 0.6f ? 1 : 0, stick = 0;
    if (sd != g_stick_dir) {
        if (sd < 0) up = 1;
        if (sd > 0) down = 1;
        g_stick_dir = sd;
        stick = sd;
        if (sd) g_mui_kbd = 1;
    }
    if (fc && fc->type == CT_EDIT && fc->enabled && edit_pad(fc, pad, stick)) return EV_NONE;
    if (up) return EV_UP;
    if (down) return EV_DOWN;
    int edit = fc && fc->type == CT_EDIT;
    if ((plat_key_pressed(VK_LEFT) && !edit) || (pad->pressed & PAD_LEFT)) return EV_LEFT;
    if ((plat_key_pressed(VK_RIGHT) && !edit) || (pad->pressed & PAD_RIGHT)) return EV_RIGHT;
    if ((plat_key_pressed(VK_RETURN) && !plat_key(VK_MENU)) || (pad->pressed & (PAD_A | PAD_START))) {   /* Alt+Enter = full screen */
        if (fc && fc->enabled && fc->type != CT_EDIT) return g_focus;
        return EV_ENTER;
    }
    /* Alt + hot key (check_form_key_pressed 0x411ca0) */
    int vk = plat_any_key_pressed();
    if (vk && plat_key(VK_MENU) && vk != VK_RETURN)
        for (int i = 0; i < g_cur->n; i++) {
            MuiCtrl *c = &g_cur->c[i];
            if (c->enabled && c->key && (c->key == vk || (c->key >= 'a' && c->key - 32 == vk))) {
                mui_focus(i);
                return i;
            }
        }
    return EV_NONE;
}

/* ---- special_anim.cpp ------------------------------------------------------------------------------------------- */
/* create_sine_table 0x40bd90: n steps of sin from 0 to pi/2 times amp (truncated), the last one exactly amp */
static void create_sine_table(int *t, int n, int amp)
{
    for (int i = 0; i < n; i++) t[i] = (int)(sin(i * 1.57 / n) * amp);
    t[n - 1] = amp;
}

void init_animated_header(AnimHeader *a, int header)
{
    memset(a, 0, sizeof *a);
    a->header = header;
    create_sine_table(a->sine, 10, mui_sprite_h(mui_anim_frame(3, header)));
}

/* the title bar drops in from above in 10 steps of 20 ms */
void next_animated_header_frame(AnimHeader *a)
{
    if (a->done) return;
    double now = mui_ticks();
    if (now - a->tick >= 20) {
        a->tick = now;
        int s = mui_anim_frame(3, a->header);
        mui_blit(1, s, 0, a->sine[a->frame] - mui_sprite_h(s), 3);
        a->frame++;
    }
    if (a->frame >= 10) a->done = 1;
}

void init_animated_buttons(AnimButtons *b)
{
    int h = -1;
    b->n = 0;
    b->done = 0;
    play_sfx(&h, "slide.wav");
}

void add_animated_button(AnimButtons *b, int anim, int x0, int y0, int x1, int y1, int delay, const char *text)
{
    if (b->n >= 10) return;
    AnimButton *a = &b->b[b->n];
    memset(a, 0, sizeof *a);
    a->x0 = x0; a->y0 = y0; a->x1 = x1; a->y1 = y1;
    a->anim = anim;
    a->delay = delay;
    int s = mui_anim_frame(anim, 0);
    a->w = mui_sprite_w(s);
    a->h = mui_sprite_h(s);
    snprintf(a->text, sizeof a->text, "%s", text ? text : "");
    create_sine_table(a->sx, 8, x1 - x0);
    create_sine_table(a->sy, 8, y1 - y0);
    b->n++;
}

/* show_button_frame 0x40c660: every 33 ms (after the delay) restore the old place from the save area in surface 2
 * (row = button index), save the new place, draw the button and its caption there */
static void show_button_frame(AnimButton *a, int idx)
{
    if (a->done) return;
    double now = mui_ticks();
    if (now - a->tick < 33) return;
    a->tick = now;
    if (a->delay) { a->delay--; return; }
    int nx = a->x0 + a->sx[a->step], ny = a->y0 + a->sy[a->step], sy = idx * a->h;
    if (a->step) copy_block(2, 0, sy, 1, a->x0 + a->sx[a->step - 1], a->y0 + a->sy[a->step - 1], a->w, a->h);
    copy_block(1, nx, ny, 2, 0, sy, a->w, a->h);
    mui_blit(1, mui_anim_frame(a->anim, 0), nx, ny, 3);
    mui_print_centered(1, nx, ny, nx + a->w - 1, ny + a->h - 1, F_BUT, a->text);
    if (++a->step >= 8) a->done = 1;
}

void handle_animated_buttons(AnimButtons *b)
{
    if (b->done) return;
    int done = 0;
    for (int i = 0; i < b->n; i++) {
        show_button_frame(&b->b[i], i);
        if (b->b[i].done) done++;
    }
    if (done == b->n) b->done = 1;
}

void init_anim_data(SpecialAnim *s, int x, int y, int anim, int bg)
{
    int f0 = mui_anim_frame(bg, 0);
    s->count = mui_anim_count(anim);
    s->w = mui_sprite_w(f0);
    s->h = mui_sprite_h(f0);
    s->x = x;
    s->y = y;
    s->frame = 0;
    s->bg = bg;
    s->tick = mui_ticks();
    s->anim = anim;
}

void set_special_anim(SpecialAnim *s, int anim) { s->anim = anim; }

/* the rotating car: background patch, then the next frame centred on it, every 33 ms */
void handle_special_animation(SpecialAnim *s)
{
    double now = mui_ticks();
    if (now - s->tick <= 33) return;
    mui_blit(1, mui_anim_frame(s->bg, 0), s->x, s->y, 2);
    if (++s->frame >= s->count) s->frame = 0;
    mui_blit(1, mui_anim_frame(s->anim, s->frame), s->x + s->w / 2, s->y + s->h / 2, 0);
    s->tick = now;
}

/* scroll_special_animation 0x40be30: the two cars side by side in surface 2, a w-wide window sliding over them in
 * 20 steps (sine eased), composed over the background patch and copied to the screen each step */
void scroll_special_animation(int from, int to, SpecialAnim *s, int dir)
{
    int w = s->w, h = s->h, sine[20];
    create_sine_table(sine, 20, w);
    int bg = mui_anim_frame(s->bg, 0);
    double last = 0;
    for (int i = 0; i < 20;) {
        if (mui_ticks() - last <= 33) { mui_pump(); continue; }
        last = mui_ticks();
        mui_blit(2, bg, 0, h, 2);
        mui_fill(2, 0, 0, w * 2, h, 0xff);
        mui_blit(2, mui_anim_frame(from, s->frame), w / 2, h / 2, 4);
        mui_blit(2, mui_anim_frame(to, s->frame), w + w / 2, h / 2, 4);
        if (++s->frame >= s->count - 1) s->frame = 0;
        int off = dir ? sine[i] : w - sine[i];
        mui_stretch(2, 2, off, 0, off + w, h, 0, h, w, h * 2, 8);
        mui_stretch(2, 0, 0, h, w, h * 2, s->x, s->y, s->x + w, s->y + h, 0);    /* straight onto the screen */
        i++;
        mui_pump();
    }
}

void init_anim_text(AnimText *t, const char *text, int x, int y, int start, int end)
{
    memset(t, 0, sizeof *t);
    t->x = x;
    t->y = y;
    t->start = start;
    t->end = end;
    snprintf(t->text, sizeof t->text, "%s", text);
    mui_set_font(F_CYBER);
    t->cw = mui_font_w();
    t->ch = mui_font_h();
}

/* special_anim_step 0x40c020: n characters, the last |b-a| of them in a ramp of palette colours from a towards b */
static void special_anim_step(const char *s, int x, int y, int n, int a, int b, int cw)
{
    int span = b > a ? b - a : a - b, sign = b > a ? 1 : -1, first = n - span;
    mui_set_key(255);
    mui_set_colour(a);
    for (int i = 0; i < n; i++) {
        if (i >= first) mui_set_colour(a + sign * (i - first + 1));
        mui_print(1, x + i * cw, y, "%c", s[i]);
    }
}

/* the typewriter text: a character every 33 ms with print.wav, then the colour ramp runs out */
void next_animated_text_frame(AnimText *t)
{
    static int h = -1;
    if (t->done) return;
    int len = (int)strlen(t->text), span = t->end > t->start ? t->end - t->start : t->start - t->end;
    if (mui_ticks() - t->tick > 33) {
        t->tick = mui_ticks();
        mui_set_font(F_STD);                 /* sic: drawn with stdfont, measured with cyber (both 8x17) */
        int keep = mui_get_colour();
        if (t->counter <= len) {
            if (t->text[t->counter] != ' ') play_sfx(&h, "print.wav");
            special_anim_step(t->text, t->x, t->y, t->counter, t->start, t->end, t->cw);
        } else {
            int e = t->end > t->start ? len - t->counter + t->end - 1 : t->counter - len + t->end - 1;
            special_anim_step(t->text, t->x, t->y, len, t->start, e, t->cw);
        }
        mui_set_colour(keep);
        t->counter++;
    }
    if (t->counter >= len + span) t->done = 1;
}

/* show_property_bar 0x40c960: "Snelheid" etc., the bar (anim 0x35 frame bar) cut to value %, the value and unit */
static void show_property_bar(int surf, int x, int y, int bar, int value)
{
    static const char *const NAME[4] = { "Snelheid", "Acceleratie", "Remmen", "Wegligging" };
    static const char *const UNIT[4] = { "km/h", "%", "%", "%" };
    static const float MIN[4] = { 100, 0, 0, 0 }, MAX[4] = { 300, 100, 100, 100 };
    static const int TOP[4] = { 0x18, 0x3b, 0x5e, 0x81 };
    if (bar < 0 || bar > 3) return;
    int l = x + 0xb, t = y + TOP[bar], r = l + 0x79, b = t + 0x12;
    int s = mui_anim_frame(0x35, bar), sw = mui_sprite_w(s), sh = mui_sprite_h(s);
    int fill = (r - l) * value / 100;
    if (fill <= 0) fill = 1;
    if (fill > sw) fill = sw;
    int scratch = surf == 1 ? 2 : 1;
    mui_blit(scratch, s, 0, 0, 2);
    mui_stretch(scratch, surf, 0, 0, fill, sh, l, t, l + fill, b, 0);
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(0x2f);
    mui_print(surf, l + 5, t - 0x13, "%s", NAME[bar]);
    mui_set_colour(0x1f);
    mui_print(surf, r + 10, t - 2, "%3.0f", (MAX[bar] - MIN[bar]) * value * 0.01f + MIN[bar]);
    mui_print(surf, r + 0x2f, t - 2, "%s", UNIT[bar]);
    mui_set_colour(keep);
}

void show_property_bars(int surf, int x, int y, int bg_anim, const int *perc, int n)
{
    mui_blit(surf, mui_anim_frame(bg_anim, 0), x, y, 2);
    for (int i = 0; i < n; i++) show_property_bar(surf, x, y, i, perc[i]);
}

/* show_part_of_property_bar 0x40cc80: what an item would add, in the glowing anim 0x36 */
void show_part_of_property_bar(int surf, int x, int y, int bar, int base, int add, int frame)
{
    static const int TOP[4] = { 0x18, 0x3b, 0x5e, 0x81 };
    if (bar < 0 || bar > 3) return;
    int l = x + 0xb, t = y + TOP[bar], r = l + 0x79;
    int to = base + add;
    if (to > 100) to = 100;
    if (to < 1) to = 1;
    int s = mui_anim_frame(0x36, frame), sw = mui_sprite_w(s), sh = mui_sprite_h(s);
    int a = (r - l) * base / 100, e = (r - l) * to / 100;
    if (a < 0) a = 0;
    if (a >= sw) a = sw - 1;
    if (e <= a) return;
    if (e < a + 4) e = a + 4;
    if (e < 0) e = 0;
    if (e >= sw) e = sw - 1;
    if (e < a + 4) return;
    int scratch = surf == 1 ? 2 : 1;
    mui_blit(scratch, s, 0, 0, 2);
    mui_stretch(scratch, surf, a, 0, e, sh, l + a, t, l + e, t + sh, 0);
}

/* show_percent_slider 0x40b880: track (anim 0x2b), hazard stripes (anim 0x2a) up to the knob (anim 0x29) */
void show_percent_slider(int surf, const MuiCtrl *c, int value)
{
    int knob = mui_anim_frame(0x29, 0), track = mui_anim_frame(0x2b, 0), stripes = mui_anim_frame(0x2a, 0);
    mui_blit(surf, track, c->x, c->y, 2);
    int kx = (c->w - mui_sprite_w(knob) - 16) * value / 100 + c->x + 8;
    if (kx <= c->x) kx = c->x + 1;
    if (kx - c->x > mui_sprite_w(track)) kx = mui_sprite_w(track) + c->x;
    mui_blit(2, stripes, 0, 0, 2);
    mui_stretch(2, surf, 0, 0, mui_sprite_w(stripes), mui_sprite_h(stripes), c->x + 8, c->y + 5, kx, c->y + c->h - 5, 0);
    mui_blit(surf, knob, kx, c->y + 2, 2);
}

/* drag_percent_slider 0x40b9c0: follows the mouse while a button is held; the sound on *snd follows the value */
int drag_percent_slider(const MuiCtrl *c, int value, int *snd)
{
    int kw = mui_sprite_w(mui_anim_frame(0x29, 0)), last = 0;
    (void)value;
    do {
        int mx;
        mui_mouse(&mx, NULL);
        int v = c->w - kw > 0 ? (int)((mx - c->x) * (100.0 / (c->w - kw))) : 1;
        if (v < 1) v = 1;
        if (v > 100) v = 100;
        if (v != last) {
            begin_draw();
            show_percent_slider(1, c, v);
            end_draw();
            if (snd) snd_set_volume_pct(snd, v);
            last = v;
        }
        mui_pump();
    } while (mui_mouse(NULL, NULL) & 3);
    return last;
}

void begin_draw(void)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
}

void end_draw(void) { mui_present(); }

/* menu_animate_frame 0x403590: the screen to the back buffer, the animations on it, present */
void menu_animate_frame(AnimHeader *h, AnimButtons *b)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    next_animated_header_frame(h);
    handle_animated_buttons(b);
    mui_present();
}

/* ---- Help.cpp: play_help 0x40fcb0 ------------------------------------------------------------------------------- */
/* help keys (Set_help_key in menu_mouse_on): Enter, Space, Esc; the pad's A/B and a click do the same */
static int help_key(void)
{
    return plat_key_pressed(VK_RETURN) || plat_key_pressed(VK_SPACE) || plat_key_pressed(VK_ESCAPE)
        || (plat_pad()->pressed & (PAD_A | PAD_B)) || (g_mclicked & 1);
}

/* each step dims the screen, lights one control (a button redrawn, an area copied back from surface 2, where the
 * screen saved itself first) and speaks its help_NN.wav; a help key ends the tour. The menu music is turned down by
 * the caller. */
void play_help(const HelpTable *t, int surf)
{
    if (!g_cur) return;
    mui_mouse_show(0);
    int keep_focus = g_focus;
    g_focus = -1;
    mui_copy(0, surf, 0, 0, SCR_W, SCR_H);
    int voice = -1, abort = 0;
    for (int k = 0; k < t->n && !abort; k++) {
        mui_dither(0);
        int idx = t->step[k][0];
        if (idx >= 0 && idx < g_cur->n) {
            MuiCtrl *c = &g_cur->c[idx];
            if (c->type == CT_BUTTON) draw_button(c, idx == keep_focus ? BS_PRESSED : BS_NORMAL);   /* 0x41b2e0 */
            else mui_copy(2, 0, c->x, c->y, c->x + c->w, c->y + c->h);                            /* 0x41a9a0 */
        }
        voice = snd_play_plain(t->wav[k]);
        mui_pump();
        while (snd_busy(voice)) {
            if (g_script[0] ? script_event() == EV_ESC : help_key()) { abort = 1; break; }
            mui_pump();
        }
    }
    snd_kill(voice);
    mui_copy(surf, 0, 0, 0, SCR_W, SCR_H);
    g_focus = keep_focus;
    mui_mouse_show(1);
}

/* ---- Smacker: play_smacker 0x409f70 ------------------------------------------------------------------------------- */
int play_smacker(const char *name)
{
    if (!g_movies_on) return 1;
    mui_mouse_show(0);
    mui_fill(1, 0, 0, SCR_W, SCR_H, 0);
    mui_present();
    char file[64];
    snprintf(file, sizeof file, "%s.smk", name);
    int r = film_play(file, g_movie_fullscreen);
    mui_mouse_show(1);
    mui_fill(1, 0, 0, SCR_W, SCR_H, 0);
    mui_present();
    if (r == -2) mui_quit();
    return 1;
}

/* ---- start-up ------------------------------------------------------------------------------------------------------ */
int mui_init(void)
{
    g_mrcs = res_open("Mrcs");
    g_menurc = res_open("MENURC");
    if (!g_mrcs || !g_menurc) { fprintf(stderr, "menu: Mrcs / MENURC missing\n"); return 0; }
    PeFile pe;
    if (!pe_open(&pe, "menu.dat")) { fprintf(stderr, "menu: menu.dat missing\n"); return 0; }
    /* init_anims 0x409e10: the anim table */
    for (int i = 0; i < NANIMS; i++) {
        uint32_t a = 0x433e60 + (uint32_t)i * 12;
        int n = (int)pe_u32(&pe, a);
        uint32_t fr = pe_u32(&pe, a + 8);
        if (n < 0 || n > 48) n = 0;
        g_anim[i].n = n;
        for (int k = 0; k < n; k++) g_anim[i].id[k] = (int)pe_u32(&pe, fr + (uint32_t)k * 8 + 4);
    }
    for (int i = 0; i < F_COUNT; i++) font_load(&g_font[i], FONT_FILE[i]);
    build_forms(&pe);
    pe_close(&pe);
    memset(g_surf, 0, sizeof g_surf);
    mui_load_pcx(2, "PAL.PCX");          /* some palette before the first screen (the intro film draws itself) */
    mui_fill(2, 0, 0, SCR_W, SCR_H, 0);
    g_last_frame = 0;
    plat_show_cursor(0);                 /* the menu's own hand replaces the system pointer */
    return 1;
}

void mui_shutdown(void)
{
    for (int i = 0; i < NSPR; i++) { free(g_spr[i].px); g_spr[i].px = NULL; g_spr[i].tried = 0; }
    for (int i = 0; i < F_COUNT; i++) { free(g_font[i].cells); memset(&g_font[i], 0, sizeof g_font[i]); }
    for (int i = 0; i < g_nsnd; i++) if (g_snd[i].ok) sample_free(&g_snd[i].s);
    g_nsnd = 0;
    gfx_texture_free(g_tex);
    gfx_texture_free(g_cursor_tex);
    g_tex = g_cursor_tex = 0;
    res_close(g_mrcs);
    res_close(g_menurc);
    g_mrcs = g_menurc = NULL;
    g_cur = NULL;
    plat_show_cursor(1);
}
