/* gfx_gl.c - gfx.h on OpenGL 2.1 / OpenGL ES 2.0. All GL entry points are loaded at run time through plat_gl_proc,
 * so no GL headers or import libraries are needed beyond the system's GL library (opengl32 / libGL / libGLESv2).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "gfx.h"
#include "plat.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define GLAPI __stdcall
#else
#define GLAPI
#endif
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean, GLubyte;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr, GLintptr;

#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR 0x0306
#define GL_ONE 1
#define GL_ZERO 0
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_CW 0x0900
#define GL_LEQUAL 0x0203
#define GL_TEXTURE_2D 0x0DE1
#define GL_EXTENSIONS 0x1F03
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_SCISSOR_TEST 0x0C11

#define GLFUNCS \
    X(void, Clear, (GLbitfield)) X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Enable, (GLenum)) X(void, Disable, (GLenum)) X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, DepthMask, (GLboolean)) X(void, DepthFunc, (GLenum)) X(void, CullFace, (GLenum)) X(void, FrontFace, (GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, GenTextures, (GLsizei, GLuint *)) X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat)) X(const GLubyte *, GetString, (GLenum)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, PixelStorei, (GLenum, GLint)) X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, GenerateMipmap, (GLenum)) X(void, ActiveTexture, (GLenum)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(GLuint, CreateShader, (GLenum)) X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) X(void, LinkProgram, (GLuint)) X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) X(void, UseProgram, (GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) X(void, Uniform1f, (GLint, GLfloat)) X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void, EnableVertexAttribArray, (GLuint)) X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))

#define X(ret, name, args) static ret (GLAPI *gl##name) args;
GLFUNCS
#undef X

struct GfxMesh {
    GLuint vbo, ibo;
    GLenum itype;
    int isize;
    int nr;
    GfxRange *ranges;
};

static GLuint g_prog, g_white, g_stream_vbo;
static GLint u_mvp, u_tex, u_alpha_test, u_fog, u_fogcol, u_fogrange, u_tint, u_mv, u_decal;
static Mat4 g_proj, g_view, g_model;
static int g_w, g_h, g_has_mipmap;
static float g_aniso;           /* anisotropic filtering of mipmapped textures (GL_EXT_texture_filter_anisotropic), 0 = none */
static float g_fog[5];
static float g_tint[4] = { 1, 1, 1, 1 };
static int g_depth_off;         /* gfx_set_depth_off */
static int g_fog_on, g_decal;   /* gfx_set_fog_on / gfx_set_decal, until the next gfx_begin */
static int g_2d;                /* from gfx_2d_begin to the next gfx_set_matrices: no fog, no decal */
/* 2D */
static int g_vw = 640, g_vh = 480;
static float g_ox, g_oy, g_scale;

static const char *VS =
    "attribute vec3 a_pos; attribute vec2 a_uv; attribute vec4 a_col;\n"
    "uniform mat4 u_mvp; uniform mat4 u_mv;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_dist;\n"
    "void main() { v_uv = a_uv; v_col = a_col; vec4 e = u_mv * vec4(a_pos, 1.0); v_dist = -e.z;\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";
static const char *FS =
    "uniform sampler2D u_tex; uniform float u_alpha_test; uniform float u_fog; uniform vec4 u_fogcol; uniform vec4 u_fogrange;\n"
    "uniform vec4 u_tint; uniform float u_decal;\n"
    "varying vec2 v_uv; varying vec4 v_col; varying float v_dist;\n"
    "void main() { vec4 t = texture2D(u_tex, v_uv); vec4 c = (u_decal > 0.5 ? t : t * v_col) * u_tint;\n"
    "  if (c.a < u_alpha_test) discard;\n"
    "  if (u_fog > 0.5) { float f = clamp((v_dist - u_fogrange.x) / (u_fogrange.y - u_fogrange.x), 0.0, 1.0);\n"
    "    c.rgb = mix(c.rgb, u_fogcol.rgb, f); }\n"
    "  gl_FragColor = c; }\n";

static GLuint compile(GLenum type, const char *src)
{
    const char *hdr = plat_is_gles() ? "#version 100\nprecision mediump float;\n" : "#version 120\n";
    const char *parts[2] = { hdr, src };
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, parts, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "shader: %s\n", log);
        return 0;
    }
    return s;
}

int gfx_init(void)
{
#define X(ret, name, args) gl##name = (ret (GLAPI *) args)plat_gl_proc("gl" #name); \
    if (!gl##name && strcmp(#name, "GenerateMipmap")) { fprintf(stderr, "missing gl%s\n", #name); return 0; }
    GLFUNCS
#undef X
    g_has_mipmap = glGenerateMipmap != NULL;
    {   /* the original's bilinear filtering blurs the road into the distance; 8x anisotropic keeps it sharp */
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        if (ext && (strstr(ext, "_texture_filter_anisotropic"))) {
            float mx = 0;
            glGetFloatv(0x84FF, &mx);                   /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */
            g_aniso = mx > 8 ? 8 : mx;
        }
    }
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) return 0;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glBindAttribLocation(g_prog, 0, "a_pos");
    glBindAttribLocation(g_prog, 1, "a_uv");
    glBindAttribLocation(g_prog, 2, "a_col");
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "shader link failed\n"); return 0; }
    glUseProgram(g_prog);
    u_mvp = glGetUniformLocation(g_prog, "u_mvp");
    u_mv = glGetUniformLocation(g_prog, "u_mv");
    u_tex = glGetUniformLocation(g_prog, "u_tex");
    u_alpha_test = glGetUniformLocation(g_prog, "u_alpha_test");
    u_fog = glGetUniformLocation(g_prog, "u_fog");
    u_fogcol = glGetUniformLocation(g_prog, "u_fogcol");
    u_fogrange = glGetUniformLocation(g_prog, "u_fogrange");
    u_tint = glGetUniformLocation(g_prog, "u_tint");
    u_decal = glGetUniformLocation(g_prog, "u_decal");
    glUniform1i(u_tex, 0);
    glUniform4f(u_tint, 1, 1, 1, 1);
    glUniform1f(u_decal, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glGenBuffers(1, &g_stream_vbo);
    uint8_t white[4] = { 255, 255, 255, 255 };
    g_white = gfx_texture(white, 1, 1, TEX_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CW);      /* the original's faces are visible when clockwise on screen (D3DCULL_CCW) */
    glCullFace(GL_BACK);
    g_proj = g_view = g_model = m4_identity();
    return 1;
}

unsigned gfx_white(void) { return g_white; }

void gfx_begin(int width, int height, float r, float g, float b)
{
    g_w = width;
    g_h = height;
    g_fog_on = g_decal = 0;
    glViewport(0, 0, width, height);
    glDepthMask(1);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_viewport(int x, int y, int w, int h) { glViewport(x, g_h - y - h, w, h); }

unsigned gfx_texture(const uint8_t *rgba, int w, int h, int flags)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    int pot = !(w & (w - 1)) && !(h & (h - 1));
    int mip = (flags & TEX_MIPMAP) && g_has_mipmap && (pot || !plat_is_gles());
    if (mip) glGenerateMipmap(GL_TEXTURE_2D);
    GLint mag = (flags & TEX_NEAREST) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : mag);
    if (mip && g_aniso > 1) glTexParameterf(GL_TEXTURE_2D, 0x84FE, g_aniso);   /* GL_TEXTURE_MAX_ANISOTROPY_EXT */
    /* GLES 2.0 only repeats power-of-two textures */
    GLint wrap = ((flags & TEX_CLAMP) || (!pot && plat_is_gles())) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return t;
}

void gfx_texture_update(unsigned tex, const uint8_t *rgba, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void gfx_texture_free(unsigned tex) { if (tex) glDeleteTextures(1, &tex); }

static void upload_mvp(void)
{
    Mat4 mv = m4_mul(g_view, g_model), mvp = m4_mul(g_proj, mv);
    glUniformMatrix4fv(u_mvp, 1, 0, mvp.m);
    glUniformMatrix4fv(u_mv, 1, 0, mv.m);
}

void gfx_set_matrices(const Mat4 *proj, const Mat4 *view)
{
    g_proj = *proj;
    g_view = *view;
    g_model = m4_identity();
    g_2d = 0;
    upload_mvp();
}

void gfx_set_model(const Mat4 *model)
{
    g_model = model ? *model : m4_identity();
    upload_mvp();
}

void gfx_set_fog(float r, float g, float b, float start, float end)
{
    g_fog[0] = r; g_fog[1] = g; g_fog[2] = b; g_fog[3] = start; g_fog[4] = end;
    glUniform4f(u_fogcol, r, g, b, 1);
    glUniform4f(u_fogrange, start, end > start ? end : start + 1, 0, 0);
}

void gfx_set_tint(float r, float g, float b, float a)
{
    g_tint[0] = r; g_tint[1] = g; g_tint[2] = b; g_tint[3] = a;
    glUniform4f(u_tint, r, g, b, a);
}

void gfx_set_depth_off(int off) { g_depth_off = off; }
void gfx_set_fog_on(int on) { g_fog_on = on; }
void gfx_set_decal(int on) { g_decal = on; }

GfxState gfx_state_opaque(void)
{
    GfxState s = { BLEND_NONE, 1, 1, 1, 0, 0 };
    return s;
}

static void apply_state(const GfxState *s)
{
    if (s->blend == BLEND_NONE) glDisable(GL_BLEND);
    else {
        glEnable(GL_BLEND);
        if (s->blend == BLEND_ALPHA) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        else if (s->blend == BLEND_ADD) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else if (s->blend == BLEND_INVALPHA) glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);
        else glBlendFunc(GL_DST_COLOR, GL_ZERO);
    }
    if (s->depth_test && !g_depth_off) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(s->depth_write && !g_depth_off ? 1 : 0);
    if (s->cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glUniform1f(u_alpha_test, s->alpha_test == 2 ? 0.02f : s->alpha_test ? 0.5f : -1.0f);
    glUniform1f(u_fog, (s->fog || g_fog_on) && !g_2d ? 1.0f : 0.0f);
    glUniform1f(u_decal, g_decal && !g_2d ? 1.0f : 0.0f);
}

static void set_pointers(void)
{
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(GfxVertex), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(GfxVertex), (void *)12);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, sizeof(GfxVertex), (void *)20);
}

static GfxMesh *mesh_create(const GfxVertex *v, int nv, const void *idx, int ni, int isize, const GfxRange *r, int nr)
{
    GfxMesh *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    glGenBuffers(1, &m->vbo);
    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * sizeof *v, v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ni * isize, idx, GL_STATIC_DRAW);
    m->itype = isize == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    m->isize = isize;
    m->nr = nr;
    m->ranges = malloc(sizeof *r * (size_t)(nr ? nr : 1));
    if (nr) memcpy(m->ranges, r, sizeof *r * (size_t)nr);
    return m;
}

GfxMesh *gfx_mesh_create(const GfxVertex *v, int nv, const uint16_t *idx, int ni, const GfxRange *r, int nr)
{
    return mesh_create(v, nv, idx, ni, 2, r, nr);
}
GfxMesh *gfx_mesh_create32(const GfxVertex *v, int nv, const uint32_t *idx, int ni, const GfxRange *r, int nr)
{
    return mesh_create(v, nv, idx, ni, 4, r, nr);
}

void gfx_mesh_draw_range(const GfxMesh *m, int i)
{
    if (!m || i < 0 || i >= m->nr || m->ranges[i].count <= 0) return;
    const GfxRange *r = &m->ranges[i];
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    set_pointers();
    apply_state(&r->state);
    glBindTexture(GL_TEXTURE_2D, r->tex ? r->tex : g_white);
    glDrawElements(GL_TRIANGLES, r->count, m->itype, (void *)((size_t)r->first * (size_t)m->isize));
}

void gfx_mesh_draw_sub(const GfxMesh *m, int i, int first, int count)
{
    if (!m || i < 0 || i >= m->nr || count <= 0) return;
    const GfxRange *r = &m->ranges[i];
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    set_pointers();
    apply_state(&r->state);
    glBindTexture(GL_TEXTURE_2D, r->tex ? r->tex : g_white);
    glDrawElements(GL_TRIANGLES, count, m->itype, (void *)((size_t)first * (size_t)m->isize));
}

void gfx_mesh_draw(const GfxMesh *m)
{
    if (!m) return;
    for (int i = 0; i < m->nr; i++) gfx_mesh_draw_range(m, i);
}

void gfx_mesh_free(GfxMesh *m)
{
    if (!m) return;
    glDeleteBuffers(1, &m->vbo);
    glDeleteBuffers(1, &m->ibo);
    free(m->ranges);
    free(m);
}

void gfx_draw_tris(const GfxVertex *v, int nv, unsigned tex, const GfxState *st)
{
    if (nv <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, g_stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * sizeof *v, v, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    set_pointers();
    apply_state(st);
    glBindTexture(GL_TEXTURE_2D, tex ? tex : g_white);
    glDrawArrays(GL_TRIANGLES, 0, nv);
}

/* ---- 2D -------------------------------------------------------------------------------------------------------- */
void gfx_2d_begin(int vw, int vh)
{
    g_vw = vw;
    g_vh = vh;
    g_2d = 1;
    float sx = (float)g_w / vw, sy = (float)g_h / vh;
    g_scale = sx < sy ? sx : sy;
    g_ox = (g_w - vw * g_scale) * 0.5f;
    g_oy = (g_h - vh * g_scale) * 0.5f;
    glViewport(0, 0, g_w, g_h);
    Mat4 p = m4_ortho(0, (float)g_w, (float)g_h, 0, -1, 1), id = m4_identity();
    g_proj = p;
    g_view = id;
    g_model = id;
    upload_mvp();
}

void gfx_2d_to_screen(float vx, float vy, float *sx, float *sy)
{
    *sx = g_ox + vx * g_scale;
    *sy = g_oy + vy * g_scale;
}

void gfx_screen_to_2d(int sx, int sy, float *vx, float *vy)
{
    *vx = g_scale > 0 ? (sx - g_ox) / g_scale : 0;
    *vy = g_scale > 0 ? (sy - g_oy) / g_scale : 0;
}

void gfx_2d_quad(unsigned tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 uint32_t rgba, int blend)
{
    float x0, y0, x1, y1;
    gfx_2d_to_screen(x, y, &x0, &y0);
    gfx_2d_to_screen(x + w, y + h, &x1, &y1);
    uint8_t r = (uint8_t)(rgba >> 24), g = (uint8_t)(rgba >> 16), b = (uint8_t)(rgba >> 8), a = (uint8_t)rgba;
    GfxVertex v[6] = {
        { x0, y0, 0, u0, v0, r, g, b, a }, { x0, y1, 0, u0, v1, r, g, b, a }, { x1, y1, 0, u1, v1, r, g, b, a },
        { x0, y0, 0, u0, v0, r, g, b, a }, { x1, y1, 0, u1, v1, r, g, b, a }, { x1, y0, 0, u1, v0, r, g, b, a } };
    GfxState st = { blend, blend == BLEND_NONE, 0, 0, 0, 0 };
    gfx_draw_tris(v, 6, tex, &st);
}

void gfx_2d_rect(float x, float y, float w, float h, uint32_t rgba)
{
    gfx_2d_quad(g_white, x, y, w, h, 0, 0, 1, 1, rgba, (rgba & 0xff) == 0xff ? BLEND_NONE : BLEND_ALPHA);
}

void gfx_2d_end(void) {}

void gfx_read_pixels(int w, int h, uint8_t *rgb) { glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb); }
