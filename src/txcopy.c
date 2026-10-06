/* txcopy.c - see txcopy.h.
 *
 * The original blits the sprite surface straight into the DirectDraw surface of the texture and re-uploads it
 * (texture_reload 0x45bae0). Here each texture that gets copies keeps its pixels as tex_get uploaded them; a copy
 * changes them and replaces the GL texture's contents (mipmaps rebuilt). The copies' coordinates are those of the CD's
 * texture images; a texture pack's image of another size (mods/textures) gets them scaled to its size.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "txcopy.h"
#include "exetab.h"
#include "gfx.h"
#include "hud.h"
#include "image.h"
#include "ini.h"
#include "lights.h"
#include "plat.h"
#include "res.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int anim8, anim16, x, y, type; char tex[64]; } CopyDef;   /* 0x48c968, 0x58 bytes */
typedef struct { char name[32]; unsigned id; int w, h, ow, oh; uint8_t *rgba; } Target;

static CopyDef g_def[TXC_COUNT];
static int g_def_loaded;
static Target g_tgt[TXC_COUNT];
static int g_ntgt;
static int g_of[TXC_COUNT];        /* the copy's texture (0x48c9bc, resolved by sub_42b060), -1 = not on this track */
static int g_last[TXC_COUNT];      /* the frame the copy last put there, -1 = none yet */
static int g_enabled;              /* g_radar_enabled 0x4d9aa4 */
static int g_brake_on;             /* 0x4d9e10: the brake lights as last set */
static int g_test_brake;           /* A2_TEST_BRAKE=1 (README): the brake lights stay on */

static void load_defs(void)
{
    PeFile pe;
    if (g_def_loaded || !pe_open(&pe, "spel.dat")) return;
    for (int i = 0; i < TXC_COUNT; i++) {
        uint32_t a = 0x48c968 + (uint32_t)i * 0x58;
        CopyDef *d = &g_def[i];
        d->anim8 = (int)pe_u32(&pe, a);
        d->anim16 = (int)pe_u32(&pe, a + 4);
        d->x = (int)pe_u32(&pe, a + 8);
        d->y = (int)pe_u32(&pe, a + 0xc);
        const char *n = pe_str(&pe, a + 0x10);
        snprintf(d->tex, sizeof d->tex, "%s", n ? n : "");
        d->type = (int)pe_u32(&pe, a + 0x50);          /* track type 0 highway, 1 city, 2 both */
    }
    pe_close(&pe);
    g_def_loaded = 1;
}

/* the texture as on the CD (or as the texture pack has it): a copy of the uploaded pixels, put back into the GL
 * texture, since the copies of an earlier race changed it (the original loads its textures anew for every race) */
static int target(const char *name)
{
    for (int i = 0; i < g_ntgt; i++)
        if (!_stricmp(g_tgt[i].name, name)) return i;
    const Texture *t = tex_get(name);
    Image img;
    if (!t || g_ntgt >= TXC_COUNT || !tex_pixels(name, &img)) return -1;
    if (img.w != t->w || img.h != t->h) { image_free(&img); return -1; }
    Target *g = &g_tgt[g_ntgt];
    memset(g, 0, sizeof *g);
    snprintf(g->name, sizeof g->name, "%s", name);
    g->id = t->id;
    g->w = img.w;
    g->h = img.h;
    if (!tex_orig_size(name, &g->ow, &g->oh)) { g->ow = img.w; g->oh = img.h; }
    g->rgba = malloc((size_t)img.w * img.h * 4);
    if (!g->rgba) { image_free(&img); return -1; }
    memcpy(g->rgba, img.rgba, (size_t)img.w * img.h * 4);
    image_free(&img);
    gfx_texture_update(g->id, g->rgba, g->w, g->h);
    return g_ntgt++;
}

void txcopy_free(void)
{
    for (int i = 0; i < g_ntgt; i++) free(g_tgt[i].rgba);
    memset(g_tgt, 0, sizeof g_tgt);
    g_ntgt = 0;
}

void txcopy_init(const Race *r)
{
    txcopy_free();
    load_defs();
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    g_enabled = ini_int(g, "OPTIONS", "texturecopy", 1);     /* 0x421d41 */
    ini_free(g);
    /* check_texture_copy 0x424032: a test blit into HIRADAR.PCX (the radar texture of the 640x480 modes); when it
     * fails, texture copying and the radar are off */
    if (g_enabled && !tex_get("HIRADAR")) g_enabled = 0;
    g_brake_on = 0;
    g_test_brake = getenv("A2_TEST_BRAKE") && atoi(getenv("A2_TEST_BRAKE"));     /* tests: lit as when braking */
    /* sub_42b060: the textures of the copies of this track type (Raser.ini [MAPS] arg 0) or of both (2) */
    int type = r->track.city ? 1 : 0;
    for (int i = 0; i < TXC_COUNT; i++) {
        g_of[i] = g_last[i] = -1;
        if (g_def_loaded && g_def[i].tex[0] && (g_def[i].type == type || g_def[i].type == 2)) g_of[i] = target(g_def[i].tex);
    }
}

int txcopy_enabled(void) { return g_enabled; }

void txcopy_blit(int index, int frame)
{
    if (!g_enabled || index < 0 || index >= TXC_COUNT || g_of[index] < 0) return;
    /* the same frame again (the AI switches sirens off every tick): the copies do not overlap, so it is there */
    if (g_last[index] == frame) return;
    g_last[index] = frame;
    const CopyDef *d = &g_def[index];
    Target *t = &g_tgt[g_of[index]];
    int sw = 0, sh = 0;
    /* the 16-bit set of the anim (g_8bit 0); a frame outside the anim is a fatal error in the original (0x42af59) */
    uint8_t *s = hud_sprite_rgba(d->anim16, frame, &sw, &sh);
    if (!s) return;
    /* blt_surf_2_surf 0x4697c0 gets the exclusive rectangles (0,0)-(w-1,h-1) -> (x,y)-(x+w-1,y+h-1): the sprite's last
     * row and column are not copied; a rectangle outside the texture is a fatal error (0x42afe0) */
    int x0 = d->x, y0 = d->y, x1 = d->x + sw - 1, y1 = d->y + sh - 1;
    if (x0 >= 0 && y0 >= 0 && x1 < t->ow && y1 < t->oh && sw > 1 && sh > 1) {
        int X0 = x0 * t->w / t->ow, X1 = x1 * t->w / t->ow, Y0 = y0 * t->h / t->oh, Y1 = y1 * t->h / t->oh;
        for (int Y = Y0; Y < Y1; Y++)
            for (int X = X0; X < X1; X++) {
                int sx = (X - X0) * (sw - 1) / (X1 - X0), sy = (Y - Y0) * (sh - 1) / (Y1 - Y0);
                memcpy(t->rgba + ((size_t)Y * t->w + X) * 4, s + ((size_t)sy * sw + sx) * 4, 4);
            }
        gfx_texture_update(t->id, t->rgba, t->w, t->h);          /* texture_reload 0x45bae0 */
    }
    free(s);
}

/* set_brake_light_texture 0x42b100 (txt_reml.cpp): car 0..6 uses copy 0..6 (table at esp+4), frame 2*car when on,
 * 2*car + 1 when off */
static void set_brake_light_texture(int car, int on)
{
    if (car < 0 || car >= 7) return;                         /* "Illegal car!" */
    txcopy_blit(TXC_BRAKE + car, on ? 2 * car : 2 * car + 1);
}

void txcopy_brake_lights(const Race *r)
{
    /* 0x425f0b: on while g_plr_brake_light (or the dead g_plr_gas_hold) is 1, switched only when it changes. With
     * hardware acceleration and Game.ini object_lights=1 (the CD's setting) the original switches the two glow meshes
     * of gfx_lght.cpp (light_set_on 0x41b560, lights.c) instead; this is its object_lights=0 path. */
    if (lights_on()) return;
    int on = r->brake_light == 1 || g_test_brake;
    if (on == g_brake_on) return;
    g_brake_on = on;
    set_brake_light_texture(r->plr_car, on);
}

/* set_speed_sign_texture 0x42b190 (txt_vbrd.cpp): zone 0..4 through the jump table 0x42b200 to frames 2 ("50"),
 * 3 ("70"), 4 ("100"), 1 (a cross: no limit) and 0 (dark) of the matrix sign */
static void set_speed_sign_texture(int zone)
{
    static const int FRAME[5] = { 2, 3, 4, 1, 0 };
    if (!g_enabled || zone < 0 || zone > 4) return;          /* "Unknown speed zone" */
    txcopy_blit(TXC_SPEED, FRAME[zone]);
}

void txcopy_trigger(const Race *r, int id)
{
    int highway = !r->track.city;                            /* [0x4d8e9c + etappe*0x88] == 0 */
    switch (id) {
    case 4: case 5: case 6:                                  /* trigger_speed_limit_50/70/100 0x42a340.. */
        if (highway) set_speed_sign_texture(id - 4);
        break;
    case 8: set_speed_sign_texture(3); break;                /* trigger_speed_limit_120 0x42a3d0: any track */
    default:
        /* tr_vbord.cpp 0x42a640..0x42adb0: trigger 22 + f -> frame f of the sign at (9,122), 31 + f -> the sign at
         * (9,7), with the copy of etappe 1, 3 or 5 ("Trigger is only used for highway tracks!" on the others) */
        if (id >= 22 && id <= 39) {
            int e = r->etappe, k = e == 1 ? 0 : e == 3 ? 1 : e == 5 ? 2 : -1;
            if (k >= 0) txcopy_blit((id <= 30 ? TXC_SIGN_A : TXC_SIGN_B) + k, id <= 30 ? id - 22 : id - 31);
        }
        break;
    }
}
