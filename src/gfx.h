/* gfx.h - the renderer: OpenGL 2.1 on the desktop, OpenGL ES 2.0 on Android, through one small set of calls.
 * The original draws with Direct3D 5 (immediate mode, 640x480x16 or 320x200/640x480x8); here everything is drawn as
 * textured triangles with a vertex colour, plus 2D quads for the overlay in the original's 640x480 screen space.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_GFX_H
#define A2_GFX_H
#include <stdint.h>
#include "vmath.h"

typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} GfxVertex;

enum GfxBlend { BLEND_NONE, BLEND_ALPHA, BLEND_ADD, BLEND_MUL };
enum GfxTexFlags { TEX_CLAMP = 1, TEX_NEAREST = 2, TEX_MIPMAP = 4 };

typedef struct {
    int blend;              /* GfxBlend */
    int alpha_test;         /* discard texels with alpha < 0.5 (colour-keyed textures) */
    int depth_test, depth_write;
    int cull;               /* 0 none, 1 cull counter-clockwise faces (the original's D3DCULL_CCW) */
    int fog;
} GfxState;

/* a static batch of triangles: one vertex/index buffer, drawn in ranges that each have a texture and state */
typedef struct {
    unsigned tex;
    int first, count;       /* index range */
    GfxState state;
} GfxRange;
typedef struct GfxMesh GfxMesh;

int  gfx_init(void);                                  /* after plat_init; 0 = shaders failed */
void gfx_begin(int width, int height, float r, float g, float b);
void gfx_viewport(int x, int y, int w, int h);

unsigned gfx_texture(const uint8_t *rgba, int w, int h, int flags);   /* 0 = failed */
void gfx_texture_update(unsigned tex, const uint8_t *rgba, int w, int h);
void gfx_texture_free(unsigned tex);
unsigned gfx_white(void);                            /* 1x1 white */

void gfx_set_matrices(const Mat4 *proj, const Mat4 *view);
void gfx_set_model(const Mat4 *model);               /* NULL = identity */
void gfx_set_fog(float r, float g, float b, float start, float end);
void gfx_set_tint(float r, float g, float b, float a);

GfxMesh *gfx_mesh_create(const GfxVertex *v, int nv, const uint16_t *idx, int ni, const GfxRange *ranges, int nr);
GfxMesh *gfx_mesh_create32(const GfxVertex *v, int nv, const uint32_t *idx, int ni, const GfxRange *ranges, int nr);
void gfx_mesh_draw(const GfxMesh *m);
void gfx_mesh_draw_range(const GfxMesh *m, int range);
/* part of a range: `count` indices from index `first` (absolute), with that range's texture and state */
void gfx_mesh_draw_sub(const GfxMesh *m, int range, int first, int count);
void gfx_mesh_free(GfxMesh *m);

/* immediate triangles (skid marks, particles, the sky): drawn at once with the given texture and state */
void gfx_draw_tris(const GfxVertex *v, int nv, unsigned tex, const GfxState *st);

/* 2D in a virtual screen of vw x vh (the original's 640x480), letterboxed into the window keeping 4:3 */
void gfx_2d_begin(int vw, int vh);
void gfx_2d_quad(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 uint32_t rgba, int blend);
void gfx_2d_rect(float x, float y, float w, float h, uint32_t rgba);
void gfx_2d_end(void);
void gfx_2d_to_screen(float vx, float vy, float *sx, float *sy);   /* virtual -> window pixel */
void gfx_screen_to_2d(int sx, int sy, float *vx, float *vy);       /* window pixel -> virtual */

GfxState gfx_state_opaque(void);
void gfx_read_pixels(int w, int h, uint8_t *rgb);                  /* the back buffer, bottom-up rows */
#endif
