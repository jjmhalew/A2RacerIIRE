/* vmath.h - small vector / 4x4 matrix helpers (column-major, as OpenGL takes them).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_VMATH_H
#define A2_VMATH_H
#include <math.h>
#include <string.h>

typedef struct { float x, y, z; } Vec3;
typedef struct { float m[16]; } Mat4;

#define PI_F 3.14159265358979f

static inline Vec3 v3(float x, float y, float z) { Vec3 r = { x, y, z }; return r; }
static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3_cross(Vec3 a, Vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3_len(Vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline Vec3 v3_norm(Vec3 a) { float l = v3_len(a); return l > 1e-12f ? v3_scale(a, 1.0f / l) : a; }
static inline Vec3 v3_lerp(Vec3 a, Vec3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

static inline Mat4 m4_identity(void) { Mat4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }

static inline Mat4 m4_mul(Mat4 a, Mat4 b)
{
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            r.m[c * 4 + k] = a.m[0 * 4 + k] * b.m[c * 4 + 0] + a.m[1 * 4 + k] * b.m[c * 4 + 1]
                           + a.m[2 * 4 + k] * b.m[c * 4 + 2] + a.m[3 * 4 + k] * b.m[c * 4 + 3];
    return r;
}

static inline Vec3 m4_point(const Mat4 *a, Vec3 p)
{
    return v3(a->m[0] * p.x + a->m[4] * p.y + a->m[8] * p.z + a->m[12],
              a->m[1] * p.x + a->m[5] * p.y + a->m[9] * p.z + a->m[13],
              a->m[2] * p.x + a->m[6] * p.y + a->m[10] * p.z + a->m[14]);
}

static inline Mat4 m4_translate(Vec3 t) { Mat4 r = m4_identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
static inline Mat4 m4_scale(Vec3 s) { Mat4 r = m4_identity(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }

static inline Mat4 m4_rot_y(float a)
{
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rot_x(float a)
{
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rot_z(float a)
{
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[4] = -s; r.m[1] = s; r.m[5] = c;
    return r;
}

/* right-handed perspective, depth -1..1, fovy in radians */
static inline Mat4 m4_perspective(float fovy, float aspect, float znear, float zfar)
{
    Mat4 r;
    memset(&r, 0, sizeof r);
    float f = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1;
    r.m[14] = 2 * zfar * znear / (znear - zfar);
    return r;
}

static inline Mat4 m4_ortho(float l, float r_, float b, float t, float n, float f)
{
    Mat4 r = m4_identity();
    r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}

/* camera at eye looking at target, y up (right-handed: the camera looks down -z) */
static inline Mat4 m4_look_at(Vec3 eye, Vec3 target, Vec3 up)
{
    Vec3 f = v3_norm(v3_sub(target, eye)), s = v3_norm(v3_cross(f, up)), u = v3_cross(s, f);
    Mat4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye); r.m[13] = -v3_dot(u, eye); r.m[14] = v3_dot(f, eye);
    return r;
}
#endif
