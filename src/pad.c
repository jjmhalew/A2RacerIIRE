/* pad.c - see pad.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "pad.h"
#include "ini.h"
#include "plat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* an element: a game controller's button or axis, or a raw joystick's button, axis or hat */
enum { B_NONE, B_GPBUTTON, B_GPAXIS, B_BUTTON, B_AXIS, B_HAT };
typedef struct { unsigned char src, idx; signed char dir; unsigned char inv; } Bind;   /* axis dir +1 / -1 / 0 whole */

typedef struct {
    Bind b[PA_COUNT][PAD_MAX_BINDS];
    int n[PA_COUNT];
    int stick_dz, trigger_dz, axis_dz;       /* percent */
    int steer_lin, steer_sens;               /* percent */
    int rumble, rumble_strength, wheel_spring;
} Config;
static Config C, g_backup;
static int g_loaded;

static const char *const KEY[PA_COUNT] = { "steer_left", "steer_right", "gas", "brake", "turbo", "handbrake",
                                           "look_back", "camera", "horn", "pause" };
static const char *const DEF[PA_COUNT] = { "LEFTX- DPLEFT", "LEFTX+ DPRIGHT", "RT A", "LT X", "Y RB", "B", "LB", "BACK",
                                           "LS", "START" };
static const char *const GPB[GP_NBUTTONS] = { "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LS", "RS", "LB", "RB",
                                              "DPUP", "DPDOWN", "DPLEFT", "DPRIGHT", "MISC1", "PADDLE1", "PADDLE2",
                                              "PADDLE3", "PADDLE4", "TOUCHPAD" };
static const char *const GPA[GP_NAXES] = { "LEFTX", "LEFTY", "RIGHTX", "RIGHTY", "LT", "RT" };
static const struct { const char *alias; int b; } ALIAS[] = {
    { "CROSS", GP_A }, { "CIRCLE", GP_B }, { "SQUARE", GP_X }, { "TRIANGLE", GP_Y }, { "L1", GP_LB }, { "R1", GP_RB },
    { "L3", GP_LS }, { "R3", GP_RS }, { "SHARE", GP_BACK }, { "CREATE", GP_BACK }, { "SELECT", GP_BACK },
    { "VIEW", GP_BACK }, { "OPTIONS", GP_START }, { "MENU", GP_START }, { "HOME", GP_GUIDE }, { "PS", GP_GUIDE } };
static const struct { const char *alias; int a; } AXALIAS[] = { { "L2", GP_LT }, { "R2", GP_RT }, { "ZL", GP_LT },
                                                                  { "ZR", GP_RT } };

static const char DEFAULT_FILE[] =
    "; controller.ini - A2RacerRE: the game controller mapping of the race. The keyboard keys are set in the game's\r\n"
    "; controls screen (Besturing); there \"Instellen\" with the joypad or wheel / joystick device chosen writes the\r\n"
    "; first element of the six driving actions here. Everything else is edited by hand; delete the file to get the\r\n"
    "; defaults back. The menus always use the d-pad or left stick, A (confirm), B (back) and Start.\r\n"
    ";\r\n"
    "; Every action takes one or more elements separated by spaces; all connected controllers work at the same time.\r\n"
    "; Game controllers (Xbox names by position: A is the bottom face button, Cross on a PlayStation pad):\r\n"
    ";   A B X Y LB RB LT RT LS RS BACK START GUIDE DPUP DPDOWN DPLEFT DPRIGHT MISC1 PADDLE1..PADDLE4 TOUCHPAD,\r\n"
    ";   also CROSS CIRCLE SQUARE TRIANGLE L1 R1 L2 R2 L3 R3 SHARE OPTIONS\r\n"
    ";   stick halves: LEFTX- LEFTX+ LEFTY- LEFTY+ RIGHTX- RIGHTX+ RIGHTY- RIGHTY+ (Y- is up)\r\n"
    "; Other joysticks: wheels, pedals, flight sticks (numbers from 0; the log lists what was found):\r\n"
    ";   axis0- axis0+ (one half of an axis), axis2 (the whole range: a pedal), button3,\r\n"
    ";   hat0up hat0down hat0left hat0right; a ~ in front inverts an axis (~axis2: a pedal that rests at the top)\r\n"
    "; Buttons and hats work like the keys (gas and steering build up as on the keyboard); sticks, triggers and\r\n"
    "; the axes of other joysticks are analogue.\r\n"
    "; A wheel, for example: steer_left=axis0-  steer_right=axis0+  gas=~axis1  brake=~axis2\r\n"
    "\r\n"
    "[race]\r\n"
    "steer_left=LEFTX- DPLEFT\r\n"
    "steer_right=LEFTX+ DPRIGHT\r\n"
    "gas=RT A\r\n"
    "brake=LT X\r\n"
    "turbo=Y RB\r\n"
    "handbrake=B\r\n"
    "look_back=LB\r\n"
    "camera=BACK\r\n"
    "horn=LS\r\n"
    "pause=START\r\n"
    "\r\n"
    "[tuning]\r\n"
    "; dead zones in percent: the sticks, the triggers and whole-axis pedals, the other joystick axes (wheels)\r\n"
    "stick_deadzone=15\r\n"
    "trigger_deadzone=5\r\n"
    "axis_deadzone=3\r\n"
    "; analogue steering: linearity 100 = straight (as the original's analogue device), 200 = finer near the\r\n"
    "; centre, 50 = sharper. With the wheel / joystick device chosen in the menu its Gevoeligheid slider shapes the\r\n"
    "; steering instead (the original's curve). Sensitivity in percent multiplies either: 100 = full lock at full\r\n"
    "; deflection, 150 = at two thirds\r\n"
    "steer_linearity=100\r\n"
    "steer_sensitivity=100\r\n"
    "; rumble on collisions and with the turbo: 1 on, 0 off; its strength in percent\r\n"
    "rumble=1\r\n"
    "rumble_strength=100\r\n"
    "; the centring spring of a force-feedback wheel during the race in percent, 0 = off\r\n"
    "wheel_spring=40\r\n";

/* ---- element names ------------------------------------------------------------------------------------------------ */
static int parse_bind(const char *tok, Bind *b)
{
    memset(b, 0, sizeof *b);
    if (*tok == '~') { b->inv = 1; tok++; }
    char t[32];
    size_t n = strlen(tok);
    if (n == 0 || n >= sizeof t) return 0;
    memcpy(t, tok, n + 1);
    char last = t[n - 1];
    int sign = last == '+' ? 1 : last == '-' ? -1 : 0;
    if (!_strnicmp(t, "axis", 4) && t[4] >= '0' && t[4] <= '9') {
        int i = atoi(t + 4);
        if (i >= PLAT_DEV_AXES) return 0;
        b->src = B_AXIS; b->idx = (unsigned char)i; b->dir = (signed char)sign;
        return 1;
    }
    if (!_strnicmp(t, "button", 6) && t[6] >= '0' && t[6] <= '9') {
        int i = atoi(t + 6);
        if (i >= PLAT_DEV_BUTTONS) return 0;
        b->src = B_BUTTON; b->idx = (unsigned char)i;
        return 1;
    }
    if (!_strnicmp(t, "hat", 3) && t[3] >= '0' && t[3] <= '9') {
        int i = atoi(t + 3);
        const char *d = t + 4;
        while (*d >= '0' && *d <= '9') d++;
        int bit = !_stricmp(d, "up") ? 1 : !_stricmp(d, "right") ? 2 : !_stricmp(d, "down") ? 4 : !_stricmp(d, "left") ? 8 : 0;
        if (!bit || i >= PLAT_DEV_HATS) return 0;
        b->src = B_HAT; b->idx = (unsigned char)i; b->dir = (signed char)bit;
        return 1;
    }
    if (sign) t[n - 1] = 0;
    for (int a = 0; a < GP_NAXES; a++)
        if (!_stricmp(t, GPA[a])) {
            b->src = B_GPAXIS; b->idx = (unsigned char)a;
            b->dir = (signed char)(a >= GP_LT ? 1 : sign);
            return 1;
        }
    for (size_t k = 0; k < sizeof AXALIAS / sizeof AXALIAS[0]; k++)
        if (!_stricmp(t, AXALIAS[k].alias)) { b->src = B_GPAXIS; b->idx = (unsigned char)AXALIAS[k].a; b->dir = 1; return 1; }
    if (sign) return 0;
    for (int i = 0; i < GP_NBUTTONS; i++)
        if (!_stricmp(t, GPB[i])) { b->src = B_GPBUTTON; b->idx = (unsigned char)i; return 1; }
    for (size_t k = 0; k < sizeof ALIAS / sizeof ALIAS[0]; k++)
        if (!_stricmp(t, ALIAS[k].alias)) { b->src = B_GPBUTTON; b->idx = (unsigned char)ALIAS[k].b; return 1; }
    return 0;
}

static const char *bind_str(const Bind *b)
{
    static char buf[4][32];
    static int r;
    char *s = buf[r++ & 3];
    const char *inv = b->inv ? "~" : "";
    const char *sg = b->dir > 0 ? "+" : b->dir < 0 ? "-" : "";
    switch (b->src) {
    case B_GPBUTTON: snprintf(s, 32, "%s", GPB[b->idx]); break;
    case B_GPAXIS: snprintf(s, 32, "%s%s%s", inv, GPA[b->idx], b->idx >= GP_LT ? "" : sg); break;
    case B_BUTTON: snprintf(s, 32, "button%d", b->idx); break;
    case B_AXIS: snprintf(s, 32, "%saxis%d%s", inv, b->idx, sg); break;
    case B_HAT: snprintf(s, 32, "hat%d%s", b->idx, b->dir == 1 ? "up" : b->dir == 2 ? "right" : b->dir == 4 ? "down" : "left"); break;
    default: s[0] = 0; break;
    }
    return s;
}

static int bind_eq(const Bind *a, const Bind *b)
{
    return a->src == b->src && a->idx == b->idx && a->dir == b->dir && a->inv == b->inv;
}

static void parse_list(int action, const char *v)
{
    C.n[action] = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", v);
    char *semi = strchr(buf, ';');                   /* a trailing comment */
    if (semi) *semi = 0;
    for (char *t = strtok(buf, " \t,"); t; t = strtok(NULL, " \t,")) {
        Bind b;
        if (!parse_bind(t, &b)) { fprintf(stderr, "controller.ini: %s: unknown element %s\n", KEY[action], t); continue; }
        if (C.n[action] < PAD_MAX_BINDS) C.b[action][C.n[action]++] = b;
    }
}

/* ---- the file --------------------------------------------------------------------------------------------------- */
static void ini_path(char *p, size_t cap) { snprintf(p, cap, "%scontroller.ini", plat_user_dir()); }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void pad_load(void)
{
    char p[1024];
    ini_path(p, sizeof p);
    if (!plat_exists(p)) {
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(DEFAULT_FILE, 1, sizeof DEFAULT_FILE - 1, f); fclose(f); printf("%s written\n", p); }
    }
    Ini *g = ini_load(p);
    for (int a = 0; a < PA_COUNT; a++) {
        const char *v = ini_get(g, "race", KEY[a]);
        char buf[256];
        snprintf(buf, sizeof buf, "%s", v ? v : DEF[a]);
        parse_list(a, buf);
    }
    C.stick_dz = clampi(ini_int(g, "tuning", "stick_deadzone", 15), 0, 95);
    C.trigger_dz = clampi(ini_int(g, "tuning", "trigger_deadzone", 5), 0, 95);
    C.axis_dz = clampi(ini_int(g, "tuning", "axis_deadzone", 3), 0, 95);
    C.steer_lin = clampi(ini_int(g, "tuning", "steer_linearity", 100), 20, 500);
    C.steer_sens = clampi(ini_int(g, "tuning", "steer_sensitivity", 100), 10, 500);
    C.rumble = ini_int(g, "tuning", "rumble", 1) != 0;
    C.rumble_strength = clampi(ini_int(g, "tuning", "rumble_strength", 100), 0, 100);
    C.wheel_spring = clampi(ini_int(g, "tuning", "wheel_spring", 40), 0, 100);
    ini_free(g);
    g_loaded = 1;
}

static void ensure(void) { if (!g_loaded) pad_load(); }

int pad_save(void)
{
    ensure();
    char p[1024];
    ini_path(p, sizeof p);
    if (!plat_exists(p)) {
        FILE *f = fopen(p, "wb");
        if (f) { fwrite(DEFAULT_FILE, 1, sizeof DEFAULT_FILE - 1, f); fclose(f); }
    }
    Ini *g = ini_load(p);
    for (int a = 0; a < PA_COUNT; a++) {
        char v[256] = "";
        for (int i = 0; i < C.n[a]; i++) {
            size_t n = strlen(v);
            snprintf(v + n, sizeof v - n, "%s%s", i ? " " : "", bind_str(&C.b[a][i]));
        }
        ini_set(g, "race", KEY[a], v);
    }
    int ok = ini_save(g, p);
    ini_free(g);
    return ok;
}

const char *pad_action_key(int action) { return action >= 0 && action < PA_COUNT ? KEY[action] : ""; }

/* ---- reading ------------------------------------------------------------------------------------------------------ */
static int is_digital(const Bind *b) { return b->src == B_GPBUTTON || b->src == B_BUTTON || b->src == B_HAT; }

static float dead(float v, int pct)
{
    float z = pct * 0.01f;
    if (v <= z) return 0;
    v = (v - z) / (1 - z);
    return v > 1 ? 1 : v;
}

static float bind_value(const PlatDevice *d, const Bind *b, int prev)
{
    const float *ax = prev ? d->prev_axis : d->axis;
    uint32_t bt = prev ? d->prev_buttons : d->buttons;
    switch (b->src) {
    case B_GPBUTTON: return d->gamepad ? (float)((bt >> b->idx) & 1) : 0;
    case B_BUTTON: return !d->gamepad && b->idx < d->nbuttons ? (float)((bt >> b->idx) & 1) : 0;
    case B_HAT: return !d->gamepad && b->idx < d->nhats && ((prev ? d->prev_hat : d->hat)[b->idx] & b->dir) ? 1.0f : 0;
    case B_GPAXIS: {
        if (!d->gamepad) return 0;
        float v = ax[b->idx];
        if (b->idx >= GP_LT) return dead(b->inv ? 1 - v : v, C.trigger_dz);
        if (b->inv) v = -v;
        return b->dir ? dead(v * b->dir, C.stick_dz) : dead((v + 1) * 0.5f, C.trigger_dz);
    }
    case B_AXIS: {
        if (d->gamepad || b->idx >= d->naxes) return 0;
        float v = b->inv ? -ax[b->idx] : ax[b->idx];
        return b->dir ? dead(v * b->dir, C.axis_dz) : dead((v + 1) * 0.5f, C.trigger_dz);
    }
    default: return 0;
    }
}

/* the strongest element of an action: kind 1 = digital ones, 2 = analogue ones */
static float action_value(int a, int kind, int prev)
{
    float best = 0;
    if (a < 0 || a >= PA_COUNT) return 0;
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        const PlatDevice *d = plat_device(i);
        if (!d) continue;
        for (int k = 0; k < C.n[a]; k++) {
            const Bind *b = &C.b[a][k];
            if (is_digital(b) != (kind == 1)) continue;
            float v = bind_value(d, b, prev);
            if (v > best) best = v;
        }
    }
    return best;
}

int pad_digital(int a) { ensure(); return action_value(a, 1, 0) > 0.5f; }
float pad_analog(int a) { ensure(); return action_value(a, 2, 0); }
static int held(int a, int prev) { return action_value(a, 1, prev) > 0.5f || action_value(a, 2, prev) > 0.5f; }
int pad_held(int a) { ensure(); return held(a, 0); }
int pad_pressed(int a) { ensure(); return held(a, 0) && !held(a, 1); }

/* the original's SENSITIVITY curve (init_player_car 0x4256a0, INPUT.md 6): deflection 0..30 -> steering 0..30 */
static int g_curve[31], g_curve_on;

void pad_set_steer_curve(int sensitivity)
{
    g_curve_on = sensitivity >= 0;
    if (!g_curve_on) return;
    if (sensitivity > 100) sensitivity = 100;
    int k =(100 - sensitivity) * 3 / 100 + 1;
    for (int i = 0; i <= 30; i++) {
        double t = pow(2.7182817459106445, i * 0.033333335f);
        g_curve[i] = (int)(30.0f * (float)pow((float)((t - 1.0f) * 0.58197701f), (float)k));
    }
}

float pad_steer(void)
{
    ensure();
    float s = action_value(PA_STEER_RIGHT, 2, 0) - action_value(PA_STEER_LEFT, 2, 0);
    float m = fabsf(s);
    if (m <= 0) return 0;
    if (m > 1) m = 1;
    if (g_curve_on) {                               /* the table, between its integer steps */
        float x = m * 30;
        int i = (int)x;
        float c = i >= 30 ? (float)g_curve[30] : g_curve[i] + (g_curve[i + 1] - g_curve[i]) * (x - i);
        m = c / 30.0f;
    } else m = powf(m, C.steer_lin * 0.01f);
    m *= C.steer_sens * 0.01f;
    if (m > 1) m = 1;
    return s < 0 ? -m : m;
}

void pad_rumble(float strength, int ms)
{
    ensure();
    if (!C.rumble || C.rumble_strength <= 0 || strength <= 0) return;
    float s = strength * C.rumble_strength * 0.01f;
    if (s > 1) s = 1;
    static double until;
    static float cur;
    double now = plat_time();
    if (now < until && s < cur) return;             /* a weaker buzz does not cut a crash short */
    cur = s;
    until = now + ms * 0.001;
    plat_rumble(s, s * 0.6f, ms);
}

void pad_race_active(int on)
{
    ensure();
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        const PlatDevice *d = plat_device(i);
        if (d && d->can_spring) plat_device_spring(i, on ? C.wheel_spring * 0.01f : 0);
    }
}

/* ---- the controls screen ---------------------------------------------------------------------------------------- */
const char *pad_bind_name(int a, int i)
{
    ensure();
    if (a < 0 || a >= PA_COUNT || i < 0 || i >= C.n[a]) return NULL;
    return bind_str(&C.b[a][i]);
}

int pad_display_kind(void)
{
    const PlatDevice *d = plat_device(plat_last_device());
    return d ? d->kind : PADKIND_XBOX;
}

/* short, in the menu's Dutch, as printed on the pad */
const char *pad_display_name(const char *element, int kind)
{
    static char s[32];
    Bind b;
    if (!element || !parse_bind(element, &b)) return "";
    static const char *const DIR[4] = { "links", "rechts", "op", "neer" };
    switch (b.src) {
    case B_GPBUTTON: {
        static const char *const PS[GP_NBUTTONS] = { "Kruis", "Rondje", "Vierkant", "Driehoek", "Share", "PS", "Options",
                                                     "L3", "R3", "L1", "R1", 0, 0, 0, 0, "Mic", 0, 0, 0, 0, "Touchpad" };
        static const char *const NS[GP_NBUTTONS] = { "A", "B", "X", "Y", "-", "Home", "+", "LS klik", "RS klik", "L",
                                                     "R", 0, 0, 0, 0, "Capture" };
        static const char *const ST[GP_NBUTTONS] = { "A", "B", "X", "Y", "View", "Steam", "Menu", "L3", "R3", "L1",
                                                     "R1", 0, 0, 0, 0, "...", "R4", "L4", "R5", "L5" };
        static const char *const XB[GP_NBUTTONS] = { "A", "B", "X", "Y", "Back", "Guide", "Start", "LS klik", "RS klik",
                                                     "LB", "RB", 0, 0, 0, 0, "Share", "P1", "P2", "P3", "P4", "Touchpad" };
        if (b.idx >= GP_DPUP && b.idx <= GP_DPRIGHT) {   /* the panel has room for 9 letters */
            static const int D[4] = { 2, 3, 0, 1 };
            snprintf(s, sizeof s, "D-%s", DIR[D[b.idx - GP_DPUP]]);
            return s;
        }
        const char *const *t = kind == PADKIND_PS ? PS : kind == PADKIND_SWITCH ? NS : kind == PADKIND_STEAM ? ST : XB;
        snprintf(s, sizeof s, "%s", t[b.idx] ? t[b.idx] : GPB[b.idx]);
        return s;
    }
    case B_GPAXIS:
        if (b.idx >= GP_LT) {
            static const char *const T[4][2] = { { "LT", "RT" }, { "L2", "R2" }, { "ZL", "ZR" }, { "L2", "R2" } };
            int k = kind == PADKIND_PS ? 1 : kind == PADKIND_SWITCH ? 2 : kind == PADKIND_STEAM ? 3 : 0;
            snprintf(s, sizeof s, "%s%s", T[k][b.idx - GP_LT], b.inv ? " inv" : "");
        } else if (b.dir) {
            int x = b.idx == GP_LEFTX || b.idx == GP_RIGHTX;
            int neg = (b.dir < 0) ^ b.inv;
            int d = x ? (neg ? 0 : 1) : (neg ? 2 : 3);
            snprintf(s, sizeof s, "%s %s", b.idx <= GP_LEFTY ? "LS" : "RS", DIR[d]);
        } else snprintf(s, sizeof s, "%s%s", GPA[b.idx], b.inv ? " inv" : "");
        return s;
    case B_BUTTON: snprintf(s, sizeof s, "Knop %d", b.idx + 1); return s;
    case B_AXIS:
        snprintf(s, sizeof s, "As %d%s%s", b.idx + 1, b.dir > 0 ? "+" : b.dir < 0 ? "-" : "", b.inv ? " inv" : "");
        return s;
    case B_HAT: snprintf(s, sizeof s, "H%d-%s", b.idx + 1, b.dir == 1 ? "op" : b.dir == 2 ? "rechts" : b.dir == 4 ? "neer" : "links");
        return s;
    default: return "";
    }
}

static PlatDevice g_rest[PLAT_MAX_DEVICES];
static int g_rest_ok[PLAT_MAX_DEVICES], g_wait_release;

void pad_capture_begin(void)
{
    ensure();
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        const PlatDevice *d = plat_device(i);
        g_rest_ok[i] = d != NULL;
        if (d) g_rest[i] = *d;
        if (d && d->gamepad)                         /* a pad's sticks and triggers rest at 0 */
            for (int a = 0; a < GP_NAXES; a++) g_rest[i].axis[a] = 0;
    }
    g_wait_release = 1;                             /* the button that chose Instellen first */
}

/* everything back at rest: no button or hat, every axis near where it was at the start */
static int all_released(void)
{
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        const PlatDevice *d = plat_device(i);
        if (!d || !g_rest_ok[i]) continue;
        if (d->buttons) return 0;
        for (int h = 0; h < d->nhats; h++)
            if (d->hat[h]) return 0;
        for (int a = 0; a < d->naxes; a++)
            if (fabsf(d->axis[a] - g_rest[i].axis[a]) > 0.3f) return 0;
    }
    return 1;
}

int pad_capture(char *out, size_t cap)
{
    ensure();
    if (g_wait_release) {
        if (!all_released()) return 0;
        g_wait_release = 0;
    }
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        const PlatDevice *d = plat_device(i);
        if (!d) { g_rest_ok[i] = 0; continue; }
        if (!g_rest_ok[i]) { g_rest[i] = *d; g_rest_ok[i] = 1; continue; }     /* plugged in just now */
        Bind b;
        memset(&b, 0, sizeof b);
        uint32_t nb = d->buttons & ~d->prev_buttons;
        if (d->gamepad) nb &= ~(1u << GP_START);    /* Start breaks off (and stays the pause button) */
        for (int k = 0; k < 32 && !b.src; k++)
            if (nb & (1u << k)) { b.src = d->gamepad ? B_GPBUTTON : B_BUTTON; b.idx = (unsigned char)k; }
        for (int h = 0; h < d->nhats && !b.src; h++) {
            int nh = d->hat[h] & ~d->prev_hat[h];
            static const int ORDER[4] = { 1, 2, 4, 8 };
            for (int k = 0; k < 4 && !b.src; k++)
                if (nh & ORDER[k]) { b.src = B_HAT; b.idx = (unsigned char)h; b.dir = (signed char)ORDER[k]; }
        }
        for (int a = 0; a < d->naxes && !b.src; a++) {
            float v = d->axis[a], r = g_rest[i].axis[a];
            if (d->gamepad) {
                if (a >= GP_LT) { if (v > 0.6f) { b.src = B_GPAXIS; b.idx = (unsigned char)a; b.dir = 1; } }
                else if (fabsf(v) > 0.6f) { b.src = B_GPAXIS; b.idx = (unsigned char)a; b.dir = v > 0 ? 1 : -1; }
            } else if (fabsf(r) > 0.75f) {             /* a pedal resting at one end: its whole range */
                if (fabsf(v - r) > 1.0f) { b.src = B_AXIS; b.idx = (unsigned char)a; b.dir = 0; b.inv = r > 0; }
            } else if (fabsf(v - r) > 0.6f) { b.src = B_AXIS; b.idx = (unsigned char)a; b.dir = v > r ? 1 : -1; }
        }
        if (b.src) {
            snprintf(out, cap, "%s", bind_str(&b));
            g_wait_release = 1;
            return 1;
        }
    }
    return 0;
}

void pad_set_primary(int action, const char *element)
{
    ensure();
    Bind nb;
    if (action < 0 || action >= PA_COUNT || !parse_bind(element, &nb)) return;
    for (int k = 1; k < C.n[action]; k++)           /* one of its own: to the front, the old first one takes its place */
        if (bind_eq(&C.b[action][k], &nb)) { C.b[action][k] = C.b[action][0]; C.b[action][0] = nb; break; }
    for (int a = 0; a < PA_COUNT; a++)              /* an element does one thing */
        for (int k = 0; k < C.n[a];)
            if (bind_eq(&C.b[a][k], &nb) && !(a == action && k == 0)) {
                memmove(&C.b[a][k], &C.b[a][k + 1], sizeof(Bind) * (size_t)(C.n[a] - k - 1));
                C.n[a]--;
            } else k++;
    if (C.n[action] == 0) C.n[action] = 1;
    C.b[action][0] = nb;
}

void pad_swap_primary(int a, int b)
{
    ensure();
    if (a < 0 || b < 0 || a >= PA_COUNT || b >= PA_COUNT || !C.n[a] || !C.n[b]) return;
    Bind t = C.b[a][0], u = C.b[b][0];
    C.b[a][0] = u;
    C.b[b][0] = t;
    for (int x = 0; x < PA_COUNT; x++)              /* the two elements now do only these */
        for (int k = 0; k < C.n[x];) {
            const Bind *e = &C.b[x][k];
            if (!(k == 0 && (x == a || x == b)) && (bind_eq(e, &t) || bind_eq(e, &u))) {
                memmove(&C.b[x][k], &C.b[x][k + 1], sizeof(Bind) * (size_t)(C.n[x] - k - 1));
                C.n[x]--;
            } else k++;
        }
}

void pad_backup(void) { ensure(); g_backup = C; }
void pad_restore(void) { C = g_backup; }
