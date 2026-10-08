/* lights.c - the object lights (gfx_lght.cpp; docs/RENDER.md "Object lights / brake lights").
 *
 * The original keeps 20 light records (g_light 0x4aa860, 0x10 bytes: +0 used, +4 on, +8 object, +0xc 3D instance);
 * light_init 0x41b330 gives each its own instance (+0x95 = 2: no automatic matrix). light_create 0x41b5a0 sets the
 * instance's anim, frame, position and rotation (relative to the object), builds its matrix and recolours the mesh
 * (light_set_vertex_alpha 0x41b400). The only lights of the game are the player car's two brake lights
 * (create_brake_lights 0x425500, called by init_player_car; the AI, traffic and police cars get none). handle_player_car
 * switches them with light_set_on 0x41b560 when g_plr_brake_light changes; light_draw 0x41b720 draws the lit ones in
 * render_game after the objects, blended, with the light's matrix times the object's.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "lights.h"
#include "exetab.h"
#include "gfx.h"
#include "ini.h"
#include "model.h"
#include "res.h"
#include "screen.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIGHTS 20                       /* 0x4aa860 .. 0x4aa9a0 */
#define BRAKE_POS 0x485c28u             /* float[7][2][3]: per car type the left and right brake light */

typedef struct {
    int used, on, obj;                  /* +0 / +4 / +8 */
    int anim, frame;                    /* the instance (+0xc): anim +4, frame +8 */
    Vec3 pos;                           /* instance +0x50, in the object's space */
    float pitch, yaw, roll;             /* instance +0x74 / +0x78 / +0x7c, ANGLE_UNITS */
    Mat4 body;                          /* PORT: the object's matrix as drawn this frame */
    int attached;
} Light;

static Light L[LIGHTS];
static int g_on;                        /* g_opt_object_lights 0x4d9b0c (cleared by light_init when not usable) */
static int g_brake[2] = { -1, -1 };     /* the two brake lights 0x48b458 / 0x48b45c */
static int g_brake_lit;                 /* 0x4d9e10: the lights are switched on */
/* the OBJ_LIGHT01_ANIM mesh (s_licht.d3d + s_sfx): one mesh shared by all lights, so the last light_create's colour
 * is every light's colour */
static GfxVertex *g_v;
static uint16_t *g_idx;
static int g_nv, g_ni, g_anim = -1;
static unsigned g_tex;
static float g_pos[7][2][3];
static int g_loaded;
static int g_tex_bit8 = -1;         /* the mode g_tex was loaded in */

/* light_set_vertex_alpha 0x41b400: a radial glow, alpha A at the centre (the origin) to 0 at the farthest vertex,
 * RGB = the light's colour, specular 0. 'Too many vertices in this mesh to make it a light object' above 1000. */
static void light_set_vertex_alpha(uint32_t argb)
{
    float d[1000], max = 0;
    int n = g_nv < 1000 ? g_nv : 1000;
    for (int i = 0; i < n; i++) {
        d[i] = sqrtf(g_v[i].x * g_v[i].x + g_v[i].y * g_v[i].y + g_v[i].z * g_v[i].z);
        if (d[i] > max) max = d[i];
    }
    int a = (int)(argb >> 24);
    for (int i = 0; i < n; i++) {
        int k = max > 0 ? (int)(d[i] / max * (float)a) : 0;       /* _ftol */
        g_v[i].a = (uint8_t)(a - k);
        g_v[i].r = (uint8_t)(argb >> 16);
        g_v[i].g = (uint8_t)(argb >> 8);
        g_v[i].b = (uint8_t)argb;
    }
}

/* light_create 0x41b5a0: -1 when object lights are off or all 20 are used */
static int light_create(int obj, int anim, int frame, Vec3 pos, Vec3 rot, uint32_t argb)
{
    if (!g_on) return -1;
    int i = 0;
    while (i < LIGHTS && L[i].used) i++;
    if (i == LIGHTS) return -1;
    Light *l = &L[i];
    memset(l, 0, sizeof *l);
    l->anim = anim;
    l->frame = frame;
    l->pitch = rot.x; l->yaw = rot.y; l->roll = rot.z;
    l->pos = pos;
    light_set_vertex_alpha(argb);
    l->obj = obj;
    l->used = 1;
    l->on = 0;
    return i;
}

static void light_set_on(int i, int on)                             /* 0x41b560 */
{
    if (i < 0 || i >= LIGHTS) { fprintf(stderr, "Illegal index for object light! index: %d, on_off:%d\n", i, on); return; }
    L[i].on = on;
}

void lights_init(const Race *r)
{
    memset(L, 0, sizeof L);
    g_brake[0] = g_brake[1] = -1;
    g_brake_lit = 0;
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    g_on = ini_int(g, "GFX OPTIONS", "object_lights", 1);
    /* light_init 0x41b330: needs g_hw_accel (not in the 8-bit modes, screen.h), the SRCALPHA / INVSRCALPHA blend caps
     * and alpha_blending, else "Object lights disabled: Hardware doesn't support used alpha modes" and
     * object_lights = 0 (the brake lights are then texture copies, txcopy.c) */
    if (g_on && (!ini_int(g, "GFX OPTIONS", "alpha_blending", 1) || !scr_hw3d())) g_on = 0;
    ini_free(g);
    if (!g_loaded) {
        g_loaded = 1;
        g_anim = anim_find("OBJ_LIGHT01_ANIM");
        if (!anim_mesh_raw(g_anim, 0, &g_v, &g_nv, &g_idx, &g_ni, &g_tex, NULL)) g_anim = -1;
        g_tex_bit8 = tex_8bit();
        PeFile pe;
        if (pe_open(&pe, "spel.dat")) {
            const uint8_t *q = pe_ptr(&pe, BRAKE_POS, sizeof g_pos);
            if (q) memcpy(g_pos, q, sizeof g_pos);
            else g_anim = -1;
            pe_close(&pe);
        } else g_anim = -1;
    }
    if (g_anim >= 0 && g_tex_bit8 != tex_8bit()) {   /* the glow's texture in the mode of this race (texture.h) */
        GfxVertex *v;
        uint16_t *ix;
        int nv, ni;
        if (anim_mesh_raw(g_anim, 0, &v, &nv, &ix, &ni, &g_tex, NULL)) { free(v); free(ix); }
        g_tex_bit8 = tex_8bit();
    }
    if (g_anim < 0) g_on = 0;
    /* create_brake_lights 0x425500: the colour and pitch per car type are stack constants of the function */
    static const uint32_t ARGB[7] = { 0xff960000u, 0xff960000u, 0xffff0000u, 0xffff0000u, 0xffff0000u, 0xa4ff0000u,
                                      0xa4ff0000u };
    static const int PITCH[7] = { 0, 0, -500, 0, 0, 0, -180 };
    int t = r->plr_car;                                             /* g_plr_car 0x4d9e2c */
    if (t < 0 || t > 6 || r->plr_obj < 0) return;
    for (int k = 0; k < 2; k++)
        g_brake[k] = light_create(r->plr_obj, g_anim, 0, v3(g_pos[t][k][0], g_pos[t][k][1], g_pos[t][k][2]),
                                  v3((float)PITCH[t], 0, 0), ARGB[t]);
}

int lights_on(void) { return g_on && g_anim >= 0; }

/* handle_player_car 0x425f0b: on while g_plr_brake_light == 1 (or g_plr_gas_hold == 1, which is never set); only the
 * changes are passed on. Without object lights the original copies the brake-light texture instead
 * (set_brake_light_texture 0x42b100, Game.ini texturecopy; txcopy_brake_lights). */
void lights_tick(const Race *r)
{
    if (r->brake_light == 1) {
        if (g_brake_lit) return;
        g_brake_lit = 1;
    } else {
        if (!g_brake_lit) return;
        g_brake_lit = 0;
    }
    if (g_on) {
        light_set_on(g_brake[0], g_brake_lit);
        light_set_on(g_brake[1], g_brake_lit);
    }
}

void lights_attach(int o, const Mat4 *body)
{
    for (int i = 0; i < LIGHTS; i++)
        if (L[i].used && L[i].obj == o) { L[i].body = *body; L[i].attached = 1; }
}

void lights_draw(void)                                              /* light_draw 0x41b720 */
{
    if (!g_on) return;
    const float u2r = 6.28319f / ANGLE_UNITS;
    /* ALPHABLENDENABLE 1, SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA; z test and write stay on, colour key, culling */
    GfxState st = { BLEND_ALPHA, 2, 1, 1, 1, 0 };
    GfxVertex t[3 * 64];
    int n = g_ni < 3 * 64 ? g_ni : 3 * 64;
    for (int k = 0; k < n; k++) t[k] = g_v[g_idx[k]];
    for (int i = 0; i < LIGHTS; i++) {
        Light *l = &L[i];
        if (!l->used || l->on != 1) continue;
        /* PORT: the original draws a light even when its object is not drawn (the cockpit view, where the brake
         * lights are behind the camera); here only with the matrix of this frame */
        if (!l->attached) continue;
        l->attached = 0;
        /* inst_build_matrix 0x459360: Rz(roll) Rx(pitch) Ry(yaw), then the position, for row vectors (whole angle
         * units; zero angles skipped); here column vectors T Ry Rx Rz with m4_rot_*(-angle) as in wheels.c.
         * mat_mul 0x45e450: the light's matrix times the object's */
        Mat4 local = m4_translate(l->pos);
        if ((int)l->yaw) local = m4_mul(local, m4_rot_y(-(float)(int)l->yaw * u2r));
        if ((int)l->pitch) local = m4_mul(local, m4_rot_x(-(float)(int)l->pitch * u2r));
        if ((int)l->roll) local = m4_mul(local, m4_rot_z(-(float)(int)l->roll * u2r));
        Mat4 w = m4_mul(l->body, local);
        gfx_set_model(&w);
        gfx_draw_tris(t, n, g_tex, &st);
    }
    for (int i = 0; i < LIGHTS; i++) L[i].attached = 0;
}
