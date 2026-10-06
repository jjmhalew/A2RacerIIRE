/* touch.c - see touch.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "touch.h"
#include "gfx.h"
#include "hud.h"
#include "pad.h"
#include "plat.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* the controls; C_TAP is a finger of the pause and end screens (anywhere) */
enum { C_NONE, C_LEFT, C_RIGHT, C_DRAG, C_GAS, C_BRAKE, C_TURBO, C_HAND, C_LOOK, C_CAMERA, C_PAUSE, C_YES, C_NO,
       C_TAP, C_COUNT };

typedef struct { float x, y, r; int shown; } Button;          /* round, in window pixels */
typedef struct { int64_t id; int ctl, seen; float ax, ay; } Finger;   /* ax, ay: where a drag started (its centre) */

static Button B[C_COUNT];
static Finger F[PLAT_MAX_FINGERS];
static int g_nf;
static int g_active, g_mode, g_drag_mode, g_tilt_mode;
static int g_held[C_COUNT];
static int g_tapped, g_answer;
static double g_tap_until;                  /* a quick tap counts as held for a moment (the end screens poll per tick) */
static float g_steer_mid, g_pedal_mid;      /* the x between left / right and between brake / gas */
static float g_range;                       /* drag: pixels of travel for full lock */
static float g_zone_steer_x, g_zone_pedal_x, g_zone_y, g_zone_pedal_y;
static int g_drag_finger = -1;              /* index into F */
static float g_drag;                        /* -1..1 */
static float g_op;                          /* opacity 0..1 */
static int g_W, g_H;

/* ---- layout ------------------------------------------------------------------------------------------------------ */
/* a rectangle of the 640x480 overlay in window pixels (gfx_2d_begin(640, 480) letterboxes it keeping 4:3) */
static void hud_to_screen(float x, float y, float *sx, float *sy)
{
    float s = (float)g_W / 640 < (float)g_H / 480 ? (float)g_W / 640 : (float)g_H / 480;
    *sx = (g_W - 640 * s) * 0.5f + x * s;
    *sy = (g_H - 480 * s) * 0.5f + y * s;
}

/* the speed panel of the overlay (docs/HUD.md 4.3: its frame, the turbo and damage bars, the labels and speed) */
static int speed_panel_rect(float *x0, float *y0, float *x1, float *y1)
{
    static const int RECT[3] = { 52, 54, 3 }, POINT[4] = { 53, 55, 56, 7 };
    int any = 0, ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        if (!hud_item_rect(RECT[i], &x, &y, &w, &h)) continue;
        if (!any || x < ax0) ax0 = x;
        if (!any || y < ay0) ay0 = y;
        if (!any || x + w > ax1) ax1 = x + w;
        if (!any || y + h > ay1) ay1 = y + h;
        any = 1;
    }
    if (!any) return 0;
    for (int i = 0; i < 4; i++) {
        int x, y;
        hud_item_pos(POINT[i], &x, &y);
        if (x <= 0 && y <= 0) continue;
        if (x < ax0) ax0 = x;
        if (y < ay0) ay0 = y;
    }
    hud_to_screen((float)ax0, (float)ay0, x0, y0);
    hud_to_screen((float)ax1, (float)ay1, x1, y1);
    return 1;
}

static void place(int c, float x, float y, float r, int shown)
{
    B[c].x = x; B[c].y = y; B[c].r = r; B[c].shown = shown;
}

static void layout(int speed_panel)
{
    const PadTouchConfig *cfg = pad_touch_config();
    plat_size(&g_W, &g_H);
    float W = (float)g_W, H = (float)g_H;
    float U = (W < H ? W : H) * cfg->size * 0.01f;
    float R = 0.12f * U, r2 = 0.085f * U, r = 0.07f * U, m = 0.035f * U, g = 0.03f * U;
    int race = g_mode == TOUCH_RACE, quit = g_mode == TOUCH_QUIT;
    g_drag_mode = cfg->drag;
    g_tilt_mode = cfg->tilt;
    g_op = cfg->opacity * 0.01f;
    memset(B, 0, sizeof B);
    /* gas / brake in the bottom right corner, turbo / handbrake above them. Where the speed panel of the 4:3 overlay
     * is in the way: smaller (down to 70 %) beside it on a wide screen, else left of it, small enough to stay in
     * the right half (down to 55 %) */
    float xr = W - m, yb = H - m, k = 1, cw = 4 * R + g, ch = 2 * R + g + 2 * r2;
    float px0, py0, px1, py1;
    if (speed_panel && speed_panel_rect(&px0, &py0, &px1, &py1) && xr - cw < px1 && xr > px0 && yb - ch < py1 && yb > py0) {
        k = (xr - px1 - 0.5f * m) / cw;
        if (k < 0.7f) {
            xr = px0 - 0.5f * m;
            k = (xr - W * 0.5f) / cw;
            if (k < 0.55f) k = 0.55f;
        }
        if (k > 1) k = 1;
    }
    float Rk = R * k, r2k = r2 * k, gk = g * k;
    place(C_GAS, xr - Rk, yb - Rk, Rk, race);
    place(C_BRAKE, xr - 3 * Rk - gk, yb - Rk, Rk, race);
    place(C_TURBO, B[C_GAS].x, B[C_GAS].y - Rk - gk - r2k, r2k, race);
    place(C_HAND, B[C_BRAKE].x, B[C_TURBO].y, r2k, race);
    /* steering in the bottom left corner, look back above it */
    int steer = race && !g_tilt_mode;
    place(C_LEFT, m + R, yb - R, R, steer && !g_drag_mode);
    place(C_RIGHT, m + 3 * R + g, yb - R, R, steer && !g_drag_mode);
    place(C_DRAG, m + 2 * R + g * 0.5f, yb - R, R, steer && g_drag_mode);
    place(C_LOOK, B[C_LEFT].x, B[C_LEFT].y - R - g - r2, r2, race);
    /* pause and camera in the top left corner (the overlay's panels are on the right) */
    place(C_PAUSE, m + r, m + r, r, race || quit);
    place(C_CAMERA, m + 3 * r + g, m + r, r, race);
    /* the quit question: yes / no under the dialog box */
    float cx = W * 0.5f, y = H * 0.62f;
    int dx, dy, dw, dh;
    if (hud_item_rect(34, &dx, &dy, &dw, &dh)) {
        float sx0, sy0, sx1, sy1;
        hud_to_screen((float)dx, (float)dy, &sx0, &sy0);
        hud_to_screen((float)(dx + dw), (float)(dy + dh), &sx1, &sy1);
        cx = (sx0 + sx1) * 0.5f;
        y = sy1 + g + R;
    }
    if (y > H - m - R) y = H - m - R;
    place(C_YES, cx - R - 2 * g, y, R, quit);
    place(C_NO, cx + R + 2 * g, y, R, quit);
    g_steer_mid = (B[C_LEFT].x + B[C_RIGHT].x) * 0.5f;
    g_pedal_mid = (B[C_BRAKE].x + B[C_GAS].x) * 0.5f;
    g_range = 1.5f * R;
    /* the zones a finger may land in beside the buttons: the steering below the look-back button on the left, the
     * pedals below turbo / handbrake on the right */
    g_zone_steer_x = W * 0.42f;
    if (g_zone_steer_x < B[C_RIGHT].x + 1.5f * R) g_zone_steer_x = B[C_RIGHT].x + 1.5f * R;
    g_zone_y = B[C_LOOK].y + r2 * 1.2f;
    g_zone_pedal_x = B[C_BRAKE].x - 1.5f * Rk;
    if (g_zone_pedal_x < W * 0.5f) g_zone_pedal_x = W * 0.5f;
    g_zone_pedal_y = B[C_TURBO].y + r2k * 1.2f;
}

/* the control a finger went down on */
static int hit(float x, float y)
{
    if (g_mode == TOUCH_PAUSE || g_mode == TOUCH_END) return C_TAP;
    int best = C_NONE;
    float bd = 1.3f;                                 /* a little beside a button still counts */
    for (int c = C_LEFT; c < C_TAP; c++) {
        if (!B[c].shown || c == C_DRAG) continue;
        float d = hypotf(x - B[c].x, y - B[c].y) / B[c].r;
        if (d < bd) { bd = d; best = c; }
    }
    if (best != C_NONE || g_mode != TOUCH_RACE) return best;
    if (x < g_zone_steer_x && y > g_zone_y && B[C_DRAG].shown) return C_DRAG;
    if (x < g_zone_steer_x && y > g_zone_y && B[C_LEFT].shown) return x < g_steer_mid ? C_LEFT : C_RIGHT;
    if (x > g_zone_pedal_x && y > g_zone_pedal_y) return x < g_pedal_mid ? C_BRAKE : C_GAS;
    return C_NONE;
}

/* ---- per frame ----------------------------------------------------------------------------------------------------- */
static float tilt_steer(void)
{
    float a[3];
    if (!plat_accel(a)) return 0;
    /* the device is held in landscape like a wheel: gravity lies along its x axis (either way round, Android turns
     * the picture); turning it moves gravity towards its y axis */
    float gx = a[0], gy = a[1];
    if (hypotf(gx, gy) < 3.0f) return 0;            /* lying flat: no steering */
    float s = gx >= 0 ? 1.0f : -1.0f;
    float deg = atan2f(s * gy, s * gx) * 57.29578f * (float)g_tilt_mode;
    float m = fabsf(deg) - 2.0f;                     /* a small dead zone */
    if (m <= 0) return 0;
    m /= (float)pad_touch_config()->tilt_angle;
    if (m > 1) m = 1;
    return deg < 0 ? -m : m;
}

void touch_frame(int mode, int speed_panel)
{
    const PadTouchConfig *cfg = pad_touch_config();
    float dig[PA_COUNT], ana[PA_COUNT];
    memset(dig, 0, sizeof dig);
    memset(ana, 0, sizeof ana);
    memset(g_held, 0, sizeof g_held);
    g_tapped = 0;
    g_answer = -1;
    g_mode = mode;
    g_active = cfg->show == TOUCH_SHOW_ON || (cfg->show == TOUCH_SHOW_AUTO && plat_input_kind() == PLAT_INPUT_TOUCH);
    layout(speed_panel);
    const PlatFinger *pf;
    int n = plat_fingers(&pf);
    for (int k = 0; k < g_nf; k++) F[k].seen = 0;
    for (int i = 0; i < n; i++) {
        int k;
        for (k = 0; k < g_nf; k++)
            if (F[k].id == pf[i].id) break;
        if (k == g_nf) {                             /* new: the control under it (one held since before: none) */
            if (g_nf == PLAT_MAX_FINGERS) continue;
            g_nf++;
            F[k].id = pf[i].id;
            F[k].ctl = pf[i].fresh && g_active ? hit(pf[i].x, pf[i].y) : C_NONE;
            F[k].ax = pf[i].x;
            F[k].ay = pf[i].y;
            if (F[k].ctl == C_TAP) { g_tapped = 1; g_tap_until = plat_time() + 0.15; }
            if (F[k].ctl == C_YES) g_answer = 1;
            if (F[k].ctl == C_NO) g_answer = 0;
        }
        Finger *f = &F[k];
        f->seen = 1;
        if (f->ctl == C_LEFT || f->ctl == C_RIGHT) f->ctl = pf[i].x < g_steer_mid ? C_LEFT : C_RIGHT;   /* slides */
        if (f->ctl == C_GAS || f->ctl == C_BRAKE) f->ctl = pf[i].x < g_pedal_mid ? C_BRAKE : C_GAS;
        if (f->ctl == C_DRAG) {                      /* the centre follows a thumb that goes past full lock */
            if (pf[i].x - f->ax > g_range) f->ax = pf[i].x - g_range;
            if (pf[i].x - f->ax < -g_range) f->ax = pf[i].x + g_range;
        }
        if (pf[i].down || pf[i].fresh) g_held[f->ctl] = 1;          /* lifted since the last frame: let go */
    }
    {   /* the fingers that left */
        int m = 0;
        for (int k = 0; k < g_nf; k++)
            if (F[k].seen) F[m++] = F[k];
        g_nf = m;
    }
    g_drag_finger = -1;
    g_drag = 0;
    for (int k = 0; k < g_nf && g_drag_finger < 0; k++)
        if (F[k].ctl == C_DRAG)
            for (int i = 0; i < n; i++)
                if (pf[i].id == F[k].id && (pf[i].down || pf[i].fresh)) {
                    g_drag_finger = k;
                    float s = (pf[i].x - F[k].ax) / g_range, d = fabsf(s) - 0.04f;   /* a small dead zone */
                    g_drag = d <= 0 ? 0 : (s < 0 ? -d : d) / 0.96f;
                }
    if (!g_active) { g_tapped = 0; g_answer = -1; pad_set_touch(dig, ana); return; }
    static const int MAP[][2] = { { C_LEFT, PA_STEER_LEFT }, { C_RIGHT, PA_STEER_RIGHT }, { C_GAS, PA_GAS },
                                  { C_BRAKE, PA_BRAKE }, { C_TURBO, PA_TURBO }, { C_HAND, PA_HANDBRAKE },
                                  { C_LOOK, PA_LOOK_BACK }, { C_CAMERA, PA_CAMERA }, { C_PAUSE, PA_PAUSE } };
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        if (g_held[MAP[i][0]]) dig[MAP[i][1]] = 1;
    float st = g_drag;
    if (g_drag_finger < 0 && !g_held[C_LEFT] && !g_held[C_RIGHT] && g_tilt_mode && mode == TOUCH_RACE) st = tilt_steer();
    if (st > 0) ana[PA_STEER_RIGHT] = st;
    if (st < 0) ana[PA_STEER_LEFT] = -st;
    pad_set_touch(dig, ana);
}

void touch_end(void)
{
    pad_set_touch(NULL, NULL);
    g_nf = 0;
    g_active = 0;
    g_tap_until = 0;
}

int touch_tapped(void) { return g_tapped; }
int touch_answer(void) { return g_answer; }

int touch_held(void)
{
    if (!g_active) return 0;
    if (g_held[C_TAP] || plat_time() < g_tap_until) return 1;
    return 0;
}

/* ---- drawing: simple shapes in one batch of triangles ---------------------------------------------------------- */
#define MAXV 9000
static GfxVertex g_v[MAXV];
static int g_nv;

static uint32_t col(int r, int g, int b, float a)
{
    int A = (int)(a * g_op * 255 + 0.5f);
    return (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | (uint32_t)(A > 255 ? 255 : A);
}

static void vtx(float x, float y, uint32_t c)
{
    if (g_nv >= MAXV) return;
    GfxVertex *v = &g_v[g_nv++];
    v->x = x; v->y = y; v->z = 0; v->u = 0; v->v = 0;
    v->r = (uint8_t)(c >> 24); v->g = (uint8_t)(c >> 16); v->b = (uint8_t)(c >> 8); v->a = (uint8_t)c;
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
    if (g_nv + 3 > MAXV) return;
    vtx(x0, y0, c); vtx(x1, y1, c); vtx(x2, y2, c);
}

static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c)
{
    tri(x0, y0, x1, y1, x2, y2, c);
    tri(x0, y0, x2, y2, x3, y3, c);
}

static void rect(float x, float y, float w, float h, uint32_t c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }

static void line(float x0, float y0, float x1, float y1, float w, uint32_t c)
{
    float dx = x1 - x0, dy = y1 - y0, l = hypotf(dx, dy);
    if (l <= 0) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

/* a ring between radii r0 and r1 from angle a0 to a1 (radians, y down); r0 = 0 is a filled disc */
static void arc(float cx, float cy, float r0, float r1, float a0, float a1, uint32_t c)
{
    int n = (int)(fabsf(a1 - a0) * 6) + 4;
    for (int i = 0; i < n; i++) {
        float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        if (r0 <= 0) tri(cx, cy, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, c);
        else quad(cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1,
                  cx + c1 * r0, cy + s1 * r0, c);
    }
}

#define TWO_PI_F 6.2831853f
static void disc(float cx, float cy, float r, uint32_t c) { arc(cx, cy, 0, r, 0, TWO_PI_F, c); }
static void ring(float cx, float cy, float r0, float r1, uint32_t c) { arc(cx, cy, r0, r1, 0, TWO_PI_F, c); }

static void icon(int c, float x, float y, float s, uint32_t w)
{
    float t = s * 0.13f;                             /* stroke width */
    switch (c) {
    case C_LEFT: tri(x - 0.45f * s, y, x + 0.3f * s, y + 0.45f * s, x + 0.3f * s, y - 0.45f * s, w); break;
    case C_RIGHT: tri(x + 0.45f * s, y, x - 0.3f * s, y - 0.45f * s, x - 0.3f * s, y + 0.45f * s, w); break;
    case C_GAS: tri(x, y - 0.45f * s, x + 0.45f * s, y + 0.3f * s, x - 0.45f * s, y + 0.3f * s, w); break;
    case C_BRAKE: tri(x, y + 0.45f * s, x - 0.45f * s, y - 0.3f * s, x + 0.45f * s, y - 0.3f * s, w); break;
    case C_TURBO:                                    /* two chevrons up */
        for (int i = 0; i < 2; i++) {
            float yy = y - 0.12f * s + i * 0.36f * s;
            line(x - 0.4f * s, yy + 0.2f * s, x, yy - 0.2f * s, t * 1.2f, w);
            line(x, yy - 0.2f * s, x + 0.4f * s, yy + 0.2f * s, t * 1.2f, w);
        }
        break;
    case C_HAND:                                     /* (P), the parking brake lamp */
        arc(x, y, 0.5f * s, 0.5f * s + t, TWO_PI_F * 0.33f, TWO_PI_F * 0.67f, w);
        arc(x, y, 0.5f * s, 0.5f * s + t, -TWO_PI_F * 0.17f, TWO_PI_F * 0.17f, w);
        rect(x - 0.17f * s, y - 0.3f * s, t, 0.6f * s, w);
        arc(x - 0.04f * s, y - 0.12f * s, 0.18f * s - t, 0.18f * s, -TWO_PI_F * 0.25f, TWO_PI_F * 0.25f, w);
        rect(x - 0.17f * s, y - 0.3f * s, 0.13f * s, t, w);
        rect(x - 0.17f * s, y + 0.06f * s - t, 0.13f * s, t, w);
        break;
    case C_LOOK:                                     /* a U-turn arrow */
        arc(x, y - 0.02f * s, 0.28f * s - t * 0.5f, 0.28f * s + t * 0.5f, TWO_PI_F * 0.5f, TWO_PI_F, w);
        line(x + 0.28f * s, y - 0.02f * s, x + 0.28f * s, y + 0.35f * s, t, w);
        tri(x - 0.28f * s - 0.2f * s, y - 0.04f * s, x - 0.28f * s + 0.2f * s, y - 0.04f * s, x - 0.28f * s, y + 0.36f * s, w);
        break;
    case C_CAMERA:
        rect(x - 0.42f * s, y - 0.22f * s, 0.84f * s, 0.52f * s, w);
        rect(x - 0.16f * s, y - 0.34f * s, 0.32f * s, 0.13f * s, w);
        disc(x, y + 0.04f * s, 0.17f * s, col(0, 0, 0, 0.8f));
        break;
    case C_PAUSE:
        rect(x - 0.26f * s, y - 0.3f * s, 0.18f * s, 0.6f * s, w);
        rect(x + 0.08f * s, y - 0.3f * s, 0.18f * s, 0.6f * s, w);
        break;
    case C_YES:
        line(x - 0.36f * s, y, x - 0.12f * s, y + 0.26f * s, t * 1.3f, w);
        line(x - 0.12f * s, y + 0.26f * s, x + 0.38f * s, y - 0.3f * s, t * 1.3f, w);
        break;
    case C_NO:
        line(x - 0.3f * s, y - 0.3f * s, x + 0.3f * s, y + 0.3f * s, t * 1.3f, w);
        line(x - 0.3f * s, y + 0.3f * s, x + 0.3f * s, y - 0.3f * s, t * 1.3f, w);
        break;
    default: break;
    }
}

static void button(int c, int r, int g, int b)
{
    const Button *k = &B[c];
    if (!k->shown) return;
    int on = g_held[c];
    disc(k->x, k->y, k->r, on ? col(r, g, b, 0.75f) : col(0, 0, 0, 0.35f));
    ring(k->x, k->y, k->r * (on ? 0.85f : 0.9f), k->r, col(r, g, b, on ? 1.0f : 0.9f));
    icon(c, k->x, k->y, k->r, col(255, 255, 255, on ? 1.0f : 0.85f));
}

/* the drag zone: a slider with its knob, where the thumb went down (at rest: in the corner) */
static void drag_slider(void)
{
    const Button *k = &B[C_DRAG];
    if (!k->shown) return;
    float R = k->r, cx = k->x, cy = k->y, kx = cx;
    int on = g_drag_finger >= 0;
    if (on) {
        cx = F[g_drag_finger].ax;
        cy = F[g_drag_finger].ay;
        kx = cx + g_drag * g_range;
    }
    float L = g_range, h = 0.42f * R;
    uint32_t fill = col(0, 0, 0, 0.35f), edge = col(230, 230, 230, 0.9f), white = col(255, 255, 255, 0.85f);
    rect(cx - L, cy - h, 2 * L, 2 * h, fill);
    arc(cx - L, cy, 0, h, TWO_PI_F * 0.25f, TWO_PI_F * 0.75f, fill);
    arc(cx + L, cy, 0, h, -TWO_PI_F * 0.25f, TWO_PI_F * 0.25f, fill);
    rect(cx - L, cy - h, 2 * L, 0.1f * h, edge);
    rect(cx - L, cy + 0.9f * h, 2 * L, 0.1f * h, edge);
    arc(cx - L, cy, 0.9f * h, h, TWO_PI_F * 0.25f, TWO_PI_F * 0.75f, edge);
    arc(cx + L, cy, 0.9f * h, h, -TWO_PI_F * 0.25f, TWO_PI_F * 0.25f, edge);
    float a = L + 0.25f * h, s = 0.5f * h;           /* the arrows at the ends */
    tri(cx - a - s * 0.6f, cy, cx - a + s * 0.5f, cy - s, cx - a + s * 0.5f, cy + s, white);
    tri(cx + a + s * 0.6f, cy, cx + a - s * 0.5f, cy + s, cx + a - s * 0.5f, cy - s, white);
    disc(kx, cy, 0.55f * R, on ? col(255, 255, 255, 0.6f) : col(255, 255, 255, 0.3f));
    ring(kx, cy, 0.5f * R, 0.55f * R, edge);
}

void touch_draw(void)
{
    if (!g_active || (g_mode != TOUCH_RACE && g_mode != TOUCH_QUIT)) return;
    g_nv = 0;
    drag_slider();
    button(C_LEFT, 230, 230, 230);
    button(C_RIGHT, 230, 230, 230);
    button(C_LOOK, 230, 230, 230);
    button(C_BRAKE, 240, 80, 70);
    button(C_GAS, 90, 220, 90);
    button(C_HAND, 230, 230, 230);
    button(C_TURBO, 255, 160, 40);
    button(C_PAUSE, 230, 230, 230);
    button(C_CAMERA, 230, 230, 230);
    button(C_YES, 90, 220, 90);
    button(C_NO, 240, 80, 70);
    if (!g_nv) return;
    gfx_2d_begin(g_W, g_H);                          /* window pixels */
    GfxState st = { BLEND_ALPHA, 0, 0, 0, 0, 0 };
    gfx_draw_tris(g_v, g_nv, gfx_white(), &st);
    gfx_2d_end();
}
