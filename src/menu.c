/* menu.c - see menu.h. The career data of menu.dat: the save slots (slotio.cpp), options.ras (m_opties.cpp), the
 * ini exchange with the race (ini_io.cpp), the standings (hiscore_*), and the start-up / exit sequence of
 * menu_winmain 0x4098f0 and menu_session 0x401220. The screens are in menu_screens.c, the 2D machinery in menu_ui.c.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "menu.h"
#include "menu_int.h"
#include "audio.h"
#include "exetab.h"
#include "ini.h"
#include "plat.h"
#include "res.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern jmp_buf g_menu_quit;

Slot g_slots[9];
int g_active_slot;
int g_game_complete;
int g_new_player;
int g_start_budget = -1;
Options g_options;
float g_version;
int g_movies_on = 1, g_movie_fullscreen = 1;
CarInfo g_cars[7];
ItemInfo g_items[6];
char g_music_names[7][16];
KeyName g_key_names[45];
HelpTable g_help_main, g_help_bedien, g_help_garage, g_help_hiscore, g_help_kopen, g_help_nspeler, g_help_opties;
static int g_data_ok, g_results_taken;

Slot *cur_slot(void) { return &g_slots[g_active_slot >= 0 && g_active_slot < 9 ? g_active_slot : 0]; }

static Ini *open_user_ini(const char *name)
{
    char p[1024];
    res_user_path(p, sizeof p, name);
    return ini_load(p);
}

static void save_user_ini(Ini *ini, const char *name)
{
    char p[1024];
    res_user_path(p, sizeof p, name);
    if (!ini_save(ini, p)) fprintf(stderr, "menu: cannot write %s\n", p);
}

/* ---- SLOTS.RAS: 9 x 536 packed bytes (slotio.cpp) ----------------------------------------------------------------- */
#define SLOT_SIZE 536
static int32_t get32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void put32(uint8_t *p, int32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void slot_from_bytes(Slot *s, const uint8_t *b)
{
    s->used = get32(b);
    s->last_used = get32(b + 4);
    memcpy(s->name, b + 8, 10);
    s->car = get32(b + 0x12);
    s->etappe = get32(b + 0x16);
    s->unused = get32(b + 0x1a);
    s->money = get32(b + 0x1e);
    s->device = b[0x22];
    for (int i = 0; i < 6; i++) s->items[i] = get32(b + 0x23 + 4 * i);
    for (int i = 0; i < 6; i++) s->keys[i] = get32(b + 0x3b + 4 * i);
    s->sensitivity = get32(b + 0x53);
    s->hiscore_tab = b[0x57];
    for (int t = 0; t < 8; t++)
        for (int p = 0; p < 4; p++) {
            const uint8_t *r = b + 0x58 + (t * 4 + p) * 14;
            s->hiscore[t][p].car = r[0];
            memcpy(s->hiscore[t][p].name, r + 1, 9);
            s->hiscore[t][p].time = get32(r + 10);
        }
}

static void slot_to_bytes(const Slot *s, uint8_t *b)
{
    put32(b, s->used);
    put32(b + 4, s->last_used);
    memcpy(b + 8, s->name, 10);
    put32(b + 0x12, s->car);
    put32(b + 0x16, s->etappe);
    put32(b + 0x1a, s->unused);
    put32(b + 0x1e, s->money);
    b[0x22] = s->device;
    for (int i = 0; i < 6; i++) put32(b + 0x23 + 4 * i, s->items[i]);
    for (int i = 0; i < 6; i++) put32(b + 0x3b + 4 * i, s->keys[i]);
    put32(b + 0x53, s->sensitivity);
    b[0x57] = s->hiscore_tab;
    for (int t = 0; t < 8; t++)
        for (int p = 0; p < 4; p++) {
            uint8_t *r = b + 0x58 + (t * 4 + p) * 14;
            r[0] = s->hiscore[t][p].car;
            memcpy(r + 1, s->hiscore[t][p].name, 9);
            put32(r + 10, s->hiscore[t][p].time);
        }
}

void save_slots(void)
{
    uint8_t buf[9 * SLOT_SIZE];
    for (int i = 0; i < 9; i++) slot_to_bytes(&g_slots[i], buf + i * SLOT_SIZE);
    char p[1024];
    res_user_path(p, sizeof p, "SLOTS.RAS");
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(buf, 1, sizeof buf, f) != sizeof buf) fprintf(stderr, "menu: cannot write %s\n", p);
    if (f) fclose(f);
}

static int load_slots(void)
{
    uint8_t buf[9 * SLOT_SIZE];
    char p[1024];
    res_user_path(p, sizeof p, "SLOTS.RAS");
    FILE *f = plat_fopen(p, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != sizeof buf) return 0;
    for (int i = 0; i < 9; i++) slot_from_bytes(&g_slots[i], buf + i * SLOT_SIZE);
    return 1;
}

/* init_slot 0x40b1f0: an empty slot with the start budget, the default keys and empty standings */
static void init_slot(int i)
{
    Slot *s = &g_slots[i];
    s->used = 0;
    s->car = 0;
    s->etappe = 0;
    s->unused = 0;
    s->name[0] = 0;
    s->last_used = 0;
    s->money = g_start_budget;
    s->device = 0;
    for (int k = 0; k < 6; k++) s->items[k] = 0;
    static const int KEYS[6] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_CONTROL, VK_MENU };
    for (int k = 0; k < 6; k++) s->keys[k] = KEYS[k];
    s->sensitivity = 50;
    s->hiscore_tab = 0;
    for (int t = 0; t < 8; t++)
        for (int p = 0; p < 4; p++) {
            s->hiscore[t][p].car = 0;
            s->hiscore[t][p].name[0] = 0;
            s->hiscore[t][p].time = 0;
        }
}

/* init_slots 0x40b6c0: SLOTS.RAS, created with empty slots when missing */
static void init_slots(void)
{
    g_active_slot = 0;
    char p[1024];
    res_user_path(p, sizeof p, "SLOTS.RAS");
    if (!plat_exists(p) || !load_slots()) {
        memset(g_slots, 0, sizeof g_slots);
        for (int i = 0; i < 9; i++) init_slot(i);
        save_slots();
    }
}

int get_last_slot(void)
{
    int best = -1, slot = 0;
    for (int i = 0; i < 9; i++)
        if (g_slots[i].last_used > best) { best = g_slots[i].last_used; slot = i; }
    return slot;
}

/* slot_fix_order 0x40b780: meant to close the gap in the 'last used' numbers; compares the slot *index* with the
 * counters (sic), kept */
static void slot_fix_order(int i)
{
    for (int k = 0; k < 9; k++)
        if (i < g_slots[k].last_used) g_slots[k].last_used--;
}

void delete_slot(int i)
{
    slot_fix_order(i);
    init_slot(i);
}

void slot_touch(int i) { g_slots[i].last_used = g_slots[get_last_slot()].last_used + 1; }

int count_used_slots(void)
{
    int n = 0;
    for (int i = 0; i < 9; i++) n += g_slots[i].used != 0;
    return n;
}

/* ---- options.ras: {int sfx_on, music_on; u8 sfx_vol, music_vol; int resolution, difficulty} (18 bytes) ------------ */
void save_options(void)
{
    uint8_t b[18];
    put32(b, g_options.sfx_on);
    put32(b + 4, g_options.music_on);
    b[8] = g_options.sfx_vol;
    b[9] = g_options.music_vol;
    put32(b + 10, g_options.resolution);
    put32(b + 14, g_options.difficulty);
    char p[1024];
    res_user_path(p, sizeof p, "options.ras");
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(b, 1, 18, f) != 18) fprintf(stderr, "menu: cannot write %s\n", p);
    if (f) fclose(f);
}

static void init_options(void);

/* PORT: the resolution of options.ras counts only once the player has chosen one on the options screen (its OK after
 * a click on one of the three radio buttons writes Raser.ini [PROGRAM] RESOLUTION_CHOSEN=1); until then the menu shows
 * and starts the races in "3D Kaart" (RESOLUTION 3). init_options 0x408b00 creates options.ras with 320 x 200 (0),
 * the safe choice of 1998 for a PC without a 3D card, so every options.ras this port has written so far holds 0 and
 * would otherwise turn a race nobody configured into the 320x200 software look. */
static int resolution_chosen(void)
{
    Ini *r = open_user_ini("Raser.ini");
    int v = ini_int(r, "PROGRAM", "RESOLUTION_CHOSEN", 0);
    ini_free(r);
    return v != 0;
}

static void resolution_rule(void)
{
    if (!resolution_chosen()) g_options.resolution = 3;
}

void menu_resolution_chosen(void)
{
    Ini *r = open_user_ini("Raser.ini");
    ini_set_int(r, "PROGRAM", "RESOLUTION_CHOSEN", 1);
    save_user_ini(r, "Raser.ini");
    ini_free(r);
}

void load_options(void)
{
    char p[1024];
    res_user_path(p, sizeof p, "options.ras");
    FILE *f = plat_fopen(p, "rb");
    uint8_t b[18];
    size_t n = f ? fread(b, 1, 18, f) : 0;
    if (f) fclose(f);
    if (n != 18) { init_options(); return; }
    g_options.sfx_on = get32(b);
    g_options.music_on = get32(b + 4);
    g_options.sfx_vol = b[8];
    g_options.music_vol = b[9];
    g_options.resolution = get32(b + 10);
    g_options.difficulty = get32(b + 14);
    resolution_rule();
}

/* init_options 0x408b00: defaults sfx and music on at 80, resolution 0, difficulty 1, written when missing */
static void init_options(void)
{
    char p[1024];
    res_user_path(p, sizeof p, "options.ras");
    if (plat_exists(p)) {
        FILE *f = plat_fopen(p, "rb");
        long n = 0;
        if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fclose(f); }
        if (n == 18) { load_options(); return; }
    }
    g_options.sfx_on = g_options.music_on = 1;
    g_options.sfx_vol = g_options.music_vol = 80;
    g_options.resolution = 0;
    g_options.difficulty = 1;
    save_options();
    resolution_rule();
}

/* ---- ini_io.cpp ------------------------------------------------------------------------------------------------- */
/* ini_read_car_props 0x4014c0 / ini_read_item_props 0x401610: garage.ini prices and the bar percentages
 * (v - min) / (max - min) * 100 with (100, 300) for the speed and (0, 100) for the rest, cars clamped 1..100,
 * items 0..100 */
static int perc_of(int v, int bar, int lo, int sub_min)
{
    static const float MIN[4] = { 100, 0, 0, 0 }, MAX[4] = { 300, 100, 100, 100 };
    float f = v - (sub_min ? MIN[bar] : 0);      /* an item's value is an increase: no minimum taken off */
    if (f < 0) f = 0;
    float range = MAX[bar] - MIN[bar];
    if (f > range) f = range;
    int r = (int)(f / range * 100);
    return r < lo ? lo : r > 100 ? 100 : r;
}

static void ini_read_all(void)
{
    char p[1024];
    res_path(p, sizeof p, "garage.ini");
    Ini *g = ini_load(p);
    char k[32];
    for (int i = 0; i < 6; i++) {
        for (int c = 0; c < 7; c++) {
            snprintf(k, sizeof k, "item%d_car%d", i + 1, c + 1);
            g_items[i].price[c] = ini_int(g, "prices", k, 0);
        }
        for (int m = 0; m < 4; m++) {
            snprintf(k, sizeof k, "item%d_perc%d", i + 1, m + 1);
            g_items[i].perc[m] = perc_of(ini_int(g, "percents", k, 0), m, 0, 0);
        }
    }
    for (int c = 0; c < 7; c++) {
        snprintf(k, sizeof k, "car%d", c + 1);
        g_cars[c].price = ini_int(g, "prices", k, 0);
        for (int m = 0; m < 4; m++) {
            snprintf(k, sizeof k, "car%d_perc%d", c + 1, m + 1);
            g_cars[c].perc[m] = perc_of(ini_int(g, "percents", k, 0), m, 1, 1);
        }
    }
    ini_free(g);
    Ini *mi = open_user_ini("menu.ini");
    g_start_budget = ini_int(mi, "PLAYER", "start_budget", 0);
    ini_free(mi);
    Ini *r = open_user_ini("Raser.ini");
    char v[64];
    ini_str(r, "program", "version", "", v, sizeof v);
    g_version = v[0] ? (float)atof(v) : 0;
    g_movie_fullscreen = ini_int(r, "movies", "size", 0);
    g_movies_on = ini_int(r, "movies", "on", 0);
    g_sound_ok = ini_int(r, "SOUND", "ON", 0) != 0;
    ini_free(r);
}

/* read_track_info 0x406ca0: Raser.ini [maps] n = type, files..., name (arg 8), music, timer */
int read_track_info(int n, int *type, char *name, int cap)
{
    Ini *r = open_user_ini("Raser.ini");
    char k[8], line[256];
    snprintf(k, sizeof k, "%d", n);
    ini_str(r, "maps", k, "", line, sizeof line);
    ini_free(r);
    char *arg[16];
    int na = 0;
    for (char *s = line; na < 16;) {
        arg[na++] = s;
        char *c = strchr(s, ',');
        if (!c) break;
        *c = 0;
        s = c + 1;
    }
    *type = na > 0 ? atoi(arg[0]) : 0;
    snprintf(name, (size_t)cap, "%s", na > 8 ? arg[8] : "");
    for (char *s = name; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s -= 32;                 /* _strupr */
    return na == 11;
}

/* ini_write_race_setup 0x401cc0: everything the race reads */
static void ini_write_race_setup(void)
{
    Slot *s = cur_slot();
    /* ini_write_options 0x401820 */
    Ini *r = open_user_ini("Raser.ini");
    ini_set_int(r, "Program", "RESOLUTION", g_options.resolution);
    ini_set_int(r, "Sound", "SFXVOLUME", g_options.sfx_on ? 3100 - 30 * g_options.sfx_vol : 0);
    ini_set_int(r, "Sound", "MUSICVOLUME", g_options.music_on ? 3100 - 30 * g_options.music_vol : 0);
    save_user_ini(r, "Raser.ini");
    ini_free(r);
    Ini *g = open_user_ini("Game.ini");
    ini_set_int(g, "OPTIONS", "Difficulty", g_options.difficulty);
    /* ini_write_player_items 0x4018f0 (the name is upper-cased in place by _strupr, sic) */
    static const char *const ITEM[6] = { "MOTOR", "BANDEN", "DAMAGE", "TURBO", "ABS", "VERSNELLINGSBAK" };
    for (int i = 0; i < 6; i++) ini_set_int(g, "PLAYER", ITEM[i], s->items[i]);
    ini_set_int(g, "PLAYER", "Car", s->car);
    for (char *c = s->name; *c && c < s->name + 10; c++)
        if (*c >= 'a' && *c <= 'z') *c -= 32;
    ini_set_int(g, "PLAYER", "COPLAND", strncmp(s->name, "COPLAND", 10) == 0);
    /* ini_write_controls 0x401b20: device 0..3 -> 1, 2, 3, 3 */
    static const char *const KEY[6] = { "LEFT", "RIGHT", "UP", "DOWN", "EXTRA1", "EXTRA2" };
    for (int i = 0; i < 6; i++) ini_set_int(g, "INPUT", KEY[i], s->keys[i]);
    if (s->device <= 3) ini_set_int(g, "INPUT", "Device", s->device == 0 ? 1 : s->device == 1 ? 2 : 3);
    ini_set_int(g, "INPUT", "Sensitivity", s->sensitivity);
    ini_set_int(g, "PLAYER", "money", s->money);              /* ini_write_money 0x401a50 */
    ini_set_int(g, "etappe", "etappe", s->etappe);            /* ini_write_etappe 0x401c70 */
    save_user_ini(g, "Game.ini");
    ini_free(g);
    Ini *m = open_user_ini("menu.ini");                       /* ini_write_save_game 0x401aa0 */
    ini_set_int(m, "PLAYER", "save_game", g_active_slot);
    save_user_ini(m, "menu.ini");
    ini_free(m);
}

static void set_hirec(HiRec *h, int car, const char *name, int time)
{
    h->car = (uint8_t)car;
    memset(h->name, 0, sizeof h->name);
    snprintf(h->name, sizeof h->name, "%s", name);
    h->time = time;
}

/* read_race_results 0x402390 = ini_read_racer_results 0x401d30 + ini_read_player_result 0x401ff0 +
 * ini_read_damage 0x401ce0: into the standings of the etappe just driven (table etappe + 1) */
static void read_race_results(void)
{
    Slot *s = cur_slot();
    int t = s->etappe + 1;
    Ini *g = open_user_ini("Game.ini");
    if (ini_int(g, "racers", "COUNT", 0) != 3) fprintf(stderr, "menu: Game.ini [RACERS] COUNT is not 3\n");
    for (int n = 0; n < 3; n++) {
        char k[8], v[128], name[16];
        snprintf(k, sizeof k, "%d", n);
        ini_str(g, "racers", k, "", v, sizeof v);
        int car = 0, pos = 0, ms = 0;
        sscanf(v, "%d , %d , %d", &car, &pos, &ms);
        snprintf(name, sizeof name, "racer %d", n + 1);
        if (t >= 0 && t < 8 && pos >= 0 && pos < 4) set_hirec(&s->hiscore[t][pos], car, name, ms / 1000);
    }
    int money = ini_int(g, "PLAYER", "money", 0);
    s->money = money < 0 ? 0 : money > 999999 ? 999999 : money;
    int ms = ini_int(g, "PLAYER", "tracktime", 0), pos = ini_int(g, "PLAYER", "position", 0);
    if (t >= 0 && t < 8 && pos >= 0 && pos < 4) {
        char name[10];
        memcpy(name, s->name, 9);
        name[9] = 0;
        set_hirec(&s->hiscore[t][pos], s->car, name, ms / 1000);
    }
    s->items[ITEM_DAMAGE] = ini_int(g, "PLAYER", "damage", 0);
    ini_free(g);
}

/* advance_etappe 0x409870 */
static void advance_etappe(void)
{
    Slot *s = cur_slot();
    g_game_complete = s->etappe >= 6;
    s->etappe++;
}

/* ---- the standings (slotio.cpp) --------------------------------------------------------------------------------- */
/* hiscore_total_for_name 0x40a6e0: the sum of the name's times over tables 1..etappe; the car of the last table */
int hiscore_total_for_name(int slot, const char *name, int *car)
{
    const Slot *s = &g_slots[slot];
    int total = 0, e = s->etappe;
    for (int t = 1; t < e + 1 && t < 8; t++)
        for (int p = 0; p < 4; p++)
            if (!strncmp(name, s->hiscore[t][p].name, 9)) total += s->hiscore[t][p].time;
    *car = 0;
    if (e >= 0 && e < 8)
        for (int p = 0; p < 4; p++)
            if (!strncmp(name, s->hiscore[e][p].name, 9)) *car = s->hiscore[e][p].car;
    return total;
}

/* hiscore_insert_total 0x40a970: before the first place that has a longer time, the rest moves down */
static void hiscore_insert_total(const HiRec *r)
{
    HiRec *t = cur_slot()->hiscore[0];
    int at = -1;
    for (int p = 0; p < 4; p++)
        if (r->time < t[p].time) { at = p; break; }
    if (at < 0) return;
    for (int b = 3; b > at; b--) t[b] = t[b - 1];
    t[at] = *r;
}

/* hiscore_build_totals 0x40aeb0: TOTAAL from the three CPU racers and the player */
static void hiscore_build_totals(void)
{
    HiRec e[4];
    for (int n = 0; n < 4; n++) {
        char name[16];
        if (n < 3) snprintf(name, sizeof name, "racer %d", n + 1);
        else snprintf(name, sizeof name, "%.9s", cur_slot()->name);
        int car;
        int total = hiscore_total_for_name(g_active_slot, name, &car);
        memset(&e[n], 0, sizeof e[n]);
        snprintf(e[n].name, sizeof e[n].name, "%s", name);
        e[n].car = (uint8_t)car;
        e[n].time = total;
    }
    for (int p = 0; p < 4; p++) cur_slot()->hiscore[0][p].time = 0x0fffffff;
    for (int n = 0; n < 4; n++) hiscore_insert_total(&e[n]);
}

/* reset_career 0x407320: back to etappe 0 with empty standings (car, money and upgrades stay) */
void reset_career(void)
{
    Slot *s = cur_slot();
    s->etappe = 0;
    s->unused = 0;
    for (int t = 0; t < 8; t++)
        for (int p = 0; p < 4; p++) {
            s->hiscore[t][p].car = 0;
            s->hiscore[t][p].name[0] = 0;
            s->hiscore[t][p].time = 0;
        }
}

/* ---- music and films (m_main.cpp) ------------------------------------------------------------------------------- */
void stop_music(void)
{
    if (g_options.music_on && g_music != -1) snd_stop(&g_music);
}

/* start_music 0x407500: the tune of the etappe, looped */
void start_music(void)
{
    if (!g_options.music_on) return;
    int e = cur_slot()->etappe % 7;
    if (e < 0) e = 0;
    play_music(&g_music, g_music_names[e]);
}

/* end_of_game 0x407580: after the 7th etappe win.smk when the player leads TOTAAL, else lose.smk; then the career
 * starts again at etappe 0 (g_game_complete stays set, so Start keeps reading "Restart") */
void end_of_game(void)
{
    if (!g_game_complete) return;
    stop_music();
    Slot *s = cur_slot();
    if (!strncmp(s->hiscore[0][0].name, s->name, 9)) play_smacker("win");
    else play_smacker("lose");
    s->etappe = 0;
    start_music();
}

/* ---- menu.dat's static tables ----------------------------------------------------------------------------------- */
static void read_help(const PeFile *pe, uint32_t va, HelpTable *h)
{
    h->n = 0;
    for (int i = 0; i < 24; i++) {
        int idx = (int)pe_u32(pe, va + (uint32_t)i * 8);
        if (idx == -1) break;
        const char *w = pe_str(pe, pe_u32(pe, va + (uint32_t)i * 8 + 4));
        h->step[h->n][0] = idx;
        snprintf(h->wav[h->n], sizeof h->wav[0], "%s", w ? w : "");
        h->n++;
    }
}

static int read_tables(void)
{
    PeFile pe;
    if (!pe_open(&pe, "menu.dat")) return 0;
    for (int c = 0; c < 7; c++) {                 /* g_car_table 0x42df50: 7 x 48 {idx, anim, name[20], ...} */
        uint32_t a = 0x42df50 + (uint32_t)c * 48;
        g_cars[c].idx = (int)pe_u32(&pe, a);
        g_cars[c].anim = (int)pe_u32(&pe, a + 4);
        const char *n = pe_str(&pe, a + 8);
        snprintf(g_cars[c].name, sizeof g_cars[c].name, "%s", n ? n : "");
    }
    for (int i = 0; i < 6; i++) {                 /* g_item_table 0x42b8b8: 6 x 0x78 {anim, x, y, name[64], ...} */
        uint32_t a = 0x42b8b8 + (uint32_t)i * 0x78;
        g_items[i].anim = (int)pe_u32(&pe, a);
        g_items[i].x = (int)pe_u32(&pe, a + 4);
        g_items[i].y = (int)pe_u32(&pe, a + 8);
        const char *n = pe_str(&pe, a + 12);
        snprintf(g_items[i].name, sizeof g_items[i].name, "%s", n ? n : "");
    }
    for (int e = 0; e < 7; e++) {                 /* g_music_per_etappe 0x42ef10 */
        const char *n = pe_str(&pe, pe_u32(&pe, 0x42ef10 + (uint32_t)e * 4));
        snprintf(g_music_names[e], sizeof g_music_names[e], "%s", n ? n : "speed.wav");
    }
    for (int k = 0; k < 45; k++) {                /* the key names 0x42a4a8 {vk, char*} */
        g_key_names[k].vk = (int)pe_u32(&pe, 0x42a4a8 + (uint32_t)k * 8);
        const char *n = pe_str(&pe, pe_u32(&pe, 0x42a4ac + (uint32_t)k * 8));
        snprintf(g_key_names[k].name, sizeof g_key_names[k].name, "%s", n ? n : "");
    }
    read_help(&pe, 0x42eeb0, &g_help_main);       /* the help tours {form index, wav} ending with -1 */
    read_help(&pe, 0x42a430, &g_help_bedien);
    read_help(&pe, 0x42b7e8, &g_help_garage);
    read_help(&pe, 0x42c468, &g_help_hiscore);
    read_help(&pe, 0x42db68, &g_help_kopen);
    read_help(&pe, 0x430228, &g_help_nspeler);
    read_help(&pe, 0x431190, &g_help_opties);
    pe_close(&pe);
    return 1;
}

/* the start-up of menu_winmain up to the screens: ini_read_all, init_options, init_slots, init_sound */
static int data_init(void)
{
    if (g_data_ok) return 1;
    if (!read_tables()) { fprintf(stderr, "menu: menu.dat missing\n"); return 0; }
    g_game_complete = g_new_player = 0;              /* a new menu.dat process */
    ini_read_all();
    init_options();
    init_slots();
    if (!g_sound_ok) g_options.sfx_on = g_options.music_on = 0;    /* init_sound 0x409730: [SOUND] ON=0 */
    g_data_ok = 1;
    return 1;
}

/* ini_read_save_game 0x401ad0 */
static void ini_read_save_game(void)
{
    Ini *m = open_user_ini("menu.ini");
    g_active_slot = ini_int(m, "PLAYER", "save_game", 0);
    ini_free(m);
    if (g_active_slot < 0 || g_active_slot >= 9) g_active_slot = 0;
}

void menu_take_race_results(void)
{
    if (!data_init()) return;
    ini_read_save_game();
    Ini *g = open_user_ini("Game.ini");
    int finished = ini_int(g, "PLAYER", "finished", 0);          /* ini_read_finished 0x4023a0 */
    ini_free(g);
    g_results_taken = 1;
    if (!finished) return;
    read_race_results();
    advance_etappe();
    hiscore_build_totals();
    save_slots();
}

/* menu_session 0x401220: with \FIRST the player screen first (cancel = quit), then the main menu */
static int menu_session(int first)
{
    memset(g_surf, 0, sizeof g_surf);                            /* menu_mouse_on: Clear_screen */
    mui_mouse_show(1);
    int rc;
    if (first && !menu_nspeler(first)) rc = 0xff;
    else rc = menu_main(first);
    memset(g_surf, 0, sizeof g_surf);                            /* menu_mouse_off */
    return rc;
}

int menu_run(int first_start)
{
    static int rc;
    rc = 0xff;
    if (!data_init() || !mui_init()) { plat_message("The menu files (menu.dat, Mrcs, MENURC) are missing.", 1); return 0xff; }
    if (setjmp(g_menu_quit)) {                                   /* the window was closed */
        rc = 0xff;
        save_slots();
        goto done;
    }
    if (first_start) {
        play_smacker("intro");
        mui_fill(1, 0, 0, SCR_W, SCR_H, 0);
        mui_present();
    } else {
        if (!g_results_taken) menu_take_race_results();
        ini_read_save_game();
    }
    start_music();
    rc = menu_session(first_start);
    if (rc == 0x28) {
        ini_write_race_setup();
        save_slots();
    } else {
        stop_music();
        play_smacker("exit");
        mui_fill(1, 0, 0, SCR_W, SCR_H, 0);
        mui_present();
        rc = 0xff;
        save_slots();
    }
done:
    audio_stop_all();
    g_music = -1;
    mui_shutdown();
    g_results_taken = 0;
    g_data_ok = 0;            /* the next run reads the files again, as a new menu.dat process would */
    return rc;
}

int menu_script(const char *events, int first_start)
{
    mui_set_script(events);
    int rc = menu_run(first_start);
    mui_set_script("");
    return rc;
}

int menu_shot(const char *shot, const char *png)
{
    if (!data_init() || !mui_init()) return 1;
    ini_read_save_game();
    mui_set_shot(shot, png);
    char screen[32];                                   /* "main@300": main, 300 ms after it opened */
    snprintf(screen, sizeof screen, "%.*s", (int)strcspn(shot, "@"), shot);
    if (!setjmp(g_menu_quit)) {
        memset(g_surf, 0, sizeof g_surf);
        start_music();
        if (!_stricmp(screen, "main")) menu_main(0);
        else if (!_stricmp(screen, "nspeler")) menu_nspeler(0);
        else if (!_stricmp(screen, "garage")) menu_garage(0);
        else if (!_stricmp(screen, "kopen")) menu_kopen(0);
        else if (!_stricmp(screen, "bediening")) menu_bediening();
        else if (!_stricmp(screen, "opties")) menu_opties(&g_music, g_music_names[cur_slot()->etappe % 7]);
        else if (!_stricmp(screen, "standen")) menu_highscore();
        else if (!_stricmp(screen, "janee") || !_stricmp(screen, "naam")) {
            mui_load_pcx(1, "hoofd.pcx");
            mui_present();
            mui_screen_tag(screen);
            if (!_stricmp(screen, "janee")) janee_request("Wil je het spel beeindigen?", "Einde");
            else {
                char name[10] = "";
                nsp_request(name, 8, "Nieuwe speler");
            }
        } else fprintf(stderr, "menu-shot: unknown screen %s\n", screen);
    }
    audio_stop_all();
    g_music = -1;
    mui_shutdown();
    g_data_ok = 0;
    return 0;
}
