/* menu_int.h - shared by menu.c (career data, start-up, ini exchange), menu_ui.c (the winlib emulation: surfaces,
 * sprites, fonts, forms, events, sounds, the small animations) and menu_screens.c (the screens of menu.dat).
 *
 * The original draws with DirectDraw into three 8-bit 640x480 surfaces that this port keeps as plain index buffers:
 *   surface 0 = the screen (primary; the form controls draw straight onto it),
 *   surface 1 = the back buffer (the screens build their picture here, then sub_40d6f0 presents it),
 *   surface 2 = a work buffer (saved backgrounds, the clean copy of a screen under a dialog, scratch for blits).
 * Every drawing call of menu.dat maps onto one of the mui_* calls below with the same arguments, so the screens read
 * like the disassembly. The palette is that of the menu screens (hoofd.pcx), also used by MENURC and the .img fonts.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_MENU_INT_H
#define A2_MENU_INT_H
#include <stdint.h>

#define SCR_W 640
#define SCR_H 480

/* ---- surfaces and sprites (winlib Clipblit.cpp / Sprite.cpp / rcpcx.cpp) -------------------------------------- */
extern uint8_t g_surf[3][SCR_W * SCR_H];
int  mui_init(void);                          /* Mrcs + MENURC + the anim table and forms of menu.dat; 0 = failed */
void mui_shutdown(void);
void mui_load_pcx(int surf, const char *name);    /* rcpcx_draw_to_surface 0x4139c0: a Mrcs screen + its palette */
/* sub_4126a0: sprite (id) at x,y; flags 2 = x,y is the top-left corner (else the hot spot), 4 = opaque */
void mui_blit(int surf, int sprite, int x, int y, int flags);
void mui_copy(int src, int dst, int x0, int y0, int x1, int y1);       /* sub_411ff0: same rect, opaque */
/* sub_4122e0: rect (sx0,sy0)-(sx1,sy1) of src stretched to (dx0,dy0)-(dx1,dy1) of dst; flags 8 = colour key 255 */
void mui_stretch(int src, int dst, int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1, int flags);
void mui_fill(int surf, int x0, int y0, int x1, int y1, int c);       /* sub_40e2b0 */
void mui_dither(int surf);                     /* sub_40fa70: 50 % black checkerboard over a whole surface */
void mui_present(void);                        /* sub_40d6f0: back buffer -> screen */
int  mui_anim_frame(int anim, int frame);      /* anim_frame 0x409c80: sprite id of a frame of the menu anim table */
int  mui_anim_count(int anim);
int  mui_sprite_w(int sprite);
int  mui_sprite_h(int sprite);

/* ---- .img fonts (ascii.cpp) --------------------------------------------------------------------------------------- */
enum { F_ASCII12, F_CYBER, F_BUT, F_STD, F_MID, F_CARVED, F_SMALLBT, F_COUNT };
void mui_set_font(int font);                   /* ascii_set_font 0x40ecf0 */
int  mui_get_colour(void);                     /* sub_40ecb0 */
void mui_set_colour(int c);                    /* 0x40f4f0: recolours the font's text pixels in place */
void mui_set_key(int c);                       /* sub_40f520: recolours the transparent pixels (opaque background) */
int  mui_font_w(void);
int  mui_font_h(void);
void mui_print(int surf, int x, int y, const char *fmt, ...);        /* sub_40f670 */
/* print_centered_text 0x40cfc0: text centred in (l,t)-(r,b) with the given font */
void mui_print_centered(int surf, int l, int t, int r, int b, int font, const char *text);
/* get_centered_text 0x40d040: where print_centered_text would put it */
void mui_get_centered(int l, int t, int r, int b, int font, const char *text, int *x, int *y);
int  mui_print_wrapped(int surf, int x, int y, int width, const char *text);   /* sub_40d130 */

/* ---- forms (Forms.cpp, Buttons.cpp, areabuts.cpp, Ownedit.cpp) ---------------------------------------------------- */
enum { CT_BUTTON = 1, CT_AREA = 4, CT_EDIT = 7 };
typedef struct {
    int x, y, type, style;                     /* the 28-byte form entry {x, y, type, ctrl*, style, 0, 0} */
    int anim, w, h;                            /* button: animation (frames normal, pressed, disabled, focus) */
    char key;                                  /* button: hot key (Alt + key) */
    char text[32];
    int font;                                  /* -1 = no text */
    int maxlen, col[6];                        /* edit box: characters, colours (text/bg normal, focus, cursor) */
    char buf[16];
    int cursor;
    int enabled;                               /* runtime */
} MuiCtrl;
typedef struct {
    MuiCtrl c[24];
    int n;
    int ring[24], nring;                       /* port extra: the controls the cursor keys / d-pad walk through */
} MuiForm;
extern MuiForm g_form_main, g_form_nspeler, g_form_garage, g_form_kopen, g_form_bedien, g_form_opties,
               g_form_hiscore, g_form_janee, g_form_nsp;
void mui_create_form(MuiForm *f);              /* create_form 0x411ba0 */
void mui_destroy_form(void);                   /* sub_411b00 */
MuiForm *mui_form(void);
void mui_focus(int idx);                       /* sub_4110f0 */
int  mui_get_focus(void);                      /* sub_4110e0 */
void mui_enable(int idx, int on);              /* sub_410fe0 / sub_411060 */
int  focus_next(int n);                        /* 0x409e00 */
int  focus_prev(int n);                        /* 0x409de0 */
void mui_redraw_ctrl(int idx);

/* events of get_menu_event 0x409d00: a control index, or these */
enum { EV_NONE = -1, EV_ESC = 0x2710, EV_UP = 0x2711, EV_DOWN = 0x2712, EV_LEFT = 0x2713, EV_RIGHT = 0x2714,
       EV_ENTER = 0x2715 };
int  get_menu_event(void);
void mui_pump(void);                           /* sub_4130b0 + one frame on the screen */
void mui_handle_mouse(void);                   /* sub_410970: the frames before the form exists */
int  mui_key_held(int vk);                     /* 0x413700 (GetAsyncKeyState) */
int  mui_mouse(int *x, int *y);                /* sub_4102e0: virtual 640x480 position, returns buttons */
void mui_mouse_show(int on);                   /* sub_410120 / sub_410160 */
void mui_wait_mouse_released(void);
double mui_ticks(void);                        /* GetTickCount, ms */
void mui_screen_tag(const char *tag);          /* --menu-shot: which screen is up */
void mui_set_shot(const char *screen, const char *png);
void mui_set_script(const char *events);       /* --menu-script */
extern int g_mui_kbd;                          /* the last input came from keys / pad (draw the focus, hide the hand) */
void mui_quit(void);                           /* the window was closed: unwind to menu_run */

/* ---- sound (Menuanim.cpp snd_* over winlib sound.cpp) ------------------------------------------------------------- */
void play_sfx(int *h, const char *wav);        /* 0x40a170 */
void play_music(int *h, const char *wav);      /* 0x40a1c0 */
void snd_stop(int *h);                         /* 0x40a0e0 */
int  snd_set_volume(int *h, int ds);           /* 0x40a210: DirectSound volume, 1/100 dB */
void snd_set_volume_pct(int *h, int pct);      /* 0x40a150: vol*30 - 3100 */
int  snd_get_volume(int *h);                   /* 0x40a250 */
int  snd_play_plain(const char *wav);          /* sub_416130(name, 0): the help voices, full volume */
int  snd_busy(int voice);
void snd_kill(int voice);
extern int g_music;                            /* 0x42f7d0, the menu music */
extern int g_sound_ok;                         /* Raser.ini [SOUND] ON */

/* ---- the small animations (special_anim.cpp) ---------------------------------------------------------------------- */
typedef struct { int frame, sine[10], done, header; double tick; } AnimHeader;
typedef struct {
    int x0, y0, x1, y1, w, h, anim, step, sx[8], sy[8];
    double tick;
    int delay, done;
    char text[31];
} AnimButton;
typedef struct { int n, done; AnimButton b[10]; } AnimButtons;
typedef struct { int frame, x, y, w, h, count, bg; double tick; int anim; } SpecialAnim;
typedef struct { int x, y, cw, ch, start, end, counter; double tick; int done; char text[256]; } AnimText;

void init_animated_header(AnimHeader *a, int header);          /* 0x40c320 */
void next_animated_header_frame(AnimHeader *a);                 /* 0x40c3b0 */
void init_animated_buttons(AnimButtons *b);                     /* 0x40c430 */
void add_animated_button(AnimButtons *b, int anim, int x0, int y0, int x1, int y1, int delay, const char *text);
void handle_animated_buttons(AnimButtons *b);                   /* 0x40c890 */
void init_anim_data(SpecialAnim *s, int x, int y, int anim, int bg);   /* 0x40bd00 */
void set_special_anim(SpecialAnim *s, int anim);                /* 0x40bc10 */
void handle_special_animation(SpecialAnim *s);                  /* 0x40bc20 */
void scroll_special_animation(int from, int to, SpecialAnim *s, int dir);   /* 0x40be30 */
void init_anim_text(AnimText *t, const char *text, int x, int y, int start, int end);   /* 0x40c250 */
void next_animated_text_frame(AnimText *t);                     /* 0x40c110 */
void show_property_bars(int surf, int x, int y, int bg_anim, const int *perc, int n);   /* 0x40c8f0 */
void show_part_of_property_bar(int surf, int x, int y, int bar, int base, int add, int frame);   /* 0x40cc80 */
/* show_percent_slider 0x40b880 / drag_percent_slider 0x40b9c0 for a form's area control */
void show_percent_slider(int surf, const MuiCtrl *c, int value);
int  drag_percent_slider(const MuiCtrl *c, int value, int *snd);
typedef struct { int n; int step[24][2]; char wav[24][16]; } HelpTable;
void play_help(const HelpTable *t, int surf);  /* 0x40fcb0 */
int  play_smacker(const char *name);           /* 0x409f70; 0 = the window was closed during the film */
void menu_animate_frame(AnimHeader *h, AnimButtons *b);         /* 0x403590 */
void begin_draw(void);                         /* 0x40bae0 */
void end_draw(void);                           /* 0x40bb10 */
void screenshot_key(void);                     /* 0x409660 */
int  janee_request(const char *msg, const char *title);         /* 0x4025a0 */
int  nsp_request(char *name, int max, const char *title);       /* 0x40a490: 1 = cancelled */

/* ---- the career (menu.c) ------------------------------------------------------------------------------------------ */
typedef struct { uint8_t car; char name[9]; int32_t time; } HiRec;   /* 14 bytes in SLOTS.RAS */
typedef struct {
    int32_t used, last_used;
    char name[10];
    int32_t car, etappe, unused, money;
    uint8_t device;
    int32_t items[6];          /* motor, banden, damage %, turbo, abs, versnellingsbak */
    int32_t keys[6];           /* VK codes: left, right, up (gas), down (brake), extra1 (turbo), extra2 (handbrake) */
    int32_t sensitivity;
    uint8_t hiscore_tab;
    HiRec hiscore[8][4];       /* table 0 = TOTAAL, table e+1 = etappe e */
} Slot;
enum { ITEM_DAMAGE = 2 };
extern Slot g_slots[9];
extern int g_active_slot;      /* 0x43ff00 */
extern int g_game_complete;    /* 0x43e70c */
extern int g_new_player;       /* 0x43e7d4 */
extern int g_start_budget;     /* 0x4354f0 */
typedef struct { int32_t sfx_on, music_on; uint8_t sfx_vol, music_vol; int32_t resolution, difficulty; } Options;
extern Options g_options;      /* 0x43e7d8, options.ras */
extern float g_version;        /* 0x43e7ec */
extern int g_movies_on, g_movie_fullscreen;

typedef struct { int idx, anim; char name[20]; int perc[4]; int price; } CarInfo;               /* 0x42df50 */
typedef struct { int anim, x, y; char name[64]; int price[7]; int perc[4]; } ItemInfo;          /* 0x42b8b8 */
extern CarInfo g_cars[7];
extern ItemInfo g_items[6];
extern char g_music_names[7][16];             /* g_music_per_etappe 0x42ef10 */
typedef struct { int vk; char name[12]; } KeyName;
extern KeyName g_key_names[45];               /* 0x42a4a8 */
extern HelpTable g_help_main, g_help_bedien, g_help_garage, g_help_hiscore, g_help_kopen, g_help_nspeler,
                 g_help_opties;
Slot *cur_slot(void);
void save_slots(void);                         /* 0x40b120 */
void save_options(void);                       /* 0x408ab0 */
void load_options(void);                       /* 0x408a40 */
void delete_slot(int i);                       /* 0x40b7f0 */
void slot_touch(int i);                        /* 0x40b820 */
int  get_last_slot(void);                      /* 0x40b710 */
int  count_used_slots(void);                   /* 0x40a6a0 */
int  hiscore_total_for_name(int slot, const char *name, int *car);    /* 0x40a6e0, total in seconds */
int  read_track_info(int n, int *type, char *name, int cap);         /* 0x406ca0 */
void reset_career(void);                       /* 0x407320 */
void start_music(void);                        /* 0x407500 */
void stop_music(void);                         /* 0x4074e0 */
void end_of_game(void);                        /* 0x407580 */

/* ---- the screens (menu_screens.c) --------------------------------------------------------------------------------- */
int  menu_main(int first);                     /* 0x4076a0: 0x28 / 0xff */
int  menu_nspeler(int first);                  /* 0x4081b0: 0 = cancelled */
int  menu_garage(int new_game);                /* 0x4046f0: 1 = OK */
int  menu_kopen(int new_game);                 /* 0x405f10: 1 = OK */
int  menu_bediening(void);                     /* 0x4035f0 */
int  menu_opties(int *music, const char *music_name);   /* 0x409190 */
int  menu_highscore(void);                     /* 0x405630 */
#endif
