/* plat_sdl.c - plat.h on SDL2: window and GL context (desktop OpenGL 2.1, or OpenGL ES 2.0 on Android), keyboard as
 * Windows VK codes, every game controller and joystick (hot-plugged, rumble, wheel springs), mouse, the fingers of a
 * touch screen (also as SDL's emulated mouse, outside the race), the accelerometer, the audio device, time and
 * case-insensitive file lookup.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#define SDL_MAIN_HANDLED
#include "plat.h"
#include <SDL.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#endif

static SDL_Window *g_win;
static SDL_GLContext g_gl;
static int g_gles;
static int g_vsync, g_gl_lost;
static double g_test_glreset;    /* A2_TEST_GLRESET=T: the context is lost T seconds after the start (tests) */
static unsigned char g_keys[256], g_down[256];
static char g_text[64];
static PlatPad g_pad;
typedef struct {
    int used;
    SDL_GameController *gc;                 /* NULL = a raw joystick */
    SDL_Joystick *joy;                      /* the joystick (of the game controller too) */
    SDL_JoystickID id;
    SDL_Haptic *haptic;                     /* raw joysticks with force feedback (wheels) */
    int spring_fx, spring_on, haptic_rumble;
    float spring;
    PlatDevice d;
} Dev;
static Dev g_dev[PLAT_MAX_DEVICES];
static int g_last_dev = -1;
static void test_pad_init(void);
static void test_pad_step(void);
static SDL_AudioDeviceID g_audio;
static int g_audio_rate = 44100;
static PlatAudioFn g_audio_fn;
static void *g_audio_user;
static int g_mx, g_my;
static unsigned g_mb, g_mclick;
static Uint64 g_t0;
static PlatFinger g_fing[PLAT_MAX_FINGERS];
static int g_nfing, g_input_kind, g_touch_capture;
static void test_touch_init(void);
static void test_touch_step(void);
static int g_test_accel;                  /* A2_TEST_TOUCH set the accelerometer */
static float g_test_accel_v[3];

/* ---- keys: SDL scancode -> Windows VK (position based, letters and digits as on a US layout) -------------------- */
static int sc_to_vk(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return VK_NUMPAD0 + 1 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_AC_BACK: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    case SDL_SCANCODE_PAUSE: return VK_PAUSE;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    case SDL_SCANCODE_KP_PLUS: return VK_ADD;
    case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
    case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
    case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
    case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
    default: return 0;
    }
}

static void set_key(int vk, int down)
{
    if (vk <= 0 || vk > 255) return;
    if (down && !g_keys[vk]) g_down[vk] = 1;
    g_keys[vk] = (unsigned char)down;
    /* the plain codes follow either side, as GetAsyncKeyState(VK_CONTROL) does */
    int plain = (vk == VK_LCONTROL || vk == VK_RCONTROL) ? VK_CONTROL : (vk == VK_LSHIFT || vk == VK_RSHIFT) ? VK_SHIFT
              : (vk == VK_LMENU || vk == VK_RMENU) ? VK_MENU : 0;
    if (plain) {
        int a = plain == VK_CONTROL ? VK_LCONTROL : plain == VK_SHIFT ? VK_LSHIFT : VK_LMENU;
        int now = g_keys[a] || g_keys[a + 1];
        if (now && !g_keys[plain]) g_down[plain] = 1;
        g_keys[plain] = (unsigned char)now;
    }
}

/* ---- window ---------------------------------------------------------------------------------------------------- */
#ifdef __SWITCH__
#include <switch/applets/error.h>       /* not all of switch.h: its names clash with SDL's and the engine's */
/* SDL has no message box on the Switch: the system's error dialog shows the text with one button */
void plat_message(const char *text, int warn)
{
    (void)warn;
    printf("%s\n", text);
    fflush(stdout);
    ErrorApplicationConfig c;
    errorApplicationCreate(&c, text, NULL);
    errorApplicationShow(&c);
}
#else
void plat_message(const char *text, int warn)
{
    fprintf(stderr, "%s\n", text);
    if (getenv("A2_HIDDEN")) return;                          /* tests: no window */
    SDL_ShowSimpleMessageBox(warn ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION, "A2RacerRE", text, g_win);
}
#endif

int plat_init(const PlatConfig *cfg)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    /* the PlayStation pads through SDL's own HIDAPI drivers: rumble (also over Bluetooth), lightbar, player LEDs
     * (the names, not the SDL_HINT_ macros: an older system SDL, as on Ubuntu 22.04, lacks some and ignores them) */
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5_PLAYER_LED", "1");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");   /* Android: the phone's tilt is not a controller */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) fprintf(stderr, "no haptic: %s\n", SDL_GetError());   /* wheels */
    /* extra controller mappings (SDL's gamecontrollerdb.txt format) next to the program and in the user folder;
     * SDL itself also reads the file named by SDL_GAMECONTROLLERCONFIG_FILE */
    {
        const char *dirs[2] = { plat_base_dir(), plat_user_dir() };
        for (int i = 0; i < 2; i++) {
            char p[1024];
            snprintf(p, sizeof p, "%sgamecontrollerdb.txt", dirs[i]);
            if (i == 1 && !strcmp(dirs[0], dirs[1])) break;
            if (!plat_exists(p)) continue;
            int n = SDL_GameControllerAddMappingsFromFile(p);
            printf("%s: %d controller mappings\n", p, n);
        }
    }
    test_pad_init();
    test_touch_init();
    g_t0 = SDL_GetPerformanceCounter();
#if defined __ANDROID__ || defined __SWITCH__
    g_gles = 1;
#endif
    if (g_gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int w = cfg->width > 0 ? cfg->width : 1280, h = cfg->height > 0 ? cfg->height : 960;
    if (getenv("A2_TEST_WINDOW")) {                           /* tests: "WxH", e.g. a phone's 20:9 */
        int tw = 0, th = 0;
        if (sscanf(getenv("A2_TEST_WINDOW"), "%dx%d", &tw, &th) == 2 && tw > 0 && th > 0) { w = tw; h = th; }
    }
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#elif defined __SWITCH__
    flags |= SDL_WINDOW_FULLSCREEN;      /* the whole screen; SDL follows docked (1080p) and handheld (720p) */
    w = 1280;
    h = 720;
#else
    if (getenv("A2_HIDDEN")) flags |= SDL_WINDOW_HIDDEN;      /* tests and screenshots without a window on screen */
    else if (cfg->fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    g_win = SDL_CreateWindow(cfg->title ? cfg->title : "A2RacerRE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w, h, flags);
    if (!g_win) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        g_win = SDL_CreateWindow("A2RacerRE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    }
    if (!g_win || !(g_gl = SDL_GL_CreateContext(g_win))) {
        char msg[300];
        snprintf(msg, sizeof msg, "Could not open an OpenGL %s window: %s", g_gles ? "ES 2.0" : "2.1", SDL_GetError());
        plat_message(msg, 1);
        return 0;
    }
    g_vsync = cfg->vsync ? 1 : 0;
    SDL_GL_SetSwapInterval(g_vsync);
    g_test_glreset = getenv("A2_TEST_GLRESET") ? atof(getenv("A2_TEST_GLRESET")) : 0;
#if defined __ANDROID__ || defined __SWITCH__
    /* no soft keyboard over the game: on Android only while a name is typed (plat_text_mode); the Switch's is an
     * applet that stops the game, so there the name is entered with the pad (the menu's pad name entry) */
    SDL_StopTextInput();
#else
    SDL_StartTextInput();
#endif
    return 1;
}

void plat_text_mode(int on)
{
#ifdef __ANDROID__
    static int cur;
    if (on == cur) return;
    cur = on;
    if (on) SDL_StartTextInput();
    else SDL_StopTextInput();
#else
    (void)on;                        /* the desktop keeps text input on (it brings up nothing) */
#endif
}

static void accel_close(void);

void plat_shutdown(void)
{
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    accel_close();
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        Dev *v = &g_dev[i];
        if (!v->used) continue;
        if (v->haptic) SDL_HapticClose(v->haptic);
        if (v->gc) SDL_GameControllerClose(v->gc);
        else SDL_JoystickClose(v->joy);
    }
    if (g_gl) SDL_GL_DeleteContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    /* Android can start SDL_main again in the same process: nothing may point at the old SDL */
    memset(g_dev, 0, sizeof g_dev);
    memset(&g_pad, 0, sizeof g_pad);
    g_last_dev = -1;
    memset(g_keys, 0, sizeof g_keys);
    memset(g_down, 0, sizeof g_down);
    g_nfing = 0;
    g_input_kind = PLAT_INPUT_NONE;
    g_touch_capture = 0;
    g_audio = 0;
    g_audio_fn = NULL;
    g_audio_user = NULL;
    g_gl = NULL;
    g_win = NULL;
}

void plat_swap(void) { SDL_GL_SwapWindow(g_win); }
void plat_size(int *w, int *h) { SDL_GL_GetDrawableSize(g_win, w, h); }
int plat_is_gles(void) { return g_gles; }
int plat_gl_can_lose(void) { return g_gles || g_test_glreset > 0; }

int plat_gl_lost(void)
{
    int l = g_gl_lost;
    g_gl_lost = 0;
    return l;
}

/* SDL_RENDER_DEVICE_RESET: Android's SDL found the context gone on resume and made the window a new one
 * (android_egl_context_restore), which is current now */
static void gl_reset(void)
{
    g_gl = SDL_GL_GetCurrentContext();
    SDL_GL_SetSwapInterval(g_vsync);
    g_gl_lost = 1;
    printf("the GL context was lost: rebuilt\n");
}

/* A2_TEST_GLRESET: what Android does, on the desktop: a new context that shares nothing, the old one deleted */
static void test_gl_reset_step(void)
{
    if (g_test_glreset <= 0 || plat_time() < g_test_glreset) return;
    g_test_glreset = -1;                     /* once (what is made from here on keeps no copy) */
    SDL_GLContext n = SDL_GL_CreateContext(g_win);
    if (!n) return;
    SDL_GL_MakeCurrent(g_win, n);
    SDL_GL_DeleteContext(g_gl);
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_RENDER_DEVICE_RESET;
    SDL_PushEvent(&e);
}

void *plat_gl_proc(const char *name) { return SDL_GL_GetProcAddress(name); }

void plat_toggle_fullscreen(void)
{
#if !defined __ANDROID__ && !defined __SWITCH__
    int fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}

/* ---- input ----------------------------------------------------------------------------------------------------- */
static float axis(Sint16 v) { return v < 0 ? v / 32768.0f : v / 32767.0f; }

/* case-insensitive strstr (SDL_strcasestr needs SDL 2.26; Linux builds use the system's SDL) */
static int has_word(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (; *s; s++)
        if (!SDL_strncasecmp(s, w, n)) return 1;
    return 0;
}

/* how a pad's buttons are labelled: SDL's controller type, else its name (Steam Input and virtual pads) */
static int pad_kind(SDL_GameController *c, const char *name)
{
#ifdef __SWITCH__
    (void)c; (void)name;
    return PADKIND_SWITCH;                                            /* the Joy-Cons, handheld, a Pro Controller */
#endif
    switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_XBOX360: case SDL_CONTROLLER_TYPE_XBOXONE:
        if (has_word(name, "steam")) return PADKIND_STEAM;            /* Steam Input's virtual gamepad */
        return PADKIND_XBOX;
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5: return PADKIND_PS;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return PADKIND_SWITCH;
    default: break;
    }
    static const struct { const char *s; int k; } N[] = {
        { "dualsense", PADKIND_PS }, { "dualshock", PADKIND_PS }, { "playstation", PADKIND_PS }, { "ps5", PADKIND_PS },
        { "ps4", PADKIND_PS }, { "ps3", PADKIND_PS }, { "steam", PADKIND_STEAM }, { "switch", PADKIND_SWITCH },
        { "joy-con", PADKIND_SWITCH }, { "xbox", PADKIND_XBOX }, { "xinput", PADKIND_XBOX }, { "x-box", PADKIND_XBOX } };
    for (size_t i = 0; i < sizeof N / sizeof N[0]; i++)
        if (has_word(name, N[i].s)) return N[i].k;
    return PADKIND_GENERIC;
}

static void read_device(Dev *v)
{
    PlatDevice *d = &v->d;
    memcpy(d->prev_axis, d->axis, sizeof d->axis);
    memcpy(d->prev_hat, d->hat, sizeof d->hat);
    d->prev_buttons = d->buttons;
    d->buttons = 0;
    int active = 0;
    if (v->gc) {
        for (int a = 0; a < GP_NAXES; a++) {
            Sint16 r = SDL_GameControllerGetAxis(v->gc, (SDL_GameControllerAxis)a);
            d->axis[a] = a >= GP_LT ? (r > 0 ? r / 32767.0f : 0) : axis(r);
            if (a < GP_LT ? fabsf(d->axis[a]) > 0.5f : d->axis[a] > 0.3f) active = 1;
        }
        for (int b = 0; b < GP_NBUTTONS; b++)
            if (SDL_GameControllerGetButton(v->gc, (SDL_GameControllerButton)b)) d->buttons |= 1u << b;
    } else {
        for (int a = 0; a < d->naxes; a++) {
            d->axis[a] = axis(SDL_JoystickGetAxis(v->joy, a));
            if (fabsf(d->axis[a] - d->prev_axis[a]) > 0.05f) active = 1;
        }
        for (int b = 0; b < d->nbuttons; b++)
            if (SDL_JoystickGetButton(v->joy, b)) d->buttons |= 1u << b;
        for (int h = 0; h < d->nhats; h++) {
            d->hat[h] = SDL_JoystickGetHat(v->joy, h);
            if (d->hat[h] != d->prev_hat[h]) active = 1;
        }
    }
    if (d->buttons != d->prev_buttons) active = 1;
    if (active) { d->active = plat_time(); g_last_dev = (int)(v - g_dev); g_input_kind = PLAT_INPUT_OTHER; }
}

static void open_device(int index)
{
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    const char *jn = SDL_JoystickNameForIndex(index);
    if (jn && has_word(jn, "accelerometer")) return;                 /* Android's tilt sensor, if SDL offers it */
    int slot = -1;
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        if (g_dev[i].used && g_dev[i].id == id) return;
        if (!g_dev[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return;
    Dev *v = &g_dev[slot];
    memset(v, 0, sizeof *v);
    v->spring_fx = -1;
    if (SDL_IsGameController(index) && (v->gc = SDL_GameControllerOpen(index)) != NULL)
        v->joy = SDL_GameControllerGetJoystick(v->gc);
    else v->joy = SDL_JoystickOpen(index);
    if (!v->joy) { fprintf(stderr, "controller %d: %s\n", index, SDL_GetError()); memset(v, 0, sizeof *v); return; }
    v->used = 1;
    v->id = SDL_JoystickInstanceID(v->joy);
    PlatDevice *d = &v->d;
    const char *name = v->gc ? SDL_GameControllerName(v->gc) : SDL_JoystickName(v->joy);
    snprintf(d->name, sizeof d->name, "%s", name ? name : "?");
    d->gamepad = v->gc != NULL;
    if (v->gc) {
        d->kind = pad_kind(v->gc, d->name);
        d->naxes = GP_NAXES;
        d->nbuttons = GP_NBUTTONS;
        SDL_GameControllerSetPlayerIndex(v->gc, slot);               /* the DualSense's player LEDs */
        if (SDL_GameControllerHasLED(v->gc)) SDL_GameControllerSetLED(v->gc, 0xff, 0x60, 0x00);   /* lightbar */
    } else {
        d->kind = PADKIND_GENERIC;
        d->naxes = SDL_min(SDL_JoystickNumAxes(v->joy), PLAT_DEV_AXES);
        d->nbuttons = SDL_min(SDL_JoystickNumButtons(v->joy), PLAT_DEV_BUTTONS);
        d->nhats = SDL_min(SDL_JoystickNumHats(v->joy), PLAT_DEV_HATS);
        if (SDL_JoystickIsHaptic(v->joy) == 1 && (v->haptic = SDL_HapticOpenFromJoystick(v->joy)) != NULL) {
            d->can_spring = (SDL_HapticQuery(v->haptic) & SDL_HAPTIC_SPRING) != 0;
            v->haptic_rumble = SDL_HapticRumbleSupported(v->haptic) == 1 && SDL_HapticRumbleInit(v->haptic) == 0;
        }
    }
    d->can_rumble = SDL_JoystickHasRumble(v->joy) || v->haptic_rumble;
    int kind = g_input_kind;
    read_device(v);
    read_device(v);                                                  /* prev = now: nothing "went down" on arrival */
    d->active = 0;
    g_input_kind = kind;                                             /* arriving is not using it */
    printf("controller %d: %s (%s, %d axes, %d buttons, %d hats%s%s)\n", slot, d->name,
           v->gc ? "game controller" : "joystick", d->naxes, d->nbuttons, d->nhats, d->can_rumble ? ", rumble" : "",
           d->can_spring ? ", force feedback" : "");
}

static void close_device(SDL_JoystickID id)
{
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        Dev *v = &g_dev[i];
        if (!v->used || v->id != id) continue;
        printf("controller %d: %s removed\n", i, v->d.name);
        if (v->haptic) SDL_HapticClose(v->haptic);
        if (v->gc) SDL_GameControllerClose(v->gc);
        else SDL_JoystickClose(v->joy);
        memset(v, 0, sizeof *v);
        if (g_last_dev == i) g_last_dev = -1;
    }
}

/* the merged pad of the menus: every game controller, and the hats / first two buttons of raw joysticks */
static void update_pad(void)
{
    unsigned before = g_pad.buttons;
    memset(&g_pad, 0, sizeof g_pad);
    static const unsigned M[GP_NBUTTONS] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, 0, PAD_START, PAD_LS, PAD_RS,
                                             PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT };
    for (int i = 0; i < PLAT_MAX_DEVICES; i++) {
        Dev *v = &g_dev[i];
        if (!v->used) continue;
        read_device(v);
        const PlatDevice *d = &v->d;
        g_pad.present = 1;
        if (d->gamepad) {
            float *dst[6] = { &g_pad.lx, &g_pad.ly, &g_pad.rx, &g_pad.ry, &g_pad.lt, &g_pad.rt };
            for (int a = 0; a < GP_NAXES; a++)
                if (fabsf(d->axis[a]) > fabsf(*dst[a])) *dst[a] = d->axis[a];
            for (int b = 0; b < GP_NBUTTONS; b++)
                if (d->buttons & (1u << b)) g_pad.buttons |= M[b];
        } else {
            for (int h = 0; h < d->nhats; h++) {
                if (d->hat[h] & SDL_HAT_UP) g_pad.buttons |= PAD_UP;
                if (d->hat[h] & SDL_HAT_DOWN) g_pad.buttons |= PAD_DOWN;
                if (d->hat[h] & SDL_HAT_LEFT) g_pad.buttons |= PAD_LEFT;
                if (d->hat[h] & SDL_HAT_RIGHT) g_pad.buttons |= PAD_RIGHT;
            }
            if (d->buttons & 1) g_pad.buttons |= PAD_A;
            if (d->buttons & 2) g_pad.buttons |= PAD_B;
        }
    }
    g_pad.pressed = g_pad.buttons & ~before;
}

/* ---- touch ----------------------------------------------------------------------------------------------------- */
static void finger_event(const SDL_TouchFingerEvent *f, Uint32 type)
{
#ifdef SDL_MOUSE_TOUCHID
    if (f->touchId == SDL_MOUSE_TOUCHID) return;                     /* the mouse posing as a finger */
#endif
    int dw = 1, dh = 1, i;
    SDL_GL_GetDrawableSize(g_win, &dw, &dh);
    float x = f->x * dw, y = f->y * dh;
    for (i = 0; i < g_nfing; i++)
        if (g_fing[i].id == (int64_t)f->fingerId) break;
    if (type == SDL_FINGERDOWN) {
        if (i == g_nfing) {
            if (g_nfing == PLAT_MAX_FINGERS) return;
            g_nfing++;
        }
        g_fing[i].id = (int64_t)f->fingerId;
        g_fing[i].down = g_fing[i].fresh = 1;
    } else if (i == g_nfing) return;                                 /* went down before the race / window */
    g_fing[i].x = x;
    g_fing[i].y = y;
    if (type == SDL_FINGERUP) g_fing[i].down = 0;
    g_input_kind = PLAT_INPUT_TOUCH;
}

int plat_fingers(const PlatFinger **list) { *list = g_fing; return g_nfing; }
int plat_input_kind(void) { return g_input_kind; }
void plat_touch_capture(int on) { g_touch_capture = on; }

#if SDL_VERSION_ATLEAST(2, 0, 9)
static SDL_Sensor *g_accel;
static int g_accel_tried;

int plat_accel(float xyz[3])
{
    if (g_test_accel) { memcpy(xyz, g_test_accel_v, sizeof g_test_accel_v); return 1; }
    if (!g_accel_tried) {
        g_accel_tried = 1;
        if (SDL_InitSubSystem(SDL_INIT_SENSOR) == 0) {
            for (int i = 0; i < SDL_NumSensors() && !g_accel; i++)
                if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL) g_accel = SDL_SensorOpen(i);
        }
        if (g_accel) printf("accelerometer: %s\n", SDL_SensorGetName(g_accel));
        else fprintf(stderr, "no accelerometer\n");
    }
    return g_accel && SDL_SensorGetData(g_accel, xyz, 3) == 0;      /* SDL_PollEvent updates the sensors */
}

static void accel_close(void)
{
    if (g_accel) SDL_SensorClose(g_accel);
    g_accel = NULL;
    g_accel_tried = 0;
}
#else
int plat_accel(float xyz[3])
{
    if (g_test_accel) { memcpy(xyz, g_test_accel_v, sizeof g_test_accel_v); return 1; }
    return 0;
}
static void accel_close(void) {}
#endif

int plat_poll(void)
{
    SDL_Event e;
    int alive = 1;
    memset(g_down, 0, sizeof g_down);
    g_text[0] = 0;
    g_mclick = 0;
    {   /* the fingers lifted at the previous poll leave the list */
        int n = 0;
        for (int i = 0; i < g_nfing; i++)
            if (g_fing[i].down) { g_fing[n] = g_fing[i]; g_fing[n++].fresh = 0; }
        g_nfing = n;
    }
    test_pad_step();
    test_touch_step();
    test_gl_reset_step();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: alive = 0; break;
        case SDL_RENDER_DEVICE_RESET: gl_reset(); break;
        case SDL_KEYDOWN: {
            int vk = sc_to_vk(e.key.keysym.scancode);
            if (!e.key.repeat) set_key(vk, 1);
            if (vk && e.key.keysym.scancode != SDL_SCANCODE_AC_BACK) g_input_kind = PLAT_INPUT_OTHER;
            if (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) plat_toggle_fullscreen();
            break;
        }
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: finger_event(&e.tfinger, e.type); break;
        /* the fingers on the screen when the game loses it may never send their release */
        case SDL_WINDOWEVENT:
            if (e.window.event != SDL_WINDOWEVENT_FOCUS_LOST) break;
            /* fall through */
        case SDL_APP_WILLENTERBACKGROUND:
            for (int i = 0; i < g_nfing; i++) g_fing[i].down = 0;
            break;
        case SDL_KEYUP: set_key(sc_to_vk(e.key.keysym.scancode), 0); break;
        case SDL_TEXTINPUT: {
            size_t n = strlen(g_text);
            for (const unsigned char *s = (const unsigned char *)e.text.text; *s && n + 1 < sizeof g_text; s++) {
                if (*s < 0x80) g_text[n++] = (char)*s;
                else if ((*s & 0xe0) == 0xc0 && s[1]) { g_text[n++] = (char)(((*s & 0x1f) << 6) | (s[1] & 0x3f)); s++; }
            }
            g_text[n] = 0;
            break;
        }
        /* the race has its own touch controls: there a finger does not also press the emulated mouse (its releases
         * still count, so nothing stays held) */
        case SDL_MOUSEMOTION:
            if (g_touch_capture && e.motion.which == SDL_TOUCH_MOUSEID) break;
            g_mx = e.motion.x; g_my = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (g_touch_capture && e.button.which == SDL_TOUCH_MOUSEID) break;
            g_mx = e.button.x; g_my = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) { g_mb |= 1; g_mclick |= 1; }
            if (e.button.button == SDL_BUTTON_RIGHT) { g_mb |= 2; g_mclick |= 2; }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) g_mb &= ~1u;
            if (e.button.button == SDL_BUTTON_RIGHT) g_mb &= ~2u;
            break;
        /* every joystick (game controllers too) arrives and leaves as a joystick; it is opened as a game controller
         * when SDL has a mapping for it */
        case SDL_JOYDEVICEADDED: open_device(e.jdevice.which); break;
        case SDL_JOYDEVICEREMOVED: close_device(e.jdevice.which); break;
        default: break;
        }
    }
    update_pad();
    return alive;
}

int plat_key(int vk) { return vk > 0 && vk < 256 && g_keys[vk]; }
int plat_key_pressed(int vk) { return vk > 0 && vk < 256 && g_down[vk]; }
int plat_any_key_pressed(void)
{
    for (int i = 1; i < 256; i++)
        if (g_down[i] && i != VK_CONTROL && i != VK_SHIFT && i != VK_MENU) return i;
    return 0;
}
const char *plat_text_input(void) { return g_text; }
const PlatPad *plat_pad(void) { return &g_pad; }
const PlatDevice *plat_device(int i) { return i >= 0 && i < PLAT_MAX_DEVICES && g_dev[i].used ? &g_dev[i].d : NULL; }
int plat_last_device(void)
{
    if (g_last_dev >= 0 && g_dev[g_last_dev].used) return g_last_dev;
    for (int i = 0; i < PLAT_MAX_DEVICES; i++)
        if (g_dev[i].used) return i;
    return -1;
}

static Uint16 rumble16(float v) { return (Uint16)(v <= 0 ? 0 : v >= 1 ? 65535 : v * 65535); }

void plat_device_rumble(int i, float low, float high, int ms)
{
    if (i < 0 || i >= PLAT_MAX_DEVICES || !g_dev[i].used) return;
    Dev *v = &g_dev[i];
    if (SDL_JoystickHasRumble(v->joy)) SDL_JoystickRumble(v->joy, rumble16(low), rumble16(high), (Uint32)ms);
    else if (v->haptic_rumble) {
        float s = low > high ? low : high;
        if (s > 0) SDL_HapticRumblePlay(v->haptic, s > 1 ? 1 : s, (Uint32)ms);
        else SDL_HapticRumbleStop(v->haptic);
    }
}

void plat_rumble(float low, float high, int ms) { plat_device_rumble(plat_last_device(), low, high, ms); }

/* the centring spring of a force-feedback wheel / stick (what the original's DEVICE 4 made with constant forces) */
void plat_device_spring(int i, float strength)
{
    if (i < 0 || i >= PLAT_MAX_DEVICES || !g_dev[i].used || !g_dev[i].d.can_spring) return;
    Dev *v = &g_dev[i];
    if (strength < 0) strength = 0;
    if (strength > 1) strength = 1;
    if (fabsf(strength - v->spring) < 0.01f && (strength > 0) == v->spring_on) return;
    v->spring = strength;
    if (strength <= 0) {
        if (v->spring_fx >= 0 && v->spring_on) SDL_HapticStopEffect(v->haptic, v->spring_fx);
        v->spring_on = 0;
        return;
    }
    SDL_HapticEffect fx;
    memset(&fx, 0, sizeof fx);
    fx.type = SDL_HAPTIC_SPRING;
    fx.condition.length = SDL_HAPTIC_INFINITY;
    fx.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    fx.condition.direction.dir[0] = 1;
    for (int a = 0; a < 3; a++) {
        fx.condition.right_sat[a] = fx.condition.left_sat[a] = 0xffff;
        fx.condition.right_coeff[a] = fx.condition.left_coeff[a] = (Sint16)(strength * 32767);
    }
    if (v->spring_fx < 0) v->spring_fx = SDL_HapticNewEffect(v->haptic, &fx);
    else SDL_HapticUpdateEffect(v->haptic, v->spring_fx, &fx);
    if (v->spring_fx < 0) { v->d.can_spring = 0; fprintf(stderr, "spring: %s\n", SDL_GetError()); return; }
    if (!v->spring_on) SDL_HapticRunEffect(v->haptic, v->spring_fx, 1);
    v->spring_on = 1;
}

/* ---- developer: a virtual controller driven by a script (tests without a pad) ----------------------------------
 * A2_TEST_PAD="T:EL=V EL=V; T:..." attaches a virtual game controller (A2_TEST_PAD_NAME names it, e.g. "DualSense
 * Wireless Controller") and sets its elements T seconds after the start: A B X Y BACK GUIDE START LS RS LB RB DPUP
 * DPDOWN DPLEFT DPRIGHT = 0/1, LEFTX LEFTY RIGHTX RIGHTY = -1..1, LT RT = 0..1; "T:unplug" / "T:plug" detach and
 * attach it again. A2_TEST_JOY does the same with a raw wheel-like joystick (3 axes, 12 buttons, a hat):
 * axisN=-1..1 buttonN=0/1 hatN=up|down|left|right|c. Rumble reaching the virtual pad is printed. */
#if SDL_VERSION_ATLEAST(2, 24, 0)
typedef struct { const char *script; int raw, index; SDL_Joystick *joy; const char *pos; double t0; } TestPad;
static TestPad g_test[2];

static int SDLCALL test_rumble(void *user, Uint16 lo, Uint16 hi)
{
    printf("test pad %d: rumble %u %u\n", (int)(intptr_t)user, lo, hi);
    return 0;
}

static void test_attach(TestPad *t)
{
    static const char *const names[2] = { "A2 test pad", "A2 test wheel" };
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    if (t->raw) {
        desc.type = SDL_JOYSTICK_TYPE_WHEEL;
        desc.naxes = 3;
        desc.nbuttons = 12;
        desc.nhats = 1;
        desc.name = names[1];
    } else {
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MISC1;
        desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MISC1) - 1;
        desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
        desc.name = getenv("A2_TEST_PAD_NAME") ? getenv("A2_TEST_PAD_NAME") : names[0];
    }
    desc.userdata = (void *)(intptr_t)t->raw;
    desc.Rumble = test_rumble;
    t->index = SDL_JoystickAttachVirtualEx(&desc);
    t->joy = t->index >= 0 ? SDL_JoystickOpen(t->index) : NULL;
    if (!t->joy) { fprintf(stderr, "test pad: %s\n", SDL_GetError()); return; }
    if (!t->raw)                                             /* the triggers rest at the axis minimum */
        for (int a = SDL_CONTROLLER_AXIS_TRIGGERLEFT; a <= SDL_CONTROLLER_AXIS_TRIGGERRIGHT; a++)
            SDL_JoystickSetVirtualAxis(t->joy, a, -32768);
}

static void test_pad_init(void)
{
    g_test[0].script = getenv("A2_TEST_PAD");
    g_test[1].script = getenv("A2_TEST_JOY");
    g_test[1].raw = 1;
    for (int i = 0; i < 2; i++) {
        TestPad *t = &g_test[i];
        if (!t->script) continue;
        t->pos = t->script;
        t->t0 = -1;
        test_attach(t);
    }
}

static void test_set(TestPad *t, const char *el, float v)
{
    static const char *const AX[] = { "LEFTX", "LEFTY", "RIGHTX", "RIGHTY", "LT", "RT" };
    static const char *const BT[] = { "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LS", "RS", "LB", "RB", "DPUP",
                                      "DPDOWN", "DPLEFT", "DPRIGHT" };
    Sint16 full = (Sint16)(v <= -1 ? -32768 : v >= 1 ? 32767 : v * 32767);
    if (!t->raw) {
        for (int a = 0; a < 6; a++)
            if (!SDL_strcasecmp(el, AX[a])) {
                SDL_JoystickSetVirtualAxis(t->joy, a, a >= 4 ? (Sint16)(-32768 + (v < 0 ? 0 : v > 1 ? 1 : v) * 65535) : full);
                return;
            }
        for (int b = 0; b < 15; b++)
            if (!SDL_strcasecmp(el, BT[b])) { SDL_JoystickSetVirtualButton(t->joy, b, v > 0.5f); return; }
    } else if (!SDL_strncasecmp(el, "axis", 4)) { SDL_JoystickSetVirtualAxis(t->joy, atoi(el + 4), full); return; }
    else if (!SDL_strncasecmp(el, "button", 6)) { SDL_JoystickSetVirtualButton(t->joy, atoi(el + 6), v > 0.5f); return; }
    fprintf(stderr, "test pad: unknown element %s\n", el);
}

static void test_pad_step(void)
{
    for (int i = 0; i < 2; i++) {
        TestPad *t = &g_test[i];
        if (!t->script) continue;
        double now = plat_time();
        if (t->t0 < 0) t->t0 = now;
        while (*t->pos) {
            const char *p = t->pos;
            while (*p == ' ' || *p == ';') p++;
            if (!*p) { t->pos = p; break; }
            char *e;
            double at = strtod(p, &e);
            if (e == p || *e != ':') { fprintf(stderr, "test pad: bad script at %s\n", p); t->pos = p + strlen(p); break; }
            if (now - t->t0 < at) break;
            p = e + 1;
            const char *end = strchr(p, ';');
            if (!end) end = p + strlen(p);
            while (p < end) {
                char tok[64];
                size_t n = 0;
                while (p < end && *p == ' ') p++;
                while (p < end && *p != ' ' && n + 1 < sizeof tok) tok[n++] = *p++;
                tok[n] = 0;
                if (!n) continue;
                printf("test pad %d at %.2f s: %s\n", i, now - t->t0, tok);
                if (!SDL_strcasecmp(tok, "unplug")) {
                    if (t->joy) { SDL_JoystickClose(t->joy); t->joy = NULL; SDL_JoystickDetachVirtual(t->index); }
                    continue;
                }
                if (!SDL_strcasecmp(tok, "plug")) { if (!t->joy) test_attach(t); continue; }
                char *eq = strchr(tok, '=');
                if (!eq || !t->joy) continue;
                *eq = 0;
                if (t->raw && !SDL_strncasecmp(tok, "hat", 3)) {
                    const char *d = eq + 1;
                    Uint8 h = !SDL_strcasecmp(d, "up") ? SDL_HAT_UP : !SDL_strcasecmp(d, "down") ? SDL_HAT_DOWN
                            : !SDL_strcasecmp(d, "left") ? SDL_HAT_LEFT : !SDL_strcasecmp(d, "right") ? SDL_HAT_RIGHT
                            : SDL_HAT_CENTERED;
                    SDL_JoystickSetVirtualHat(t->joy, atoi(tok + 3), h);
                } else test_set(t, tok, (float)atof(eq + 1));
            }
            t->pos = end;
        }
    }
}
#else
static void test_pad_init(void)
{
    if (getenv("A2_TEST_PAD") || getenv("A2_TEST_JOY")) fprintf(stderr, "test pad: needs SDL 2.24 or later\n");
}
static void test_pad_step(void) {}
#endif

/* ---- developer: fingers driven by a script (touch tests without a touch screen) ---------------------------------
 * A2_TEST_TOUCH="T:CMD; T:CMD; ..." pushes SDL finger events T seconds after the start, one command per entry:
 * "down ID X Y", "move ID X Y" (X, Y 0..1 of the window), "up ID", "tap ID X Y" (down and up in the same frame),
 * "accel X Y Z" (fixes the accelerometer's reading, m/s^2) and "key" (a press of Shift: input that is not touch). */
static struct { const char *script, *pos; double t0; float x[16], y[16]; } g_tt;

static void test_touch_init(void)
{
    g_tt.script = g_tt.pos = getenv("A2_TEST_TOUCH");
    g_tt.t0 = -1;
}

static void test_finger(Uint32 type, int id, float x, float y)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = type;
    ev.tfinger.touchId = 77;
    ev.tfinger.fingerId = id;
    ev.tfinger.x = x;
    ev.tfinger.y = y;
    ev.tfinger.pressure = type == SDL_FINGERUP ? 0 : 1;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    ev.tfinger.windowID = SDL_GetWindowID(g_win);
#endif
    SDL_PushEvent(&ev);
}

static void test_touch_step(void)
{
    if (!g_tt.script) return;
    double now = plat_time();
    if (g_tt.t0 < 0) g_tt.t0 = now;
    while (*g_tt.pos) {
        const char *p = g_tt.pos;
        while (*p == ' ' || *p == ';') p++;
        if (!*p) { g_tt.pos = p; break; }
        char *e;
        double at = strtod(p, &e);
        if (e == p || *e != ':') { fprintf(stderr, "test touch: bad script at %s\n", p); g_tt.pos = p + strlen(p); break; }
        if (now - g_tt.t0 < at) break;
        p = e + 1;
        const char *end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        char cmd[96];
        size_t n = (size_t)(end - p) < sizeof cmd - 1 ? (size_t)(end - p) : sizeof cmd - 1;
        memcpy(cmd, p, n);
        cmd[n] = 0;
        g_tt.pos = end;
        printf("test touch at %.2f s: %s\n", now - g_tt.t0, cmd);
        char op[16];
        int id = 0;
        float x = 0, y = 0, z = 0;
        int k = sscanf(cmd, " %15s %d %f %f", op, &id, &x, &y);
        if (k < 1) continue;
        if (!SDL_strcasecmp(op, "accel") && sscanf(cmd, " %15s %f %f %f", op, &x, &y, &z) == 4) {
            g_test_accel = 1;
            g_test_accel_v[0] = x; g_test_accel_v[1] = y; g_test_accel_v[2] = z;
            continue;
        }
        if (!SDL_strcasecmp(op, "key")) {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_KEYDOWN;
            ev.key.state = SDL_PRESSED;
            ev.key.keysym.scancode = SDL_SCANCODE_LSHIFT;
            SDL_PushEvent(&ev);
            ev.type = SDL_KEYUP;
            ev.key.state = SDL_RELEASED;
            SDL_PushEvent(&ev);
            continue;
        }
        if (k < 2 || id < 0 || id >= 16) { fprintf(stderr, "test touch: bad command %s\n", cmd); continue; }
        if (k == 4) { g_tt.x[id] = x; g_tt.y[id] = y; }
        if (!SDL_strcasecmp(op, "down") && k == 4) test_finger(SDL_FINGERDOWN, id, x, y);
        else if (!SDL_strcasecmp(op, "move") && k == 4) test_finger(SDL_FINGERMOTION, id, x, y);
        else if (!SDL_strcasecmp(op, "up")) test_finger(SDL_FINGERUP, id, g_tt.x[id], g_tt.y[id]);
        else if (!SDL_strcasecmp(op, "tap") && k == 4) {
            test_finger(SDL_FINGERDOWN, id, x, y);
            test_finger(SDL_FINGERUP, id, x, y);
        } else fprintf(stderr, "test touch: bad command %s\n", cmd);
    }
}
void plat_show_cursor(int on) { SDL_ShowCursor(on ? SDL_ENABLE : SDL_DISABLE); }

void plat_mouse(int *x, int *y, unsigned *buttons, unsigned *clicked)
{
    /* window coordinates -> drawable pixels (high-DPI) */
    int ww, wh, dw, dh;
    SDL_GetWindowSize(g_win, &ww, &wh);
    SDL_GL_GetDrawableSize(g_win, &dw, &dh);
    if (x) *x = ww ? g_mx * dw / ww : g_mx;
    if (y) *y = wh ? g_my * dh / wh : g_my;
    if (buttons) *buttons = g_mb;
    if (clicked) *clicked = g_mclick;
}

/* ---- sound ----------------------------------------------------------------------------------------------------- */
static void SDLCALL audio_cb(void *user, Uint8 *stream, int len)
{
    (void)user;
    if (g_audio_fn) g_audio_fn((int16_t *)stream, len / 4, g_audio_user);
    else memset(stream, 0, (size_t)len);
}

void plat_audio_start(PlatAudioFn fn, void *user)
{
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    g_audio_fn = fn;
    g_audio_user = user;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_audio) { fprintf(stderr, "no audio: %s\n", SDL_GetError()); return; }
    g_audio_rate = have.freq;
    SDL_PauseAudioDevice(g_audio, 0);
}
int plat_audio_rate(void) { return g_audio_rate; }
void plat_audio_lock(void) { if (g_audio) SDL_LockAudioDevice(g_audio); }
void plat_audio_unlock(void) { if (g_audio) SDL_UnlockAudioDevice(g_audio); }

/* ---- time and files -------------------------------------------------------------------------------------------- */
double plat_time(void) { return (double)(SDL_GetPerformanceCounter() - g_t0) / (double)SDL_GetPerformanceFrequency(); }
void plat_sleep_ms(int ms) { SDL_Delay((Uint32)(ms > 0 ? ms : 0)); }

static int exists_exact(const char *path) { struct stat st; return stat(path, &st) == 0; }

#ifndef _WIN32
/* path with every part matched ignoring case against what is on disk; 0 = some part is not there */
static int resolve(const char *in, char *out, size_t cap)
{
    char buf[1024];
    size_t n = strlen(in), o = 0;
    if (n >= sizeof buf || n + 1 > cap) return 0;
    for (size_t i = 0; i <= n; i++) buf[i] = in[i] == '\\' ? '/' : in[i];
    out[0] = 0;
    char *p = buf;
    if (*p == '/') { out[o++] = '/'; out[o] = 0; while (*p == '/') p++; }
    while (*p) {
        char *e = strchr(p, '/'), test[1100];
        if (e) *e = 0;
        snprintf(test, sizeof test, "%s%s", out, p);
        if (!strcmp(p, ".") || !strcmp(p, "..") || exists_exact(test)) o += (size_t)snprintf(out + o, cap - o, "%s", p);
        else {
            DIR *d = opendir(o ? out : ".");
            struct dirent *de;
            int found = 0;
            if (d) {
                while ((de = readdir(d)))
                    if (!strcasecmp(de->d_name, p)) { o += (size_t)snprintf(out + o, cap - o, "%s", de->d_name); found = 1; break; }
                closedir(d);
            }
            if (!found) return 0;
        }
        if (o >= cap - 1) return 0;
        if (!e) break;
        out[o++] = '/';
        out[o] = 0;
        p = e + 1;
        while (*p == '/') p++;
    }
    return 1;
}
#endif

/* on case-sensitive file systems also when only the case differs (a user's game.ini, slots.ras, mods folder) */
int plat_exists(const char *path)
{
    if (exists_exact(path)) return 1;
#ifndef _WIN32
    char real[1024];
    return resolve(path, real, sizeof real);
#else
    return 0;
#endif
}

FILE *plat_fopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
#ifndef _WIN32
    if (!f && mode[0] == 'r' && errno == ENOENT) {
        char real[1024];
        if (resolve(path, real, sizeof real)) f = fopen(real, mode);
    }
#endif
    return f;
}

static char g_user_override[1024];
const char *plat_user_dir(void)
{
    static char *dir;
    if (g_user_override[0]) return g_user_override;
    if (!dir) {
#ifdef __SWITCH__
        /* sdmc:/switch/a2racerre on the SD card, next to a2racerre.nro: the game files in data/ (or an ISO image of the
         * CD to unpack there), the settings, the saves and mods/ */
        mkdir("sdmc:/switch", 0755);
        mkdir("sdmc:/switch/a2racerre", 0755);
        dir = SDL_strdup("sdmc:/switch/a2racerre/");
#endif
#ifdef __ANDROID__
        /* Android/data/<app>/files: reachable over USB, so the game files can also be copied there by hand */
        const char *ext = SDL_AndroidGetExternalStoragePath();
        if (ext) { size_t n = strlen(ext) + 2; dir = SDL_malloc(n); snprintf(dir, n, "%s/", ext); }
        if (!dir)
#endif
        dir = SDL_GetPrefPath("", "A2RacerRE");
        if (!dir) dir = SDL_strdup("./");
    }
    return dir;
}

void plat_set_user_dir(const char *dir)
{
    size_t n = dir ? strlen(dir) : 0;
    int sep = n && dir[n - 1] != '/' && dir[n - 1] != '\\';      /* plat_user_dir ends with a separator */
    snprintf(g_user_override, sizeof g_user_override, "%s%s", dir ? dir : "", sep ? "/" : "");
}

const char *plat_base_dir(void)
{
    static char *base;
    if (!base) {
#ifdef __SWITCH__
        base = SDL_strdup(plat_user_dir());
#else
        base = SDL_GetBasePath();
#endif
        if (!base) base = SDL_strdup("./");
    }
    return base;
}

int plat_ask(const char *text, const char *b1, const char *b2, const char *b3)
{
    SDL_MessageBoxButtonData btn[3];
    int n = 0;
    /* SDL lists the buttons right to left on Windows: add them in reverse so b1 is on the left */
    if (b3) { btn[n].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT; btn[n].buttonid = -1; btn[n].text = b3; n++; }
    if (b2) { btn[n].flags = 0; btn[n].buttonid = 0; btn[n].text = b2; n++; }
    if (b1) { btn[n].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT; btn[n].buttonid = 1; btn[n].text = b1; n++; }
    SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, g_win, "A2RacerRE", text, n, btn, NULL };
    int hit = -1;
    if (SDL_ShowMessageBox(&box, &hit) != 0) return -1;
    return hit;
}

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
int plat_pick_data(char *out, size_t cap, int *fd, const char *dest)
{
    (void)fd; (void)dest;
    int r = plat_ask("Choose an ISO image of the A2 Racer II CD, or a folder with a copy of the CD's files.",
                     "ISO image", "Folder", "Cancel");
    if (r < 0) return PICK_NONE;
    if (r == 1) {
        char file[1024] = "";
        OPENFILENAMEA o;
        memset(&o, 0, sizeof o);
        o.lStructSize = sizeof o;
        o.lpstrFilter = "CD images (*.iso)\0*.iso\0All files\0*.*\0";
        o.lpstrFile = file;
        o.nMaxFile = sizeof file;
        o.lpstrTitle = "A2 Racer II CD image";
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&o)) return PICK_NONE;
        snprintf(out, cap, "%s", file);
        return PICK_PATH;
    }
    BROWSEINFOA bi;
    char name[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.pszDisplayName = name;
    bi.lpszTitle = "The A2 Racer II CD, or a folder with a copy of it";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderA(&bi);
    if (!id) return PICK_NONE;
    char path[MAX_PATH];
    int ok = SHGetPathFromIDListA(id, path);
    CoTaskMemFree(id);
    if (!ok) return PICK_NONE;
    snprintf(out, cap, "%s", path);
    return PICK_PATH;
}
#elif defined(__ANDROID__)
#include <jni.h>
/* A2Activity.pickGameData (android/): Android's own file picker; kind 1 = ISO -> fd, 2 = folder -> copied into dest */
int plat_pick_data(char *out, size_t cap, int *fd, const char *dest)
{
    (void)out; (void)cap;
    int r = plat_ask("Choose an ISO image of the A2 Racer II CD, or a folder with a copy of the CD's files.",
                     "ISO image", "Folder", "Cancel");
    if (r < 0) return PICK_NONE;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return PICK_NONE;
    jclass c = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetStaticMethodID(env, c, "pickGameData", "(ILjava/lang/String;)I");
    int res = -1;
    if (m) {
        jstring d = (*env)->NewStringUTF(env, dest);
        res = (*env)->CallStaticIntMethod(env, c, m, r == 1 ? 1 : 2, d);
        (*env)->DeleteLocalRef(env, d);
    }
    (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, act);
    if (res < 0) return PICK_NONE;
    if (r == 1) { *fd = res; return PICK_FD; }
    return PICK_COPIED;
}
#elif defined __SWITCH__
/* no file picker: datasetup.c looks for an ISO image in the folder of the .nro instead */
int plat_pick_data(char *out, size_t cap, int *fd, const char *dest)
{
    (void)out; (void)cap; (void)fd; (void)dest;
    return PICK_NONE;
}
#else
/* Linux: zenity (GNOME, SteamOS desktop) or kdialog (KDE) when there; else the path typed in a terminal is not
 * possible from a game window, so the message tells how to pass it on the command line */
int plat_pick_data(char *out, size_t cap, int *fd, const char *dest)
{
    (void)fd; (void)dest;
    int r = plat_ask("Choose an ISO image of the A2 Racer II CD, or a folder with a copy of the CD's files.",
                     "ISO image", "Folder", "Cancel");
    if (r < 0) return PICK_NONE;
    const char *cmds[2][2] = {
        { "zenity --file-selection --title='A2 Racer II CD image' --file-filter='*.iso *.ISO' 2>/dev/null",
          "kdialog --getopenfilename ~ '*.iso *.ISO' 2>/dev/null" },
        { "zenity --file-selection --directory --title='A2 Racer II CD (or a copy)' 2>/dev/null",
          "kdialog --getexistingdirectory ~ 2>/dev/null" } };
    for (int i = 0; i < 2; i++) {
        FILE *p = popen(cmds[r == 1 ? 0 : 1][i], "r");
        if (!p) continue;
        char line[1024] = "";
        char *got = fgets(line, sizeof line, p);
        int st = pclose(p);
        if (got && st == 0) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0]) { snprintf(out, cap, "%s", line); return PICK_PATH; }
        }
    }
    plat_message("No file dialog (zenity or kdialog) found. Start the game with the path of the ISO image or of the "
                 "CD's folder as its argument instead:  ./a2racerre /path/to/A2Racer-II.iso", 1);
    return PICK_NONE;
}
#endif
