/* radar.c - see radar.h. By g_res_index: 320x200 LORADAR (64 x 64) and rect row 0, 640x480 HIRADAR (128 x 128) and
 * rect row 1; by g_8bit: the dots of anim 43 (5 x 5) or 44 (8 x 8) and the map's edge 4 x 4 or 7 x 7 (screen.h).
 *
 * The map surface holds palette indices like the original's: 0xfe empty, 13 road, 0 road edge. The original fills
 * the surface and the texture with pformat_convert(0xfe), the colour key of the textures (the radar images on the CD
 * are nothing but key colour), so all but the road and the dots is transparent; road and edge are colours 13 and 0
 * of the screen's palette: e02_pl16 (light grey, black), in the 8-bit modes the etappe's.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "radar.h"
#include "ai.h"
#include "gfx.h"
#include "hud.h"
#include "ini.h"
#include "res.h"
#include "screen.h"
#include "txcopy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* g_res_index (radar_init 0x41cf20): 0 LORADAR.PCX 64 x 64, 1 HIRADAR 128 x 128 (the size table of radar_blit_dot
 * 0x41c880); the rect table 0x489040 by g_res_index: the window into the map and the quad's corner */
static int g_tsz = 128;
#define TEX g_tsz
static const int RECTS[2][4] = { { 15, 146, 79, 210 }, { 30, 361, 158, 489 } };
static const int *RECT = RECTS[1];
static int g_8bit;                                  /* g_8bit 0x4d9a70: the map's 8-bit branch, the 8-bit dots */
enum { MAP_EMPTY = 0xfe, MAP_ROAD = 13, MAP_EDGE = 0 };

static int g_init;                                  /* 0x4acab0 */
static int g_overlay_off;                           /* Game.ini [GFX OPTIONS] disable_overlay (0x4d9afc) */
static int g_bbox[4];                               /* 0x4acab8: min x, min z, max x, max z */
static uint8_t *g_map;                              /* 0x4ac994, g_map_w 0x4ac990 x g_map_h 0x4aca28 */
static int g_map_w, g_map_h;
static uint8_t g_rgba[128 * 128 * 4];               /* the radar texture's surface */
static unsigned g_tex;
static uint8_t *g_dot[3];                           /* frames 0 racer, 1 player, 2 cop */
static int g_dot_w[3], g_dot_h[3];
static uint8_t g_col_road[4], g_col_edge[4];
static float g_quad[4][4];                          /* 0x4aca30: the unrotated quad {x, y, u, v} */
static float g_cx, g_cy;                            /* 0x4ac998 / 0x4ac9a0: its centre */

static int ftol(float f) { return (int)f; }         /* MSVC _ftol (sub_4700e0): truncation toward zero */

/* radar_init_quad 0x41c5a0: corner rect.xy, size 0.7 x the rect's (89.6 x 89.6), white, uv 0..1, a triangle fan */
static void init_quad(void)
{
    float x0 = (float)RECT[0], y0 = (float)RECT[1];
    float w = (float)(RECT[2] - RECT[0]) * 0.7f, h = (float)(RECT[3] - RECT[1]) * 0.7f;    /* 0x47f6b0 */
    const float q[4][4] = { { x0, y0, 0, 0 }, { x0 + w, y0, 1, 0 }, { x0 + w, y0 + h, 1, 1 }, { x0, y0 + h, 0, 1 } };
    memcpy(g_quad, q, sizeof q);
    g_cx = x0 + w * 0.5f;
    g_cy = y0 + h * 0.5f;
}

/* radar_get_road_bbox 0x41cc90: the chunks' start points, x and z apart; a chunk lowers the minimum or else raises
 * the maximum; then 0xf00 (64 map pixels) of margin around */
static void road_bbox(const Track *t)
{
    int *b = g_bbox;
    b[0] = b[1] = 0x7fff;
    b[2] = b[3] = -0x7fff;
    for (int i = 0; i < t->nchunks; i++) {
        float x = t->chunks[i].pos.x, z = t->chunks[i].pos.z;
        if ((float)b[0] > x) b[0] = ftol(x);
        else if ((float)b[2] < x) b[2] = ftol(x);
        if ((float)b[1] > z) b[1] = ftol(z);
        else if ((float)b[3] < z) b[3] = ftol(z);
    }
    b[0] -= 0xf00; b[1] -= 0xf00;
    b[2] += 0xf00; b[3] += 0xf00;
}

/* radar_chunk_to_map 0x41c790 (radar_draw_road_map 0x41c9a0 inlines it): 60 world units per pixel, north (+z) up */
static void chunk_to_map(const Track *t, int c, int *mx, int *my)
{
    if (c < 0 || c >= t->nchunks) c = 0;            /* "Illegal chunk drawing radar!" */
    const Vec3 *p = &t->chunks[c].pos;
    int x = (ftol(p->x) - g_bbox[0]) / 60, y = (g_bbox[3] - ftol(p->z)) / 60;
    *mx = x < 0 ? 0 : x >= g_map_w ? g_map_w - 1 : x;
    *my = y < 0 ? 0 : y >= g_map_h ? g_map_h - 1 : y;
}

static void map_set(int x, int y, uint8_t v)
{
    if (x >= 0 && y >= 0 && x < g_map_w && y < g_map_h) g_map[(size_t)y * g_map_w + x] = v;
}

/* radar_draw_road_map 0x41c9a0: per chunk the road pixel, an edge around it where there is no road yet (16-bit branch
 * 0x41cb79: 7x7 at -3..3; 8-bit branch 0x41cb06: 4x4 at -2..1), then a 3x3 road block */
static void draw_road_map(const Track *t)
{
    int e0 = g_8bit ? -2 : -3, e1 = g_8bit ? 2 : 4;
    for (int c = 0; c < t->nchunks; c++) {
        int x, y;
        chunk_to_map(t, c, &x, &y);
        map_set(x, y, MAP_ROAD);
        for (int dx = e0; dx < e1; dx++)
            for (int dy = e0; dy < e1; dy++) {
                int xx = x + dx, yy = y + dy;
                if (xx >= 0 && yy >= 0 && xx < g_map_w && yy < g_map_h && g_map[(size_t)yy * g_map_w + xx] != MAP_ROAD)
                    g_map[(size_t)yy * g_map_w + xx] = MAP_EDGE;
            }
        for (int dx = -1; dx < 2; dx++)
            for (int dy = -1; dy < 2; dy++) map_set(x + dx, y + dy, MAP_ROAD);
    }
}

void radar_free(void)
{
    free(g_map);
    g_map = NULL;
    for (int i = 0; i < 3; i++) { free(g_dot[i]); g_dot[i] = NULL; }
    gfx_texture_free(g_tex);
    g_tex = 0;
    g_init = 0;
}

void radar_init(const Race *r)
{
    radar_free();
    if (!txcopy_enabled()) return;                  /* init_game 0x424252: only with g_radar_enabled */
    g_8bit = scr_8bit();
    g_tsz = scr_res_index() ? 128 : 64;
    RECT = RECTS[scr_res_index() ? 1 : 0];
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    g_overlay_off = ini_int(g, "GFX OPTIONS", "disable_overlay", 0);
    ini_free(g);
    init_quad();
    /* create_radar 0x41cda0 */
    road_bbox(&r->track);
    g_map_w = (g_bbox[2] - g_bbox[0]) / 60;
    g_map_h = (g_bbox[3] - g_bbox[1]) / 60;
    if (g_map_w <= 0 || g_map_h <= 0) return;       /* "Illegal value for width/height radar!" */
    g_map = malloc((size_t)g_map_w * g_map_h);
    if (!g_map) return;
    memset(g_map, MAP_EMPTY, (size_t)g_map_w * g_map_h);     /* clear_radar: the map and the texture to 0xfe */
    memset(g_rgba, 0, sizeof g_rgba);
    hud_pal_rgba(MAP_ROAD, g_col_road);
    hud_pal_rgba(MAP_EDGE, g_col_edge);
    draw_road_map(&r->track);
    /* the dots: the anim of layout item 15 (radar_blit_dot 0x41c8be: 43 in row 0, 44 in row 1) */
    for (int i = 0; i < 3; i++) g_dot[i] = hud_sprite_rgba(hud_item_anim(15), i, &g_dot_w[i], &g_dot_h[i]);
    g_tex = gfx_texture(g_rgba, TEX, TEX, TEX_CLAMP | (g_8bit ? TEX_NEAREST : 0));
    g_init = g_tex != 0;
}

/* radar_calc_window 0x41cf90: the rect-sized window centred on (mx, my), pushed inside the map (exclusive corner
 * x1 = x0 + 127 inside; at the right / bottom edge x0 = x1 - 128) */
static void calc_window(int mx, int my, int *win)
{
    int w = RECT[2] - RECT[0], h = RECT[3] - RECT[1];
    win[0] = mx - w / 2;
    win[1] = my - h / 2;
    if (win[0] < 0) { win[0] = 0; win[2] = w - 1; }
    else if (win[0] + w >= g_map_w) { win[2] = g_map_w - 1; win[0] = win[2] - h; }   /* sic: h */
    else win[2] = win[0] + w - 1;
    if (win[1] < 0) { win[1] = 0; win[3] = h - 1; }
    else if (win[1] + h >= g_map_h) { win[3] = g_map_h - 1; win[1] = win[3] - h; }
    else win[3] = win[1] + h - 1;
}

/* radar_blit_dot 0x41c880: the dot sprite with its centre (rounded up) at (x, y) of the texture, colour-keyed, only
 * when it stays 4 pixels inside */
static void blit_dot(int x, int y, int frame)
{
    const uint8_t *s = g_dot[frame];
    int w = g_dot_w[frame], h = g_dot_h[frame];
    if (!s) return;
    int dx = x - ((w >> 1) + (w & 1)) + 1, dy = y - ((h >> 1) + (h & 1)) + 1;
    if (dx < 4 || dy < 4 || dx + w > TEX - 4 || dy + h > TEX - 4) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *q = s + ((size_t)j * w + i) * 4;
            if (q[3]) memcpy(g_rgba + ((size_t)(dy + j) * TEX + dx + i) * 4, q, 4);
        }
}

static int map_chunk_of(const Race *r, int o, int *mx, int *my)
{
    if (o < 0 || o >= MAX_OBJS || !r->obj[o].used) return 0;
    chunk_to_map(&r->track, r->obj[o].chunk, mx, my);
    return 1;
}

/* radar_update_texture 0x41d040 */
static void update_texture(const Race *r)
{
    int px, py, rx[3], ry[3], rok[3], cx[20], cy[20], nc = 0, win[4];
    chunk_to_map(&r->track, r->obj[r->plr_obj].chunk, &px, &py);    /* g_player_chunk 0x4d9e44 */
    for (int i = 0; i < 3; i++) rok[i] = map_chunk_of(r, ai_racer_object(i), &rx[i], &ry[i]);   /* 0x485b2c.. */
    for (int k = 0; k < 20; k++)                                      /* the active-cop list 0x4a5ce0 */
        if (ai_active_cop(k) != -1 && map_chunk_of(r, ai_active_cop(k), &cx[nc], &cy[nc])) nc++;
    calc_window(px, py, win);
    /* blt_surf_2_surf 0x4697c0 (no key): the window (exclusive corner) stretched to (2,2)-(126,126) of the texture,
     * clipped to the map */
    int x0 = win[0] < 0 ? 0 : win[0], y0 = win[1] < 0 ? 0 : win[1];
    int x1 = win[2] > g_map_w ? g_map_w : win[2], y1 = win[3] > g_map_h ? g_map_h : win[3];
    if (x1 > x0 && y1 > y0) {
        const int D = TEX - 4;
        for (int j = 0; j < D; j++) {
            const uint8_t *row = g_map + (size_t)(y0 + j * (y1 - y0) / D) * g_map_w;
            uint8_t *o = g_rgba + ((size_t)(2 + j) * TEX + 2) * 4;
            for (int i = 0; i < D; i++, o += 4) {
                uint8_t v = row[x0 + i * (x1 - x0) / D];
                if (v == MAP_ROAD) memcpy(o, g_col_road, 4);
                else if (v == MAP_EDGE) memcpy(o, g_col_edge, 4);
                else memset(o, 0, 4);
            }
        }
    }
    /* the dots at their map position relative to the window (without the 2-pixel offset and the stretch of the map):
     * racers, cops, the player last */
    for (int i = 0; i < 3; i++) if (rok[i]) blit_dot(rx[i] - win[0], ry[i] - win[1], 0);
    for (int k = 0; k < nc; k++) blit_dot(cx[k] - win[0], cy[k] - win[1], 2);
    blit_dot(px - win[0], py - win[1], 1);
    gfx_texture_update(g_tex, g_rgba, TEX, TEX);                     /* texture_reload 0x45bae0 */
}

void radar_draw(const Race *r, Vec3 view)
{
    if (!g_init || r->event_running || !txcopy_enabled() || g_overlay_off || r->detail <= 0) return;
    update_texture(r);
    /* radar_rotate_quad 0x41d280: v = normalize(-dx, 0, dz) of the camera's view; each corner turns around the centre:
     * x' = x*v.z - y*v.x, y' = x*v.x + y*v.z, so the direction the camera looks in points up */
    float vx = -view.x, vz = view.z, l = sqrtf(vx * vx + vz * vz);
    if (l > 1e-6f) { vx /= l; vz /= l; } else { vx = 0; vz = 1; }
    GfxVertex v[6];
    static const int FAN[6] = { 0, 1, 2, 0, 2, 3 };                  /* D3DPT_TRIANGLEFAN of 4 TL vertices */
    scr_2d_begin();                                  /* the TL vertices are in screen coordinates */
    for (int i = 0; i < 6; i++) {
        const float *q = g_quad[FAN[i]];
        float x = q[0] - g_cx, y = q[1] - g_cy, sx, sy;
        gfx_2d_to_screen(x * vz - y * vx + g_cx, x * vx + y * vz + g_cy, &sx, &sy);
        GfxVertex t = { sx, sy, 0, q[2], q[3], 255, 255, 255, 255 };
        v[i] = t;
    }
    GfxState st = { BLEND_ALPHA, 0, 0, 0, 0, 0 };    /* the colour-keyed texture: transparent around the road */
    gfx_draw_tris(v, 6, g_tex, &st);
    gfx_2d_end();
}
