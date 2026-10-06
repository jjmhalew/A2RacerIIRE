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
void plat_text_mode(int on);                /* a text field has the focus (Android: the soft keyboard) */

/* Every game controller and joystick SDL sees, opened when plugged in and closed when pulled out (several at once).
 * A device SDL knows as a game controller (XInput / Xbox, DualShock 4, DualSense, Switch Pro, Steam Deck / Steam
 * Input, everything in SDL's database or in a gamecontrollerdb.txt) has its elements in SDL_GameController order and
 * Xbox naming (A = the bottom face button); anything else (wheels, pedals, flight sticks) is a raw joystick with
 * numbered axes, buttons and hats. pad.h maps them onto the race's actions. */
enum { PADKIND_GENERIC, PADKIND_XBOX, PADKIND_PS, PADKIND_SWITCH, PADKIND_STEAM };  /* how the buttons are labelled */
#define PLAT_MAX_DEVICES 8
#define PLAT_DEV_AXES 16
#define PLAT_DEV_BUTTONS 32
#define PLAT_DEV_HATS 4
enum { GP_LEFTX, GP_LEFTY, GP_RIGHTX, GP_RIGHTY, GP_LT, GP_RT, GP_NAXES };          /* = SDL_GameControllerAxis */
enum { GP_A, GP_B, GP_X, GP_Y, GP_BACK, GP_GUIDE, GP_START, GP_LS, GP_RS, GP_LB, GP_RB, GP_DPUP, GP_DPDOWN,
       GP_DPLEFT, GP_DPRIGHT, GP_MISC1, GP_PADDLE1, GP_PADDLE2, GP_PADDLE3, GP_PADDLE4, GP_TOUCHPAD, GP_NBUTTONS };
typedef struct {
    int gamepad;                            /* 1 = SDL game controller (GP_* elements), 0 = raw joystick */
    int kind;                               /* PADKIND_* */
    char name[64];
    int naxes, nbuttons, nhats;
    float axis[PLAT_DEV_AXES];              /* sticks / raw axes -1..1 (down / right positive), triggers 0..1 */
    float prev_axis[PLAT_DEV_AXES];         /* ... at the previous plat_poll */
    uint32_t buttons, prev_buttons;         /* bit i = button i (GP_* or raw button number) */
    uint8_t hat[PLAT_DEV_HATS], prev_hat[PLAT_DEV_HATS];    /* 1 up, 2 right, 4 down, 8 left (SDL_HAT_*) */
    double active;                          /* plat_time() of its last input (the last used device rumbles) */
    int can_rumble, can_spring;
} PlatDevice;
const PlatDevice *plat_device(int i);       /* i < PLAT_MAX_DEVICES; NULL = no device in that place */
int  plat_last_device(void);                /* the device used last, -1 = none */
void plat_device_rumble(int i, float low, float high, int ms);
void plat_device_spring(int i, float strength);   /* a wheel's centring spring (SDL haptic) 0..1, 0 = off */

/* All devices merged for the menus and films (the d-pad, the left stick, A / B / Start; a raw joystick's hat and
 * buttons 0 / 1 count as d-pad and A / B), all values 0 when none. */
typedef struct {
    int present;
    float lx, ly, rx, ry;                   /* sticks -1..1 */
    float lt, rt;                           /* triggers 0..1 */
    unsigned buttons, pressed;              /* PAD_* bits held / went down since the previous poll */
} PlatPad;
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_BACK = 64, PAD_START = 128,
       PAD_UP = 256, PAD_DOWN = 512, PAD_LEFT = 1024, PAD_RIGHT = 2048, PAD_LS = 4096, PAD_RS = 8192 };
const PlatPad *plat_pad(void);
void plat_rumble(float low, float high, int ms);   /* the device used last */

/* mouse in window pixels; buttons bit 0 = left, 1 = right */
void plat_mouse(int *x, int *y, unsigned *buttons, unsigned *clicked);
void plat_show_cursor(int on);              /* the system's pointer (the menu draws its own hand) */

/* Touch screens (phones, tablets, Windows touch screens): every finger by its id, in drawable pixels. A finger that
 * went down and up again between two polls (a quick tap) is listed for that one poll with down = 0, fresh = 1.
 * Touches still reach the menus as SDL's emulated mouse. */
#define PLAT_MAX_FINGERS 10
typedef struct {
    int64_t id;
    float x, y;                             /* drawable pixels */
    int down;                               /* still on the screen */
    int fresh;                              /* went down since the previous plat_poll */
} PlatFinger;
int  plat_fingers(const PlatFinger **list); /* the number of fingers in *list */
/* what was used last: PLAT_INPUT_TOUCH after a finger, PLAT_INPUT_OTHER after a key or a controller (the Android
 * back button does not count: it is how a touch player pauses), PLAT_INPUT_NONE before either */
enum { PLAT_INPUT_NONE, PLAT_INPUT_TOUCH, PLAT_INPUT_OTHER };
int  plat_input_kind(void);
void plat_touch_capture(int on);            /* 1 = the race: touches do not press the emulated mouse */
/* the accelerometer (SDL's sensor API) in m/s^2 in the device's own axes (gravity included); it is opened by the
 * first call. 0 = none */
int  plat_accel(float xyz[3]);

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
