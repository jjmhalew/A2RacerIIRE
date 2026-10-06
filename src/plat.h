/* plat.h - the window, input, sound output, time and files, on SDL2 for every platform (Windows, Linux / Steam Deck,
 * Android). The engine keeps the original's spelling of keys: Windows virtual-key codes, as in Game.ini [INPUT]
 * (LEFT=37 is VK_LEFT). Files are opened through plat_fopen, which on case-sensitive file systems looks a name up again
 * ignoring case (the CD writes "RCS.IMG" next to "Mrcs.img" and the code asks for "rcs.img").
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_PLAT_H
#define A2_PLAT_H
#include <stdint.h>
#include <stdio.h>

/* ---- window ---------------------------------------------------------------------------------------------------- */
typedef struct {
    int width, height;          /* requested window size, 0 = default (desktop) */
    int fullscreen;             /* 1 = borderless desktop fullscreen */
    int vsync;
    const char *title;
} PlatConfig;

int  plat_init(const PlatConfig *cfg);      /* window + GL context + audio; 0 = failed (message shown) */
void plat_shutdown(void);
void plat_swap(void);
void plat_size(int *w, int *h);             /* drawable size in pixels */
void plat_toggle_fullscreen(void);
int  plat_is_gles(void);                    /* 1 = OpenGL ES 2.0 (Android), 0 = desktop OpenGL 2.1 */
void *plat_gl_proc(const char *name);
void plat_message(const char *text, int warn);

/* ---- input ----------------------------------------------------------------------------------------------------- */
/* plat_poll handles the window's events; 0 = the window was closed. Key state is by Windows VK code (0..255). */
int  plat_poll(void);
int  plat_key(int vk);                      /* held */
int  plat_key_pressed(int vk);              /* went down since the previous plat_poll */
int  plat_any_key_pressed(void);            /* VK of a key that went down since the previous poll, 0 = none */
const char *plat_text_input(void);          /* typed text since the previous poll (UTF-8 limited to Latin-1) */

/* The first game controller (SDL GameController: Xbox / PlayStation / Steam Deck naming), all values 0 when none. */
typedef struct {
    int present;
    float lx, ly, rx, ry;                   /* sticks -1..1 */
    float lt, rt;                           /* triggers 0..1 */
    unsigned buttons, pressed;              /* PAD_* bits held / went down since the previous poll */
} PlatPad;
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_BACK = 64, PAD_START = 128,
       PAD_UP = 256, PAD_DOWN = 512, PAD_LEFT = 1024, PAD_RIGHT = 2048, PAD_LS = 4096, PAD_RS = 8192 };
const PlatPad *plat_pad(void);
void plat_rumble(float low, float high, int ms);

/* mouse in window pixels; buttons bit 0 = left, 1 = right */
void plat_mouse(int *x, int *y, unsigned *buttons, unsigned *clicked);

/* ---- sound ----------------------------------------------------------------------------------------------------- */
/* The mixer callback fills `frames` stereo frames of signed 16-bit at plat_audio_rate(); runs on the audio thread. */
typedef void (*PlatAudioFn)(int16_t *out, int frames, void *user);
void plat_audio_start(PlatAudioFn fn, void *user);
int  plat_audio_rate(void);
void plat_audio_lock(void);
void plat_audio_unlock(void);

/* ---- time and files -------------------------------------------------------------------------------------------- */
double plat_time(void);                     /* seconds since start, high resolution */
void   plat_sleep_ms(int ms);
FILE  *plat_fopen(const char *path, const char *mode);
int    plat_exists(const char *path);
const char *plat_user_dir(void);            /* where a2racerre.cfg / saves / log live, ends in a separator */
void   plat_set_user_dir(const char *dir);  /* portable mode: next to the program (dir ends in a separator) */
const char *plat_base_dir(void);            /* the program's folder, ends in a separator */

/* a question with up to three buttons: 1 = b1, 0 = b2, -1 = b3 (or the window closed) */
int plat_ask(const char *text, const char *b1, const char *b2, const char *b3);
/* the first start: let the user choose an ISO image or a folder with a copy of the CD.
 * PICK_PATH: path in out; PICK_FD (Android): an open file descriptor of the ISO in *fd; PICK_COPIED (Android): the
 * folder was copied into dest already; PICK_NONE: cancelled. */
enum { PICK_NONE, PICK_PATH, PICK_FD, PICK_COPIED };
int plat_pick_data(char *out, size_t cap, int *fd, const char *dest);

/* the Windows virtual-key codes (winuser.h) that the engine uses */
enum {
    VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_PAUSE = 0x13,
    VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24,
    VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E,
    VK_NUMPAD0 = 0x60, VK_MULTIPLY = 0x6A, VK_ADD = 0x6B, VK_SUBTRACT = 0x6D, VK_DECIMAL = 0x6E, VK_DIVIDE = 0x6F,
    VK_F1 = 0x70, VK_F11 = 0x7A, VK_F12 = 0x7B, VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2,
    VK_RCONTROL = 0xA3, VK_LMENU = 0xA4, VK_RMENU = 0xA5, VK_OEM_1 = 0xBA, VK_OEM_PLUS = 0xBB, VK_OEM_COMMA = 0xBC,
    VK_OEM_MINUS = 0xBD, VK_OEM_PERIOD = 0xBE, VK_OEM_2 = 0xBF, VK_OEM_3 = 0xC0, VK_OEM_4 = 0xDB, VK_OEM_5 = 0xDC,
    VK_OEM_6 = 0xDD, VK_OEM_7 = 0xDE
};

#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif
#endif
