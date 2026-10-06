/* plat_sdl.c - plat.h on SDL2: window and GL context (desktop OpenGL 2.1, or OpenGL ES 2.0 on Android), keyboard as
 * Windows VK codes, the first game controller, mouse / touch as mouse, the audio device, time and case-insensitive
 * file lookup.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#define SDL_MAIN_HANDLED
#include "plat.h"
#include <SDL.h>
#include <errno.h>
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
static unsigned char g_keys[256], g_down[256];
static char g_text[64];
static PlatPad g_pad;
static SDL_GameController *g_ctrl;
static SDL_AudioDeviceID g_audio;
static int g_audio_rate = 44100;
static PlatAudioFn g_audio_fn;
static void *g_audio_user;
static int g_mx, g_my;
static unsigned g_mb, g_mclick;
static Uint64 g_t0;

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
void plat_message(const char *text, int warn)
{
    fprintf(stderr, "%s\n", text);
    SDL_ShowSimpleMessageBox(warn ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION, "A2RacerRE", text, g_win);
}

int plat_init(const PlatConfig *cfg)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    g_t0 = SDL_GetPerformanceCounter();
#ifdef __ANDROID__
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
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
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
    SDL_GL_SetSwapInterval(cfg->vsync ? 1 : 0);
    SDL_StartTextInput();
    return 1;
}

void plat_shutdown(void)
{
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    if (g_ctrl) SDL_GameControllerClose(g_ctrl);
    if (g_gl) SDL_GL_DeleteContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

void plat_swap(void) { SDL_GL_SwapWindow(g_win); }
void plat_size(int *w, int *h) { SDL_GL_GetDrawableSize(g_win, w, h); }
int plat_is_gles(void) { return g_gles; }
void *plat_gl_proc(const char *name) { return SDL_GL_GetProcAddress(name); }

void plat_toggle_fullscreen(void)
{
#ifndef __ANDROID__
    int fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}

/* ---- input ----------------------------------------------------------------------------------------------------- */
static float axis(Sint16 v) { return v < 0 ? v / 32768.0f : v / 32767.0f; }

static void update_pad(void)
{
    unsigned before = g_pad.buttons;
    memset(&g_pad, 0, sizeof g_pad);
    if (!g_ctrl) return;
    SDL_GameController *c = g_ctrl;
    g_pad.present = 1;
    g_pad.lx = axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    g_pad.ly = axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    g_pad.rx = axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
    g_pad.ry = axis(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    g_pad.lt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    g_pad.rt = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    static const struct { SDL_GameControllerButton b; unsigned bit; } M[] = {
        { SDL_CONTROLLER_BUTTON_A, PAD_A }, { SDL_CONTROLLER_BUTTON_B, PAD_B }, { SDL_CONTROLLER_BUTTON_X, PAD_X },
        { SDL_CONTROLLER_BUTTON_Y, PAD_Y }, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_LB },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_RB }, { SDL_CONTROLLER_BUTTON_BACK, PAD_BACK },
        { SDL_CONTROLLER_BUTTON_START, PAD_START }, { SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT }, { SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_LS },
        { SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_RS } };
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++)
        if (SDL_GameControllerGetButton(c, M[i].b)) g_pad.buttons |= M[i].bit;
    g_pad.pressed = g_pad.buttons & ~before;
}

int plat_poll(void)
{
    SDL_Event e;
    int alive = 1;
    memset(g_down, 0, sizeof g_down);
    g_text[0] = 0;
    g_mclick = 0;
    unsigned before = g_pad.buttons;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: alive = 0; break;
        case SDL_KEYDOWN:
            if (!e.key.repeat) set_key(sc_to_vk(e.key.keysym.scancode), 1);
            if (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) plat_toggle_fullscreen();
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
        case SDL_MOUSEMOTION: g_mx = e.motion.x; g_my = e.motion.y; break;
        case SDL_MOUSEBUTTONDOWN:
            g_mx = e.button.x; g_my = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) { g_mb |= 1; g_mclick |= 1; }
            if (e.button.button == SDL_BUTTON_RIGHT) { g_mb |= 2; g_mclick |= 2; }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) g_mb &= ~1u;
            if (e.button.button == SDL_BUTTON_RIGHT) g_mb &= ~2u;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!g_ctrl) g_ctrl = SDL_GameControllerOpen(e.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_ctrl && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_ctrl))) {
                SDL_GameControllerClose(g_ctrl);
                g_ctrl = NULL;
                for (int i = 0; i < SDL_NumJoysticks(); i++)
                    if (SDL_IsGameController(i) && (g_ctrl = SDL_GameControllerOpen(i))) break;
            }
            break;
        default: break;
        }
    }
    g_pad.buttons = before;
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
void plat_rumble(float low, float high, int ms)
{
    if (g_ctrl) SDL_GameControllerRumble(g_ctrl, (Uint16)(low * 65535), (Uint16)(high * 65535), (Uint32)ms);
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

int plat_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

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
        if (!strcmp(p, ".") || !strcmp(p, "..") || plat_exists(test)) o += (size_t)snprintf(out + o, cap - o, "%s", p);
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
        base = SDL_GetBasePath();
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
