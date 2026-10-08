/* gfx_gl.c - gfx.h on OpenGL 2.1 / OpenGL ES 2.0. All GL entry points are loaded at run time through plat_gl_proc,
 * so no GL headers or import libraries are needed beyond the system's GL library (opengl32 / libGL / libGLESv2).
 * Textures are handles into a table of GL names, so that when Android loses the context (plat_gl_lost) everything can
 * be made again under the same handles: on OpenGL ES each texture and mesh keeps a copy of what was uploaded.
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

/* framebuffer objects (OpenGL 3.0 / ARB_framebuffer_object / EXT_framebuffer_object, OpenGL ES 2.0): optional, only
 * the offscreen target of the low-resolution modes uses them (gfx_target_begin) */
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define FBOFUNCS \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint *)) X(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))
#define X(ret, name, args) static ret (GLAPI *gl##name) args;
FBOFUNCS
#undef X
static int g_has_fbo;

struct GfxMesh {
    GLuint vbo, ibo;
    GLenum itype;
    int isize;
    int nr;
    GfxRange *ranges;
    /* plat_gl_can_lose: the uploaded data, and the list of meshes, for a lost context */
    void *keep_v, *keep_i;
    size_t nv_bytes, ni_bytes;
    GfxMesh *prev, *next;
};

typedef struct {
    GLuint name;            /* 0 = a free slot, NOT_KEPT = lost with the context and not kept */
    int w, h, mip, wrap, filter;
    uint8_t *keep;          /* plat_gl_can_lose: the pixels as uploaded (level 0), else NULL */
} Tex;
#define NOT_KEPT ((GLuint)-1)
static Tex *g_tex;          /* handle n = g_tex[n - 1] */
static int g_ntex, g_captex;
static GfxMesh *g_meshes;

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
    /* vertex shaders default to highp; mediump positions jitter on GPUs that honour it (track coordinates reach 1e4) */
    const char *hdr = !plat_is_gles() ? "#version 120\n"
                    : type == GL_FRAGMENT_SHADER ? "#version 100\nprecision mediump float;\n" : "#version 100\n";
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

/* the shader, the stream buffer and the fixed states: at the start and again in a new context */
static int setup(void)
{
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CW);      /* the original's faces are visible when clockwise on screen (D3DCULL_CCW) */
    glCullFace(GL_BACK);
    return 1;
}

int gfx_init(void)
{
#define X(ret, name, args) gl##name = (ret (GLAPI *) args)plat_gl_proc("gl" #name); \
    if (!gl##name && strcmp(#name, "GenerateMipmap")) { fprintf(stderr, "missing gl%s\n", #name); return 0; }
    GLFUNCS
#undef X
    g_has_fbo = 1;          /* the core names, else the EXT ones (same signatures) */
#define X(ret, name, args) gl##name = (ret (GLAPI *) args)plat_gl_proc("gl" #name);     if (!gl##name) gl##name = (ret (GLAPI *) args)plat_gl_proc("gl" #name "EXT");     if (!gl##name) g_has_fbo = 0;
    FBOFUNCS
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
    /* Android can run SDL_main again in the same process: the table of a previous run names nothing now */
    for (int i = 0; i < g_ntex; i++) free(g_tex[i].keep);
    free(g_tex);
    g_tex = NULL;
    g_ntex = g_captex = 0;
    g_meshes = NULL;
    if (!setup()) return 0;
    uint8_t white[4] = { 255, 255, 255, 255 };
    g_white = gfx_texture(white, 1, 1, TEX_NEAREST);
    g_proj = g_view = g_model = m4_identity();
    return 1;
}

static void upload(Tex *t, const uint8_t *rgba)
{
    glGenTextures(1, &t->name);
    glBindTexture(GL_TEXTURE_2D, t->name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (t->mip) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t->filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, t->mip ? GL_LINEAR_MIPMAP_LINEAR : t->filter);
    if (t->mip && g_aniso > 1) glTexParameterf(GL_TEXTURE_2D, 0x84FE, g_aniso);   /* GL_TEXTURE_MAX_ANISOTROPY_EXT */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, t->wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t->wrap);
}

static void mesh_upload(GfxMesh *m, const void *v, const void *idx)
{
    glGenBuffers(1, &m->vbo);
    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->nv_bytes, v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m->ni_bytes, idx, GL_STATIC_DRAW);
}

static void fbo_forget(void);
static void post_forget(void);

/* plat_gl_lost: the new context has none of the old objects; make them again under the same handles */
static void check_lost(void)
{
    if (!plat_gl_lost()) return;
    setup();
    fbo_forget();                               /* the offscreen targets are made again at their next use */
    post_forget();
    glUniform4f(u_tint, g_tint[0], g_tint[1], g_tint[2], g_tint[3]);
    gfx_set_fog(g_fog[0], g_fog[1], g_fog[2], g_fog[3], g_fog[4]);
    int n = 0;
    for (int i = 0; i < g_ntex; i++) {
        Tex *t = &g_tex[i];
        if (!t->name) continue;
        if (t->keep) { upload(t, t->keep); n++; }
        else t->name = NOT_KEPT;            /* drawn with the white texture */
    }
    for (GfxMesh *m = g_meshes; m; m = m->next)
        if (m->keep_v) mesh_upload(m, m->keep_v, m->keep_i);
    fprintf(stderr, "gfx: %d textures made again\n", n);
}

static GLuint gl_tex(unsigned h)
{
    if (!h || h > (unsigned)g_ntex || g_tex[h - 1].name == NOT_KEPT) h = g_white;
    return h && h <= (unsigned)g_ntex ? g_tex[h - 1].name : 0;
}

unsigned gfx_white(void) { return g_white; }

void gfx_begin(int width, int height, float r, float g, float b)
{
    check_lost();
    g_w = width;
    g_h = height;
    g_fog_on = g_decal = 0;
    glViewport(0, 0, width, height);
    glDepthMask(1);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_viewport(int x, int y, int w, int h) { glViewport(x, g_h - y - h, w, h); }

static int next_pot(int n)
{
    int p = 1;
    while (p < n && p < 4096) p <<= 1;
    return p;
}

/* bilinear, wrapping at the edges as GL_REPEAT does */
static uint8_t *resample(const uint8_t *src, int w, int h, int nw, int nh)
{
    uint8_t *dst = malloc((size_t)nw * nh * 4);
    if (!dst) return NULL;
    for (int y = 0; y < nh; y++) {
        float fy = (y + 0.5f) * h / nh - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        int ya = (y0 % h + h) % h, yb = (ya + 1) % h;
        for (int x = 0; x < nw; x++) {
            float fx = (x + 0.5f) * w / nw - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int xa = (x0 % w + w) % w, xb = (xa + 1) % w;
            const uint8_t *p00 = src + ((size_t)ya * w + xa) * 4, *p01 = src + ((size_t)ya * w + xb) * 4;
            const uint8_t *p10 = src + ((size_t)yb * w + xa) * 4, *p11 = src + ((size_t)yb * w + xb) * 4;
            uint8_t *d = dst + ((size_t)y * nw + x) * 4;
            for (int c = 0; c < 4; c++) {
                float a = p00[c] + (p01[c] - p00[c]) * tx, b = p10[c] + (p11[c] - p10[c]) * tx;
                d[c] = (uint8_t)(a + (b - a) * ty + 0.5f);
            }
        }
    }
    return dst;
}

unsigned gfx_texture(const uint8_t *rgba, int w, int h, int flags)
{
    check_lost();
    int slot = 0;
    while (slot < g_ntex && g_tex[slot].name) slot++;
    if (slot == g_ntex) {
        if (g_ntex == g_captex) {
            int cap = g_captex ? g_captex * 2 : 256;
            Tex *nt = realloc(g_tex, sizeof *nt * (size_t)cap);
            if (!nt) return 0;
            g_tex = nt;
            g_captex = cap;
        }
        g_ntex++;
    }
    int pot = !(w & (w - 1)) && !(h & (h - 1));
    uint8_t *scaled = NULL;
    if (!pot && plat_is_gles() && !(flags & TEX_CLAMP)) {
        /* GLES 2.0 neither repeats nor mipmaps other sizes (texture packs may have any size): scale up */
        int nw = next_pot(w), nh = next_pot(h);
        if ((scaled = resample(rgba, w, h, nw, nh))) { rgba = scaled; w = nw; h = nh; pot = 1; }
    }
    Tex *t = &g_tex[slot];
    memset(t, 0, sizeof *t);
    t->w = w;
    t->h = h;
    t->mip = (flags & TEX_MIPMAP) && g_has_mipmap && (pot || !plat_is_gles());
    t->filter = (flags & TEX_NEAREST) ? GL_NEAREST : GL_LINEAR;
    /* GLES 2.0 only repeats power-of-two textures */
    t->wrap = ((flags & TEX_CLAMP) || (!pot && plat_is_gles())) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    if (plat_gl_can_lose() && (t->keep = malloc((size_t)w * h * 4))) memcpy(t->keep, rgba, (size_t)w * h * 4);
    upload(t, rgba);
    free(scaled);
    return (unsigned)slot + 1;
}

void gfx_texture_update(unsigned tex, const uint8_t *rgba, int w, int h)
{
    check_lost();
    if (!tex || tex > (unsigned)g_ntex || !g_tex[tex - 1].name || g_tex[tex - 1].name == NOT_KEPT) return;
    Tex *t = &g_tex[tex - 1];
    if (w > t->w) w = t->w;
    if (h > t->h) h = t->h;
    if (t->keep)
        for (int y = 0; y < h; y++) memcpy(t->keep + (size_t)y * t->w * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
    glBindTexture(GL_TEXTURE_2D, t->name);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (t->mip) glGenerateMipmap(GL_TEXTURE_2D);    /* the texture copies of txcopy.c: rebuild the smaller levels */
}

void gfx_texture_free(unsigned tex)
{
    if (!tex || tex > (unsigned)g_ntex || !g_tex[tex - 1].name) return;
    Tex *t = &g_tex[tex - 1];
    if (t->name != NOT_KEPT) glDeleteTextures(1, &t->name);
    free(t->keep);
    memset(t, 0, sizeof *t);
}

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
    check_lost();
    GfxMesh *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->nv_bytes = (size_t)nv * sizeof *v;
    m->ni_bytes = (size_t)ni * (size_t)isize;
    mesh_upload(m, v, idx);
    if (plat_gl_can_lose()) {
        m->keep_v = malloc(m->nv_bytes + 1);
        m->keep_i = malloc(m->ni_bytes + 1);
        if (m->keep_v && m->keep_i) {
            memcpy(m->keep_v, v, m->nv_bytes);
            memcpy(m->keep_i, idx, m->ni_bytes);
        } else { free(m->keep_v); free(m->keep_i); m->keep_v = m->keep_i = NULL; }
    }
    m->next = g_meshes;
    if (g_meshes) g_meshes->prev = m;
    g_meshes = m;
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

/* binds the range's texture; a clamped range (GfxRange.clamp) switches the texture to clamp-to-edge for its draw and
 * returns the wrap mode range_done puts back (0 = nothing to put back). The wrap mode is texture state, and other
 * users of the same texture (track, effects, other meshes) repeat it. */
static GLint range_bind(const GfxRange *r)
{
    glBindTexture(GL_TEXTURE_2D, gl_tex(r->tex));
    if (!r->clamp || !r->tex || r->tex > (unsigned)g_ntex) return 0;
    const Tex *t = &g_tex[r->tex - 1];
    if (t->name == NOT_KEPT || t->wrap == GL_CLAMP_TO_EDGE) return 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t->wrap;
}

static void range_done(GLint wrap)
{
    if (!wrap) return;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void gfx_mesh_draw_range(const GfxMesh *m, int i)
{
    if (!m || i < 0 || i >= m->nr || m->ranges[i].count <= 0) return;
    const GfxRange *r = &m->ranges[i];
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    set_pointers();
    apply_state(&r->state);
    GLint wrap = range_bind(r);
    glDrawElements(GL_TRIANGLES, r->count, m->itype, (void *)((size_t)r->first * (size_t)m->isize));
    range_done(wrap);
}

void gfx_mesh_draw_sub(const GfxMesh *m, int i, int first, int count)
{
    if (!m || i < 0 || i >= m->nr || count <= 0) return;
    const GfxRange *r = &m->ranges[i];
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    set_pointers();
    apply_state(&r->state);
    GLint wrap = range_bind(r);
    glDrawElements(GL_TRIANGLES, count, m->itype, (void *)((size_t)first * (size_t)m->isize));
    range_done(wrap);
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
    if (m->prev) m->prev->next = m->next; else g_meshes = m->next;
    if (m->next) m->next->prev = m->prev;
    free(m->keep_v);
    free(m->keep_i);
    free(m->ranges);
    free(m);
}

void gfx_draw_tris(const GfxVertex *v, int nv, unsigned tex, const GfxState *st)
{
    if (nv <= 0) return;
    check_lost();
    glBindBuffer(GL_ARRAY_BUFFER, g_stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * sizeof *v, v, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    set_pointers();
    apply_state(st);
    glBindTexture(GL_TEXTURE_2D, gl_tex(tex));
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

/* ---- the offscreen target of the low-resolution modes (screen.c) ------------------------------------------------ */
/* PORT: the 8-bit modes are drawn at their own size into a framebuffer object (a colour texture and a 16-bit depth
 * buffer, the original's Z-buffer format) and then scaled up into the window. The target is made on first use and
 * again when its size changes or g_fb.valid was cleared (after a lost GL context: the names are gone, nothing to
 * delete); its colour texture is a plain GL texture, not one of gfx_texture's. */
static struct { GLuint fbo, tex, depth; int w, h, valid; } g_fb;
static int g_fb_failed;         /* the driver cannot render into it: draw into the window instead */

static void fbo_destroy(void)
{
    if (g_fb.valid) {
        glDeleteFramebuffers(1, &g_fb.fbo);
        glDeleteRenderbuffers(1, &g_fb.depth);
        glDeleteTextures(1, &g_fb.tex);
    }
    memset(&g_fb, 0, sizeof g_fb);
}

static void fbo_forget(void) { memset(&g_fb, 0, sizeof g_fb); }    /* its names died with the context */

static int fbo_create(int w, int h)
{
    fbo_destroy();
    glGenTextures(1, &g_fb.tex);
    glBindTexture(GL_TEXTURE_2D, g_fb.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &g_fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb.fbo);
    glGenRenderbuffers(1, &g_fb.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_fb.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_fb.depth);
    g_fb.valid = 1;
    g_fb.w = w;
    g_fb.h = h;
    /* RGBA8, else RGB565 (a colour format every OpenGL ES 2.0 renders to) */
    for (int k = 0; k < 2; k++) {
        glTexImage2D(GL_TEXTURE_2D, 0, k ? GL_RGB : GL_RGBA, w, h, 0, k ? GL_RGB : GL_RGBA,
                     k ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fb.tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fbo_destroy();
    fprintf(stderr, "gfx: no offscreen target of %dx%d; the low resolution is drawn into the window\n", w, h);
    return 0;
}

int gfx_target_begin(int w, int h, float r, float g, float b)
{
    if (!g_has_fbo || g_fb_failed || w <= 0 || h <= 0) return 0;
    gfx_begin(w, h, r, g, b);                    /* (clears the window's buffer for nothing; gfx_target_present does) */
    if ((!g_fb.valid || g_fb.w != w || g_fb.h != h) && !fbo_create(w, h)) {
        g_fb_failed = 1;
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_fb.fbo);
    glViewport(0, 0, w, h);
    glDepthMask(1);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}

void gfx_target_present(int win_w, int win_h, int x, int y, int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_w = win_w;
    g_h = win_h;
    glViewport(0, 0, win_w, win_h);
    glDepthMask(1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!g_fb.valid) return;
    gfx_2d_begin(win_w, win_h);                  /* window pixels; the target's rows are bottom-up (v 1 at the top) */
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w), y1 = (float)(y + h);
    GfxVertex v[6] = {
        { x0, y0, 0, 0, 1, 255, 255, 255, 255 }, { x0, y1, 0, 0, 0, 255, 255, 255, 255 }, { x1, y1, 0, 1, 0, 255, 255, 255, 255 },
        { x0, y0, 0, 0, 1, 255, 255, 255, 255 }, { x1, y1, 0, 1, 0, 255, 255, 255, 255 }, { x1, y0, 0, 1, 1, 255, 255, 255, 255 } };
    glBindBuffer(GL_ARRAY_BUFFER, g_stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof v, v, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    set_pointers();
    GfxState st = { BLEND_NONE, 0, 0, 0, 0, 0 };
    apply_state(&st);
    glBindTexture(GL_TEXTURE_2D, g_fb.tex);      /* nearest: the period's big pixels */
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* ---- the scene target: multisampling and ambient occlusion (PORT EXTRA, graphics.ini; docs/RENDER.md 12) -------- */
/* The original draws without either. With one of them on (and OpenGL 3 / OpenGL ES 3, for the multisampled targets and
 * the depth blit), the 3D picture of the 640x480x16 mode is drawn into an own target of the window's size instead of
 * the window: multisampled with MSAA (colour and depth renderbuffers), else a colour texture with a depth buffer.
 * gfx_scene_ao, after the opaque world and cars, blits its depth into a depth texture and runs the ground-truth
 * ambient occlusion of Jimenez et al. 2016 (in XeGTAO's form, the shader of WoodyRE's gtao.c): the visibility per
 * pixel into an RGBA8 texture, then a depth-aware 4x4 blur multiplied into the picture. gfx_scene_end resolves the
 * target (MSAA) into its texture and draws that into the window; the overlay is drawn into the target before that,
 * the touch controls into the window after it. */
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_MAX_SAMPLES 0x8D57
#define GL_VERSION 0x1F02
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_SRC_COLOR 0x0300
#define AO_RADIUS 18.0f               /* world units (1/10 m): 1.8 m, about half a car */
#define AO_POWER 1.5f                 /* visibility ^ POWER: a little more contrast than the raw integral */

static void (GLAPI *glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void (GLAPI *glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void (GLAPI *glGetIntegerv)(GLenum, GLint *);
static void (GLAPI *glUniform2f)(GLint, GLfloat, GLfloat);
static void (GLAPI *glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);

static int g_msaa, g_ao;                    /* gfx_post_set */
static int g_post_ok = -1;                  /* -1 = not checked yet, 0 = this GL cannot, 1 = can */
static GLint g_max_samples;
static struct {
    GLuint fbo, color, depth;               /* what the 3D picture is drawn into (fbo = rfbo without MSAA) */
    GLuint rfbo, tex;                       /* the colour texture the frame ends in */
    GLuint dfbo, dtex, afbo, atex;          /* ambient occlusion: the depth copy, the visibility */
    int w, h, samples, ao, valid;
} g_sc;
static GLuint g_prog_ao, g_prog_mix;
static GLint ua_depth, ua_size, ua_z, ua_scale, ua_radius, um_ao, um_depth, um_size, um_z, um_power;
static int g_ao_failed, g_scene;

static const char *AO_VS =
    "attribute vec3 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos.xy, 0.0, 1.0); }\n";
static const char *AO_FS =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uSize;\n"                   /* the target in pixels */
    "uniform vec3 uZ;\n"                      /* zn * zf, zf - zn, zf */
    "uniform vec2 uScale;\n"                  /* the projection's x / y scale */
    "uniform float uRadius;\n"
    "const float PI = 3.14159265, HALF_PI = 1.57079633;\n"
    "const int SLICES = 3, STEPS = 6;\n"
    "float lin(float d) { return uZ.x / (uZ.z - d * uZ.y); }\n"
    "float dep(vec2 uv) { return texture2D(uDepth, uv).r; }\n"
    "vec3 viewPos(vec2 uv, float d) { float L = lin(d); vec2 n = uv * 2.0 - 1.0; return vec3(n.x * L / uScale.x, n.y * L / uScale.y, -L); }\n"
    "vec3 posAt(vec2 uv) { return viewPos(uv, dep(uv)); }\n"
    "float fastAcos(float x) { float r = (-0.156583 * abs(x) + HALF_PI) * sqrt(1.0 - abs(x)); return x >= 0.0 ? r : PI - r; }\n"
    "float bayer2(vec2 v) { return mod(2.0 * v.x + 3.0 * v.y, 4.0); }\n"
    "void main() {\n"
    "    vec2 inv = 1.0 / uSize, uv = gl_FragCoord.xy * inv;\n"
    "    float d = dep(uv);\n"
    "    if (d >= 1.0) { gl_FragColor = vec4(1.0); return; }\n"
    "    vec3 P = viewPos(uv, d);\n"
    /* the normal from the neighbour on the same surface per axis (the smaller depth step) */
    "    vec3 l = posAt(uv - vec2(inv.x, 0.0)), r = posAt(uv + vec2(inv.x, 0.0)), b = posAt(uv - vec2(0.0, inv.y)), t = posAt(uv + vec2(0.0, inv.y));\n"
    "    vec3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;\n"
    "    vec3 dy = abs(t.z - P.z) < abs(P.z - b.z) ? t - P : P - b;\n"
    "    vec3 V = normalize(-P), N = normalize(cross(dx, dy));\n"
    "    if (dot(N, V) < 0.0) N = -N;\n"
    "    float rpx = min(uRadius * uScale.y * 0.5 * uSize.y / -P.z, 0.25 * uSize.y);\n"   /* the radius in pixels, capped */
    "    if (rpx < 1.0) { gl_FragColor = vec4(1.0); return; }\n"
    "    vec2 c = mod(floor(gl_FragCoord.xy), 4.0);\n"
    "    float bay = 4.0 * bayer2(mod(c, 2.0)) + bayer2(floor(c * 0.5));\n"                /* 0..15, one 4x4 tile */
    "    float nSlice = (bay + 0.5) / 16.0, nStep = (mod(bay * 5.0 + 3.0, 16.0) + 0.5) / 16.0;\n"
    "    float fMul = -1.0 / (0.615 * uRadius), fAdd = (1.0 - 0.615) / 0.615 + 1.0;\n"    /* falloff over the outer 61.5 % */
    "    float vis = 0.0;\n"
    "    for (int s = 0; s < SLICES; s++) {\n"
    "        float phi = (float(s) + nSlice) * (PI / float(SLICES));\n"
    "        vec2 om = vec2(cos(phi), sin(phi));\n"
    "        vec3 dir = vec3(om, 0.0), ortho = dir - dot(dir, V) * V, axis = normalize(cross(ortho, V));\n"
    "        vec3 pN = N - axis * dot(N, axis); float pLen = length(pN);\n"
    "        if (pLen < 1e-4) { vis += 1.0; continue; }\n"
    "        float cosN = clamp(dot(pN, V) / pLen, 0.0, 1.0), n = sign(dot(ortho, pN)) * fastAcos(cosN);\n"
    "        float low0 = cos(n + HALF_PI), low1 = cos(n - HALF_PI), h0 = low0, h1 = low1;\n"
    "        for (int k = 0; k < STEPS; k++) {\n"
    "            float st = (float(k) + nStep) / float(STEPS); st *= st;\n"
    "            vec2 off = om * (st * rpx + 1.0) * inv;\n"
    "            vec3 d0 = posAt(uv + off) - P, d1 = posAt(uv - off) - P;\n"
    "            float l0 = length(d0), l1 = length(d1);\n"
    "            h0 = max(h0, mix(low0, dot(d0, V) / l0, clamp(l0 * fMul + fAdd, 0.0, 1.0)));\n"
    "            h1 = max(h1, mix(low1, dot(d1, V) / l1, clamp(l1 * fMul + fAdd, 0.0, 1.0)));\n"
    "        }\n"
    "        float a1 = n + clamp(fastAcos(h0) - n, -HALF_PI, HALF_PI), a0 = n + clamp(-fastAcos(h1) - n, -HALF_PI, HALF_PI), sn = sin(n);\n"
    "        vis += pLen * ((cosN + 2.0 * a0 * sn - cos(2.0 * a0 - n)) + (cosN + 2.0 * a1 * sn - cos(2.0 * a1 - n))) * 0.25;\n"
    "    }\n"
    "    gl_FragColor = vec4(vec3(clamp(vis / float(SLICES), 0.0, 1.0)), 1.0);\n"
    "}\n";
static const char *MIX_FS =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "uniform sampler2D uAO, uDepth;\n"
    "uniform vec2 uSize;\n"
    "uniform vec3 uZ;\n"
    "uniform float uPower;\n"
    "float lin(float d) { return uZ.x / (uZ.z - d * uZ.y); }\n"
    "void main() {\n"
    "    vec2 inv = 1.0 / uSize, uv = gl_FragCoord.xy * inv;\n"
    "    float d = texture2D(uDepth, uv).r;\n"
    "    if (d >= 1.0) { gl_FragColor = vec4(1.0); return; }\n"
    "    float Lc = lin(d), sum = 0.0, wsum = 0.0;\n"
    "    for (int j = -2; j < 2; j++) for (int i = -2; i < 2; i++) {\n"
    "        vec2 q = uv + vec2(float(i), float(j)) * inv;\n"
    "        float w = exp(-abs(lin(texture2D(uDepth, q).r) - Lc) / (0.04 * Lc));\n"
    "        sum += texture2D(uAO, q).r * w; wsum += w;\n"
    "    }\n"
    "    float ao = pow(sum / wsum, uPower);\n"
    "    gl_FragColor = vec4(ao, ao, ao, 1.0);\n"
    "}\n";

void gfx_post_set(int msaa, int ao)
{
    g_msaa = msaa >= 2 ? msaa : 0;
    g_ao = ao != 0;
}

/* OpenGL 3 / OpenGL ES 3 (or a desktop OpenGL 2.1 with ARB_framebuffer_object): multisampled renderbuffers and
 * glBlitFramebuffer. An OpenGL ES 2.0 context finds the entry points too, so its version decides */
static int post_capable(void)
{
    if (g_post_ok >= 0) return g_post_ok;
    g_post_ok = 0;
    if (!g_has_fbo) return 0;
    const char *ver = (const char *)glGetString(GL_VERSION), *ext = (const char *)glGetString(GL_EXTENSIONS);
    const char *p = ver ? ver : "";
    while (*p && (*p < '0' || *p > '9')) p++;
    int major = atoi(p);
    if (major < 3 && !(!plat_is_gles() && ext && strstr(ext, "GL_ARB_framebuffer_object"))) {
        fprintf(stderr, "gfx: %s: no MSAA / ambient occlusion (they need OpenGL 3 or OpenGL ES 3)\n", ver ? ver : "?");
        return 0;
    }
#define LOAD(v, name) do { void *f_ = plat_gl_proc(name); if (!f_) f_ = plat_gl_proc(name "EXT"); memcpy(&v, &f_, sizeof f_); } while (0)
    LOAD(glBlitFramebuffer, "glBlitFramebuffer");
    LOAD(glRenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
    LOAD(glGetIntegerv, "glGetIntegerv");
    LOAD(glUniform2f, "glUniform2f");
    LOAD(glUniform3f, "glUniform3f");
#undef LOAD
    if (!glBlitFramebuffer || !glRenderbufferStorageMultisample || !glGetIntegerv || !glUniform2f || !glUniform3f) return 0;
    glGetIntegerv(GL_MAX_SAMPLES, &g_max_samples);
    g_post_ok = 1;
    return 1;
}

static void scene_destroy(void)
{
    if (g_sc.valid) {
        GLuint fb[4] = { g_sc.fbo != g_sc.rfbo ? g_sc.fbo : 0, g_sc.rfbo, g_sc.dfbo, g_sc.afbo }, rb[2] = { g_sc.color, g_sc.depth };
        GLuint tx[3] = { g_sc.tex, g_sc.dtex, g_sc.atex };
        for (int i = 0; i < 4; i++) if (fb[i]) glDeleteFramebuffers(1, &fb[i]);
        for (int i = 0; i < 2; i++) if (rb[i]) glDeleteRenderbuffers(1, &rb[i]);
        for (int i = 0; i < 3; i++) if (tx[i]) glDeleteTextures(1, &tx[i]);
    }
    memset(&g_sc, 0, sizeof g_sc);
}

static GLuint target_tex(int w, int h, GLenum ifmt, GLenum fmt, GLenum type)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (fmt == GL_DEPTH_COMPONENT) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, fmt, type, NULL);
    return t;
}

static GLuint renderbuffer(int samples, GLenum fmt, int w, int h)
{
    GLuint r;
    glGenRenderbuffers(1, &r);
    glBindRenderbuffer(GL_RENDERBUFFER, r);
    if (samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
    else glRenderbufferStorage(GL_RENDERBUFFER, fmt, w, h);
    return r;
}

static int scene_create(int w, int h)
{
    scene_destroy();
    int s = g_msaa > g_max_samples ? g_max_samples : g_msaa;
    g_sc.valid = 1;
    g_sc.w = w;
    g_sc.h = h;
    g_sc.samples = s >= 2 ? s : 0;
    g_sc.ao = g_ao;
    g_sc.tex = target_tex(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    glGenFramebuffers(1, &g_sc.rfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_sc.rfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sc.tex, 0);
    int ok = 1;
    if (g_sc.samples) {
        ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glGenFramebuffers(1, &g_sc.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_sc.fbo);
        g_sc.color = renderbuffer(g_sc.samples, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_sc.color);
    } else g_sc.fbo = g_sc.rfbo;
    g_sc.depth = renderbuffer(g_sc.samples, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_sc.depth);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok && g_sc.ao) {   /* the depth copy (with the visibility texture as its colour, for completeness) */
        g_sc.dtex = target_tex(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
        g_sc.atex = target_tex(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        glGenFramebuffers(1, &g_sc.dfbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_sc.dfbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sc.atex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_sc.dtex, 0);
        ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glGenFramebuffers(1, &g_sc.afbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_sc.afbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sc.atex, 0);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "gfx: no %dx%d scene target (MSAA %d, AO %d): drawn as the original\n", w, h, g_sc.samples, g_sc.ao);
        scene_destroy();
    }
    return ok;
}

static void post_forget(void)              /* a lost context: the names are gone, the programs too */
{
    memset(&g_sc, 0, sizeof g_sc);
    g_prog_ao = g_prog_mix = 0;
    g_scene = 0;
}

int gfx_scene_begin(int w, int h, float r, float g, float b)
{
    gfx_begin(w, h, r, g, b);
    g_scene = 0;
    if ((!g_msaa && !g_ao) || w <= 0 || h <= 0 || !post_capable()) return 0;
    int want = g_msaa > g_max_samples ? g_max_samples : g_msaa;
    if (want < 2) want = 0;
    if (!g_sc.valid || g_sc.w != w || g_sc.h != h || g_sc.samples != want || g_sc.ao != g_ao)
        if (!scene_create(w, h)) { g_post_ok = 0; return 0; }
    glBindFramebuffer(GL_FRAMEBUFFER, g_sc.fbo);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_scene = 1;
    return 1;
}

static GLuint post_program(const char *fs)
{
    GLuint v = compile(GL_VERTEX_SHADER, AO_VS), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

/* a quad over the whole viewport in clip space, through the stream buffer */
static void fullscreen_quad(void)
{
    GfxVertex v[6] = {
        { -1, -1, 0, 0, 0, 255, 255, 255, 255 }, { 1, -1, 0, 1, 0, 255, 255, 255, 255 }, { 1, 1, 0, 1, 1, 255, 255, 255, 255 },
        { -1, -1, 0, 0, 0, 255, 255, 255, 255 }, { 1, 1, 0, 1, 1, 255, 255, 255, 255 }, { -1, 1, 0, 0, 1, 255, 255, 255, 255 } };
    glBindBuffer(GL_ARRAY_BUFFER, g_stream_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof v, v, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    set_pointers();
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void gfx_scene_ao(const Mat4 *proj, float zn, float zf)
{
    if (!g_scene || !g_sc.ao || g_ao_failed) return;
    if (!g_prog_ao) {
        g_prog_ao = post_program(AO_FS);
        g_prog_mix = post_program(MIX_FS);
        if (!g_prog_ao || !g_prog_mix) {
            fprintf(stderr, "gfx: the ambient occlusion shaders failed: off\n");
            g_ao_failed = 1;
            glUseProgram(g_prog);
            return;
        }
        ua_depth = glGetUniformLocation(g_prog_ao, "uDepth");
        ua_size = glGetUniformLocation(g_prog_ao, "uSize");
        ua_z = glGetUniformLocation(g_prog_ao, "uZ");
        ua_scale = glGetUniformLocation(g_prog_ao, "uScale");
        ua_radius = glGetUniformLocation(g_prog_ao, "uRadius");
        um_ao = glGetUniformLocation(g_prog_mix, "uAO");
        um_depth = glGetUniformLocation(g_prog_mix, "uDepth");
        um_size = glGetUniformLocation(g_prog_mix, "uSize");
        um_z = glGetUniformLocation(g_prog_mix, "uZ");
        um_power = glGetUniformLocation(g_prog_mix, "uPower");
    }
    int w = g_sc.w, h = g_sc.h;
    /* 1. the depth, one sample of each pixel under MSAA */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_sc.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_sc.dfbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    /* 2. the visibility */
    glBindFramebuffer(GL_FRAMEBUFFER, g_sc.afbo);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(0);
    glDisable(GL_CULL_FACE);
    glUseProgram(g_prog_ao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_sc.dtex);
    glUniform1i(ua_depth, 0);
    glUniform2f(ua_size, (float)w, (float)h);
    glUniform3f(ua_z, zn * zf, zf - zn, zf);
    glUniform2f(ua_scale, proj->m[0], proj->m[5]);
    glUniform1f(ua_radius, AO_RADIUS);
    fullscreen_quad();
    /* 3. blurred and multiplied into the picture */
    glBindFramebuffer(GL_FRAMEBUFFER, g_sc.fbo);
    glUseProgram(g_prog_mix);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_sc.atex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(um_ao, 1);
    glUniform1i(um_depth, 0);
    glUniform2f(um_size, (float)w, (float)h);
    glUniform3f(um_z, zn * zf, zf - zn, zf);
    glUniform1f(um_power, AO_POWER);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    fullscreen_quad();
    /* back to the renderer's own state (apply_state sets the rest per draw) */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(g_prog);
    upload_mvp();
}

/* the target into the window, 1:1 (the colour texture's rows are bottom-up like the window's) */
void gfx_scene_end(void)
{
    if (!g_scene) return;
    g_scene = 0;
    int w = g_sc.w, h = g_sc.h;
    if (g_sc.samples) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_sc.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_sc.rfbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    Mat4 id = m4_identity();
    Mat4 sp = g_proj, sv = g_view, sm = g_model;
    g_proj = g_view = g_model = id;
    upload_mvp();
    GfxState st = { BLEND_NONE, 0, 0, 0, 0, 0 };
    int two_d = g_2d;
    g_2d = 1;                                  /* no fog, no decal */
    apply_state(&st);
    g_2d = two_d;
    glUniform4f(u_tint, 1, 1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, g_sc.tex);
    fullscreen_quad();
    glUniform4f(u_tint, g_tint[0], g_tint[1], g_tint[2], g_tint[3]);
    g_proj = sp; g_view = sv; g_model = sm;
    upload_mvp();
}

/* GLES only guarantees RGBA reads */
void gfx_read_pixels(int w, int h, uint8_t *rgb)
{
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) memcpy(rgb + i * 3, rgba + i * 4, 3);
    free(rgba);
}
