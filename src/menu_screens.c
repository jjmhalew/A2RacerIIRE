/* menu_screens.c - the screens of menu.dat (docs/MENU.md): main menu (m_main.cpp), player slots (m_nspeler.cpp)
 * with the name request (req_nsp.cpp), garage (m_garage.cpp), showroom (m_kopen.cpp), controls (m_bediening.cpp),
 * options (m_opties.cpp), standings (m_highscore.cpp) and the yes/no requester (janeereq.cpp). Each screen is the
 * original's loop: open the screen (draw it into the back buffer, present, start the sliding buttons and title bar),
 * animate a frame, create the form once the buttons are in, then act on get_menu_event.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "menu_int.h"
#include "pad.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_X ((SCR_W - 401) / 2)      /* the dialog panel (menu anim 0x38) */
#define PANEL_Y ((SCR_H - 222) / 2)

/* the help button of every screen: the screen is saved to surface 2 (the tour lights areas from it), the music
 * turned down to -20 dB while the voices speak */
static void do_help(const HelpTable *t, int *snd)
{
    mui_wait_mouse_released();
    mui_mouse_show(0);
    mui_copy(0, 2, 0, 0, SCR_W, SCR_H);
    play_sfx(snd, "klik.wav");
    int vol = snd_get_volume(&g_music);
    snd_set_volume(&g_music, -2000);
    play_help(t, 1);
    snd_set_volume(&g_music, vol);
    mui_mouse_show(1);
}

static void close_screen(void) { mui_destroy_form(); }

/* the animated OK / Annuleren pair (mbed_add_buttons 0x403200 and its siblings) */
static void add_buttons(AnimButtons *b, MuiForm *f, const int *idx, const int *delay, int n)
{
    init_animated_buttons(b);
    for (int i = 0; i < n; i++) {
        const MuiCtrl *c = &f->c[idx[i]];
        add_animated_button(b, c->anim, -100, c->y, c->x, c->y, delay[i], c->text);
    }
}

/* ---- janeereq.cpp ----------------------------------------------------------------------------------------------- */
/* janee_open_screen 0x402430: the screen dimmed (saved to surface 2 first), the panel, message and title */
static void janee_open_screen(const char *msg, const char *title)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    mui_copy(1, 2, 0, 0, SCR_W, SCR_H);
    mui_dither(1);
    mui_blit(1, mui_anim_frame(0x38, 0), SCR_W / 2, SCR_H / 2, 0);
    mui_set_font(F_MID);
    mui_print_wrapped(1, 0xa5, 0xaa, 0x140, msg);
    mui_print_centered(1, PANEL_X + 0xb, PANEL_Y + 9, PANEL_X + 0x187, PANEL_Y + 0x22, F_STD, title);
    mui_present();
    mui_create_form(&g_form_janee);
}

int janee_request(const char *msg, const char *title)
{
    int snd = -1, result = 1;
    janee_open_screen(msg, title);
    for (int running = 1; running;) {
        int ev = get_menu_event();
        if (ev == EV_ESC) { mui_focus(1); result = 0; running = 0; }
        else if (ev == 0) running = 0;
        else if (ev == 1) { result = 0; running = 0; }
        else if (ev == EV_UP) { play_sfx(&snd, "click3.wav"); mui_focus(focus_prev(2)); }
        else if (ev == EV_DOWN) { play_sfx(&snd, "click3.wav"); mui_focus(focus_next(2)); }
    }
    close_screen();
    return result;
}

/* ---- req_nsp.cpp: the name of a new player ---------------------------------------------------------------------- */
int nsp_request(char *name, int max, const char *title)
{
    int snd = -1, cancelled = 0;
    /* nsp_req_open_screen 0x40a280 */
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    mui_copy(1, 2, 0, 0, SCR_W, SCR_H);
    mui_dither(1);
    mui_blit(1, mui_anim_frame(0x38, 0), SCR_W / 2, SCR_H / 2, 0);
    mui_set_font(F_MID);
    mui_print_wrapped(1, 0xaa, 0xaa, 0x14a, "Vul je naam in en wordt lid van de A2 racing club");
    mui_print_centered(1, PANEL_X + 0xa, PANEL_Y + 8, PANEL_X + 0x186, PANEL_Y + 0x21, F_STD, title);
    mui_present();
    mui_create_form(&g_form_nsp);
    MuiCtrl *e = &mui_form()->c[2];
    static const int COL[6] = { 0x3f, 0x7c, 0x3f, 0x7c, 0x3f, 0x6a };   /* sub_4179a0(edit, 3f, 7c, 3f, 7c, 3f, 6a) */
    memcpy(e->col, COL, sizeof COL);
    mui_redraw_ctrl(2);
    for (int running = 1; running;) {
        int ev = get_menu_event();
        if (ev == EV_ESC || ev == 1) {
            if (ev == EV_ESC) mui_focus(1);
            cancelled = 1;
            running = 0;
        } else if (ev == 0 || ev == EV_ENTER) {
            if (ev == EV_ENTER) mui_focus(0);
            /* nsp_req_get_name 0x40a430: only a name that is not empty ends the request */
            size_t n = strlen(e->buf);
            while (n && e->buf[n - 1] == ' ') e->buf[--n] = 0;      /* PORT: the pad's letter picker leaves blanks */
            if (e->buf[0]) {
                snprintf(name, (size_t)max + 1, "%s", e->buf);
                running = 0;
            }
        } else if (ev == EV_UP) { play_sfx(&snd, "click3.wav"); mui_focus(focus_prev(2)); }
        else if (ev == EV_DOWN) { play_sfx(&snd, "click3.wav"); mui_focus(focus_next(2)); }
    }
    mui_set_font(F_MID);              /* nsp_req_close_screen 0x40a3e0 */
    mui_set_colour(0x3f);
    mui_set_key(0xff);
    close_screen();
    return cancelled;
}

/* ---- m_main.cpp ------------------------------------------------------------------------------------------------- */
static void mm_show_damage_gauge(int surf)
{
    int f = cur_slot()->items[ITEM_DAMAGE] / 10;
    if (f > 9) f = 9;
    mui_blit(surf, mui_anim_frame(0x14, f), 0x15d, 0xb4, 2);
}

static void mm_show_box_titles(int surf)
{
    mui_print_centered(surf, 0xc3, 0x47, 0x262, 0x5e, F_STD, "Spelerinformatie");
    mui_print_centered(surf, 0xc3, 0x11a, 0x1cb, 0x132, F_STD, "Volgende etappe");
    mui_print_centered(surf, 0x1db, 0x11a, 0x262, 0x132, F_STD, "Besturing");
}

static void mm_show_player_info(int surf)
{
    const Slot *s = cur_slot();
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(0x1f);
    mui_print(surf, 0xcf, 0x72, "Naam");
    mui_print(surf, 0xcf, 0x91, "Budget");
    mui_print(surf, 0xcf, 0xc9, "Schade");
    mui_set_colour(0x3a);
    mui_print(surf, 0x129, 0x72, "%.10s", s->name);
    mui_print(surf, 0x129, 0x91, "Fl %d", s->money);
    mui_print(surf, 0x129, 0xc9, "%d %%", s->items[ITEM_DAMAGE]);
    mui_set_colour(keep);
}

static void mm_show_control_icon(int surf)
{
    mui_blit(surf, mui_anim_frame(0x15, cur_slot()->device), 0x1fa, 0x14d, 2);
}

/* mm_show_next_etappe 0x406e20: the road or city picture, the track name and Kwalificatie / Stedenrace */
static void mm_show_next_etappe(int surf)
{
    int e = cur_slot()->etappe, type = 0, x, y;
    if (e >= 7) e = 0;
    char name[32];
    read_track_info(e, &type, name, 30);
    int s = mui_anim_frame(0x13, type);
    int r = 0xd6 + mui_sprite_w(s), b = 0x154 + mui_sprite_h(s);
    mui_blit(surf, s, 0xd6, 0x154, 2);
    mui_get_centered(0xd6, 0x154, r, b, F_MID, name, &x, &y);
    mui_set_font(F_MID);
    mui_print(surf, x, 0x16d, "%s", name);
    const char *kind = type ? "Stedenrace" : "Kwalificatie";
    mui_get_centered(0xd6, 0x154, r, b, F_STD, kind, &x, &y);
    mui_set_font(F_STD);
    mui_print(surf, x, 0x159, "%s", kind);
}

static void mm_add_buttons(AnimButtons *b)
{
    static const int IDX[7] = { 0, 1, 2, 3, 4, 5, 6 }, DELAY[7] = { 0, 2, 2, 4, 6, 8, 10 };
    add_buttons(b, &g_form_main, IDX, DELAY, 7);
}

/* mm_open_screen 0x406fd0; full = 0 after a dialog: the screen saved under it comes back from surface 2 */
static void mm_open_screen(int full, AnimHeader *hdr, AnimButtons *btn, SpecialAnim *sa)
{
    mui_screen_tag("main");
    mui_load_pcx(1, "hoofd.pcx");
    mm_show_next_etappe(1);
    mm_show_damage_gauge(1);
    mm_show_player_info(1);
    mm_show_control_icon(1);
    mm_show_box_titles(1);
    int car = cur_slot()->car >= 0 && cur_slot()->car < 7 ? cur_slot()->car : 0;
    if (full) {
        snprintf(g_form_main.c[0].text, sizeof g_form_main.c[0].text, "%s",
                 g_game_complete && !g_new_player ? "Restart" : "Start");
        mui_present();
        mm_add_buttons(btn);
        init_animated_header(hdr, 1);
        init_anim_data(sa, 0x1b0, 0x6b, g_cars[car].anim, 0x16);
    } else {
        set_special_anim(sa, g_cars[car].anim);
        mui_copy(2, 1, 0, 0, SCR_W, SCR_H);
        mui_present();
    }
}

static void mm_close_screen(int clear)
{
    close_screen();
    if (clear) mui_fill(2, 0, 0, SCR_W, SCR_H, 0);
}

static void mm_animate_frame(AnimHeader *hdr, AnimButtons *btn, SpecialAnim *sa)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    next_animated_header_frame(hdr);
    handle_animated_buttons(btn);
    handle_special_animation(sa);
    mui_present();
}

int menu_main(int first)
{
    int snd = -1, rc = 0xff, need_form = 1;
    AnimHeader hdr;
    AnimButtons btn;
    SpecialAnim sa;
    (void)first;
    if (g_game_complete) {
        menu_highscore();
        end_of_game();
    }
    mm_open_screen(1, &hdr, &btn, &sa);
    for (int running = 1; running;) {
        /* the budget cheat 0x407706: Ctrl + Space + M held together */
        if (mui_key_held(VK_CONTROL) && mui_key_held(VK_SPACE) && mui_key_held('M')) cur_slot()->money = 700000;
        mm_animate_frame(&hdr, &btn, &sa);
        if (need_form) {
            if (btn.done) {
                mui_create_form(&g_form_main);
                need_form = 0;
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        switch (ev) {
        case 0:                                   /* Start */
            if (cur_slot()->etappe >= 7) reset_career();
            play_sfx(&snd, "motor.wav");
            mm_close_screen(0);
            rc = 0x28;
            running = 0;
            break;
        case 1:                                   /* Garage */
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            menu_garage(0);
            mm_open_screen(1, &hdr, &btn, &sa);
            need_form = 1;
            break;
        case 2:                                   /* Besturing */
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            menu_bediening();
            mm_open_screen(1, &hdr, &btn, &sa);
            need_form = 1;
            break;
        case 3:                                   /* Opties */
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            menu_opties(&g_music, g_music_names[cur_slot()->etappe % 7]);
            mm_open_screen(1, &hdr, &btn, &sa);
            need_form = 1;
            break;
        case 4:                                   /* Standen */
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            menu_highscore();
            mm_open_screen(1, &hdr, &btn, &sa);
            need_form = 1;
            break;
        case 5:                                   /* Nieuwe speler */
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            if (!janee_request("Weet je zeker dat je de race wilt onderbreken?", "Nieuwe speler")) {
                mm_open_screen(0, &hdr, &btn, &sa);
                need_form = 1;
                break;
            }
            snd_stop(&snd);                       /* (the click, not the music: the tune stays that of the old slot) */
            menu_nspeler(0);
            mm_open_screen(1, &hdr, &btn, &sa);
            need_form = 1;
            break;
        case EV_ESC:
        case 6:                                   /* Einde */
            if (ev == EV_ESC) mui_focus(6);
            play_sfx(&snd, "klik.wav");
            mm_close_screen(0);
            if (janee_request("Wil je het spel beeindigen?", "Einde")) {
                mm_open_screen(0, &hdr, &btn, &sa);
                rc = 0xff;
                running = 0;
            } else {
                mm_open_screen(0, &hdr, &btn, &sa);
                need_form = 1;
            }
            break;
        case 7:                                   /* H */
            do_help(&g_help_main, &snd);
            break;
        case EV_UP:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_prev(7));
            break;
        case EV_DOWN:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_next(7));
            break;
        default: break;
        }
    }
    mm_close_screen(1);
    return rc;
}

/* ---- m_nspeler.cpp ---------------------------------------------------------------------------------------------- */
/* msn_show_slot 0x407e80: the row (frame 1 = selected), name, car, money, etappe and total time of a used slot */
static void msn_show_slot(int surf, int idx, int selected)
{
    const MuiCtrl *row = &g_form_nspeler.c[idx];
    int i = idx - 4, x = row->x, y = row->y;
    mui_blit(surf, mui_anim_frame(0x25, selected), x, y, 2);
    const Slot *s = &g_slots[i];
    if (!s->used) return;
    mui_set_font(F_CARVED);
    mui_print(surf, x + 0xe, y + 7, "%.10s", s->name);
    mui_print(surf, x + 0xb9, y + 7, "%d", s->money);
    if (s->etappe >= 7) mui_print(surf, x + 0x113, y + 7, "-");
    else mui_print(surf, x + 0x113, y + 7, "%d", s->etappe + 1);
    char name[11];
    memcpy(name, s->name, 10);
    name[10] = 0;
    int car, t = hiscore_total_for_name(i, name, &car);
    mui_print(surf, x + 0x144, y + 7, "%02d:%02d", t / 60, t % 60);
    mui_blit(surf, mui_anim_frame(selected ? 0x26 : 0x27, s->car), x + 0x8c, y + 0xd, 0);
}

static void msn_show_slots(int surf, int sel)
{
    for (int i = 4; i <= 12; i++) msn_show_slot(surf, i, 0);
    msn_show_slot(surf, sel, 1);
}

static void msn_open_screen(int full, int sel, AnimHeader *hdr, AnimButtons *btn, int first)
{
    mui_screen_tag("nspeler");
    snprintf(g_form_nspeler.c[1].text, sizeof g_form_nspeler.c[1].text, "%s", first ? "Einde" : "Annuleren");
    if (full) {
        mui_load_pcx(1, "newgame.pcx");
        mui_set_font(F_STD);
        mui_print(1, 0xfa, 0x4a, "Naam       Auto     Geld    Etappe   Tijd");
        msn_show_slots(1, sel);
        mui_present();
        static const int IDX[2] = { 0, 1 }, DELAY[2] = { 0, 2 };
        add_buttons(btn, &g_form_nspeler, IDX, DELAY, 2);
        init_animated_header(hdr, 0);
    } else {
        mui_copy(2, 1, 0, 0, SCR_W, SCR_H);
        msn_show_slots(1, sel);
        mui_present();
    }
}

/* Verwijderen is only for a used slot that is not the one being played */
static void msn_update_delete(int sel)
{
    int i = sel - 4;
    mui_enable(3, g_slots[i].used && g_active_slot != i);
}

/* the new player of slot i: name, then the showroom (new game) and the garage; 1 = done, 0 = back to the list */
static int msn_new_player(int i, int sel, AnimHeader *hdr, AnimButtons *btn, int first, int *need_form)
{
    close_screen();
    if (nsp_request(g_slots[i].name, 8, "Nieuwe speler")) {
        msn_open_screen(0, sel, hdr, btn, first);
        *need_form = 1;
        return 0;
    }
    g_active_slot = i;
    g_slots[i].used = 1;
    slot_touch(i);
    g_new_player = 1;
    close_screen();
    if (menu_kopen(1)) return 1;
    msn_open_screen(1, sel, hdr, btn, first);
    *need_form = 1;
    return 0;
}

int menu_nspeler(int first)
{
    int snd = -1, need_form = 1, cancel = 0, auto_ok = 0, first_ev = 1;
    AnimHeader hdr;
    AnimButtons btn;
    if (first) g_active_slot = -1;
    int sel = get_last_slot() + 4;
    msn_open_screen(1, sel, &hdr, &btn, first);
    for (int running = 1; running;) {
        menu_animate_frame(&hdr, &btn);
        if (need_form) {
            if (btn.done) {
                mui_create_form(&g_form_nspeler);
                need_form = 0;
                mui_focus(0);
                msn_update_delete(sel);
                auto_ok = count_used_slots() == 0;      /* no player yet: straight to the name */
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        if (auto_ok && first_ev) { ev = 0; auto_ok = first_ev = 0; }
        if (ev == 0 || (ev >= 4 && ev <= 12 && ev == sel)) {         /* OK, or the selected row clicked again */
            if (ev == 0) play_sfx(&snd, "klik.wav");
            int i = sel - 4;
            if (!g_slots[i].used) {
                if (msn_new_player(i, sel, &hdr, &btn, first, &need_form)) return 1;
            } else {
                g_new_player = 0;
                g_active_slot = i;
                running = 0;
            }
        } else if (ev >= 4 && ev <= 12) {                           /* select a row */
            msn_show_slot(0, sel, 0);
            msn_show_slot(0, ev, 1);
            sel = ev;
            mui_focus(0);
            msn_update_delete(sel);
        } else if (ev == EV_ESC || ev == 1) {                       /* Annuleren / Einde */
            if (ev == EV_ESC) mui_focus(1);
            play_sfx(&snd, "klik.wav");
            running = 0;
            cancel = 1;
            g_new_player = 0;
        } else if (ev == 2) do_help(&g_help_nspeler, &snd);
        else if (ev == 3) {                                         /* Verwijderen */
            int i = sel - 4;
            if (g_slots[i].used && g_active_slot != i) {
                close_screen();
                if (janee_request("Weet je zeker dat je dit spel wilt verwijderen?", "Verwijderen")) {
                    delete_slot(i);
                    sel = get_last_slot() + 4;
                }
                msn_open_screen(0, sel, &hdr, &btn, first);
                need_form = 1;
            }
        } else if (ev == EV_UP) { play_sfx(&snd, "click3.wav"); mui_focus(focus_prev(2)); }
        else if (ev == EV_DOWN) { play_sfx(&snd, "click3.wav"); mui_focus(focus_next(2)); }
    }
    close_screen();
    return !cancel;
}

/* ---- m_garage.cpp ----------------------------------------------------------------------------------------------- */
static int g_highlight = -1;               /* 0x43e5f8, the item under the mouse */

static void get_current_car_props(int *perc)
{
    const Slot *s = cur_slot();
    int car = s->car >= 0 && s->car < 7 ? s->car : 0;
    for (int m = 0; m < 4; m++) perc[m] = g_cars[car].perc[m];
    for (int i = 0; i < 6; i++) {
        if (!s->items[i]) continue;
        for (int m = 0; m < 4; m++) {
            if (!g_items[i].perc[m]) continue;
            perc[m] += g_items[i].perc[m];
            if (perc[m] > 100) perc[m] = 100;
            if (perc[m] < 1) perc[m] = 1;
        }
    }
}

static int item_price(int i)
{
    int car = cur_slot()->car;
    return g_items[i].price[car >= 0 && car < 7 ? car : 0];
}

static void play_item_sound(int i)
{
    static const char *const W[6] = { "motor.wav", "banden.wav", "repair.wav", "banden.wav", "antiblok.wav", "versne~1.wav" };
    int h = -1;
    play_sfx(&h, W[i]);
}

/* show_item_button 0x403ba0: frame 0 normal, 1 highlighted (glow.wav), 2 not affordable, 3 bought / nothing to do */
static void show_item_button(int surf, int i, int frame, int price)
{
    const ItemInfo *it = &g_items[i];
    if (frame == 1) {
        int h = -1;
        play_sfx(&h, "glow.wav");
    }
    int s = mui_anim_frame(it->anim, frame);
    mui_blit(surf, s, it->x, it->y, 2);
    mui_set_font(F_STD);
    int keep = mui_get_colour(), x, y;
    mui_get_centered(it->x, it->y, it->x + 0x85, it->y + 0x6d, F_STD, it->name, &x, &y);
    mui_print(surf, x, it->y + 3, "%s", it->name);
    if (frame != 3) {
        mui_set_colour(0x3a);
        char buf[32];
        snprintf(buf, sizeof buf, "Fl %d", price);
        mui_get_centered(it->x, it->y, it->x + 0x85, it->y + 0x6d, F_STD, buf, &x, &y);
        mui_print(surf, x, it->y + mui_sprite_h(s) - 0x14, "%s", buf);
    }
    mui_set_colour(keep);
}

static void show_item_buttons(int surf)
{
    const Slot *s = cur_slot();
    for (int i = 0; i < 6; i++) {
        int frame, price = item_price(i);
        if (i == ITEM_DAMAGE) {
            price *= s->items[ITEM_DAMAGE];               /* the repair: price per % times the damage */
            frame = s->items[ITEM_DAMAGE] == 0 ? 3 : s->money >= price ? g_highlight == i : 2;
        } else frame = s->items[i] ? 3 : s->money >= price ? g_highlight == i : 2;
        show_item_button(surf, i, frame, price);
    }
}

/* show_owned_car 0x405c20: picture (frame 5 of the car's anim), name and trade-in value */
static int car_trade_in_value(const CarInfo *car);
static void show_owned_car(int surf, const CarInfo *car)
{
    mui_blit(surf, mui_anim_frame(car->anim, 5), 0x20a, 0xc7, 0);
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(0x8f);
    int v = car_trade_in_value(car);
    mui_print(surf, 0x1b5, 0x6d, "%s", car->name);
    mui_print(surf, 0x1b5, 0x79, "Waarde : Fl %d", v);
    mui_set_colour(keep);
}

/* car_trade_in_value 0x405ac0: half the price plus half of each bought upgrade, minus the repair */
static int car_trade_in_value(const CarInfo *car)
{
    const Slot *s = cur_slot();
    int v = car->price / 2;
    for (int i = 0; i < 6; i++) {
        if (i == ITEM_DAMAGE) v -= item_price(ITEM_DAMAGE) * s->items[ITEM_DAMAGE];
        else if (s->items[i]) v += item_price(i) / 2;
    }
    return v;
}

static void mr_open_screen(int full, AnimButtons *btn, AnimHeader *hdr, AnimText *tb, AnimText *td)
{
    mui_screen_tag("garage");
    const Slot *s = cur_slot();
    if (full) {
        mui_load_pcx(1, "garage.pcx");
        mui_set_font(F_STD);
        int keep = mui_get_colour();
        mui_set_colour(0x1f);
        mui_print(1, 0xee, 0x4c, "BUDGET");
        mui_print(1, 0x1ae, 0x4c, "SCHADE");
        mui_set_colour(keep);
    } else {
        mui_copy(2, 1, 0, 0, SCR_W, SCR_H);               /* sub_40e200: the screen from before the dialog */
        mui_load_pcx(2, "garage.pcx");
        mui_copy(2, 1, 0xba, 0x42, 0x26a, 0x1ce);
    }
    g_highlight = -1;
    show_item_buttons(1);
    int perc[4];
    get_current_car_props(perc);
    show_property_bars(1, 0xba, 0x66, 0x23, perc, 4);
    show_owned_car(1, &g_cars[s->car >= 0 && s->car < 7 ? s->car : 0]);
    mui_present();
    if (full) {
        static const int IDX[3] = { 0, 1, 2 }, DELAY[3] = { 0, 2, 4 };
        add_buttons(btn, &g_form_garage, IDX, DELAY, 3);
        init_animated_header(hdr, 2);
    }
    char buf[32];
    int x, y;
    snprintf(buf, sizeof buf, "Fl %d", s->money);
    mui_get_centered(0x12d, 0x4a, 0x191, 0x5c, F_STD, buf, &x, &y);
    init_anim_text(tb, buf, x, y, 0x3a, 0x3e);
    snprintf(buf, sizeof buf, "%d %%", s->items[ITEM_DAMAGE]);
    mui_get_centered(0x1f2, 0x4a, 0x256, 0x5c, F_STD, buf, &x, &y);
    init_anim_text(td, buf, x, y, 0x3a, 0x3e);
}

/* show_item_property_bars 0x4043e0: restore = put the bars back from their copy at the bottom of surface 2; else
 * every 50 ms the glowing part the highlighted item would add */
static void show_item_property_bars(int restore)
{
    static double tick;
    static int frame;
    if (g_highlight == -1) return;
    int s = mui_anim_frame(0x23, 0), w = mui_sprite_w(s), h = mui_sprite_h(s);
    if (!restore && mui_ticks() - tick < 50) return;
    mui_stretch(2, 1, 0, SCR_H - h, w, SCR_H, 0xba, 0x66, 0xba + w, 0x66 + h, 0);
    if (restore) return;
    tick = mui_ticks();
    int perc[4];
    get_current_car_props(perc);
    if (++frame >= mui_anim_count(0x36)) frame = 0;
    for (int m = 0; m < 4; m++)
        if (g_items[g_highlight].perc[m])
            show_part_of_property_bar(1, 0xba, 0x66, m, perc[m], g_items[g_highlight].perc[m], frame);
}

static void item_unhighlight(void)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    show_item_property_bars(1);
    g_highlight = -1;
    show_item_buttons(1);
    mui_present();
}

static void item_highlight(int i)
{
    const Slot *s = cur_slot();
    if ((i == ITEM_DAMAGE && s->items[ITEM_DAMAGE] == 0) || (i != ITEM_DAMAGE && s->items[i])) {
        item_unhighlight();
        return;
    }
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    show_item_property_bars(1);
    int sp = mui_anim_frame(0x23, 0), w = mui_sprite_w(sp), h = mui_sprite_h(sp);
    mui_stretch(1, 2, 0xba, 0x66, 0xba + w, 0x66 + h, 0, SCR_H - h, w, SCR_H, 0);
    g_highlight = i;
    show_item_buttons(1);
    mui_present();
}

static int item_under_mouse(void)
{
    int mx, my;
    mui_mouse(&mx, &my);
    for (int i = 0; i < 6; i++)
        if (mx >= g_items[i].x && mx <= g_items[i].x + 0x85 && my >= g_items[i].y && my <= g_items[i].y + 0x6d) return i;
    return -1;
}

static void mr_animate_frame(AnimHeader *hdr, AnimText *tb, AnimText *td, AnimButtons *btn)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    next_animated_text_frame(tb);
    next_animated_text_frame(td);
    next_animated_header_frame(hdr);
    handle_animated_buttons(btn);
    show_item_property_bars(0);
    mui_present();
}

int menu_garage(int new_game)
{
    int snd = -1, need_form = 1, cancel = 0, hover = -1;
    Slot backup = *cur_slot();
    AnimHeader hdr;
    AnimButtons btn;
    AnimText tb, td;
    mr_open_screen(1, &btn, &hdr, &tb, &td);
    for (int running = 1; running;) {
        mr_animate_frame(&hdr, &tb, &td, &btn);
        if (need_form) {
            if (btn.done && hdr.done) {
                mui_create_form(&g_form_garage);      /* mr_create_form 0x404330 */
                mui_focus(1);
                mui_enable(0, !new_game);             /* no Showroom while a new player sets up */
                need_form = 0;
            } else mui_handle_mouse();
            continue;
        }
        int it = item_under_mouse();
        /* port extra: an item focused with the keys / pad is highlighted as under the mouse */
        if (g_mui_kbd) it = mui_get_focus() >= 4 && mui_get_focus() <= 9 ? mui_get_focus() - 4 : -1;
        if (it != hover) {
            hover = it;
            if (it == -1) item_unhighlight();
            else item_highlight(it);
        }
        int ev = get_menu_event();
        Slot *s = cur_slot();
        switch (ev) {
        case 0:                                   /* Showroom */
            play_sfx(&snd, "klik.wav");
            close_screen();
            menu_kopen(0);
            mr_open_screen(1, &btn, &hdr, &tb, &td);
            need_form = 1;
            hover = -1;
            break;
        case 1:                                   /* OK */
            play_sfx(&snd, "klik.wav");
            running = 0;
            break;
        case EV_ESC:
        case 2:                                   /* Annuleren */
            if (ev == EV_ESC) mui_focus(2);
            play_sfx(&snd, "klik.wav");
            running = 0;
            cancel = 1;
            *s = backup;
            break;
        case 3:
            do_help(&g_help_garage, &snd);
            break;
        case 6: {                                 /* REPAREREN */
            int cost = item_price(ITEM_DAMAGE) * s->items[ITEM_DAMAGE];
            if (!s->items[ITEM_DAMAGE] || s->money < cost) break;
            close_screen();
            char msg[160];
            snprintf(msg, sizeof msg, "Weet je zeker dat je je auto voor Fl %d wilt laten repareren?\n\n(Budget: Fl %d)",
                     cost, s->money);
            if (janee_request(msg, "REPAREREN")) {
                play_item_sound(ITEM_DAMAGE);
                s->items[ITEM_DAMAGE] = 0;
                s->money -= cost;
            }
            mr_open_screen(0, &btn, &hdr, &tb, &td);
            need_form = 1;
            hover = -1;
            break;
        }
        case 4: case 5: case 7: case 8: case 9: {  /* MOTOR, BANDEN, TURBO, ABS, 6 VERSNELLINGEN */
            int i = ev - 4, price = item_price(i);
            if (s->money < price || s->items[i]) break;
            close_screen();
            char msg[160];
            snprintf(msg, sizeof msg, "Weet je zeker dat je dit onderdeel voor Fl %d wilt kopen?\n\n(Budget: Fl %d)",
                     price, s->money);
            if (janee_request(msg, g_items[i].name)) {
                play_item_sound(i);
                s->items[i] = 1;
                s->money -= price;
            }
            mr_open_screen(0, &btn, &hdr, &tb, &td);
            need_form = 1;
            hover = -1;
            break;
        }
        case EV_UP:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_prev(10));
            break;
        case EV_DOWN:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_next(10));
            break;
        default: break;
        }
    }
    close_screen();
    g_highlight = -1;
    return !cancel;
}

/* ---- m_kopen.cpp: the showroom -------------------------------------------------------------------------------- */
typedef struct {
    CarInfo list[6];          /* build_car_list 0x405880: every car but the owned one */
    SpecialAnim sa;
    AnimHeader hdr;
    AnimButtons btn;
    AnimText txt;
    int txt_state;
} Kopen;

/* start_new_car_text 0x4059c0: the name, then (when it has been typed) the price, each typed by the animated text;
 * the background patch under them is drawn into the back buffer first (it only shows when a frame follows before
 * the next copy of the screen into the back buffer, i.e. when browsing to another car) */
static void start_new_car_text(const CarInfo *car, AnimText *t, int *state)
{
    if (*state >= 2) return;
    mui_blit(1, mui_anim_frame(0x1b, 0), 0x1b0, 0x114, 2);
    char buf[48];
    if (*state == 0) {
        snprintf(buf, sizeof buf, "%s", car->name);
        init_anim_text(t, buf, 0x1b2, 0x116, 0x8f, 0x85);
    } else {
        snprintf(buf, sizeof buf, "Prijs : Fl %d", car->price);
        init_anim_text(t, buf, 0x1b2, 0x122, 0x8f, 0x85);
    }
    (*state)++;
}

static void mk_open_screen(Kopen *k, int owned, int cur)
{
    mui_screen_tag("kopen");
    const Slot *s = cur_slot();
    mui_load_pcx(1, "showroom.pcx");
    mui_set_font(F_STD);
    int keep = mui_get_colour(), x, y;
    char buf[32];
    snprintf(buf, sizeof buf, "Fl %d", s->money);
    mui_get_centered(0x12d, 0x4a, 0x191, 0x5c, F_STD, buf, &x, &y);
    mui_set_colour(0x1f);
    mui_print(1, 0xee, 0x4c, "BUDGET");
    mui_set_colour(0x3a);
    mui_print(1, x, y, "%s", buf);
    mui_set_colour(keep);
    int perc[4];
    get_current_car_props(perc);
    show_property_bars(1, 0xba, 0x66, 0x23, perc, 4);
    show_property_bars(1, 0xba, 0x10e, 0x24, g_cars[owned == 0].perc, 4);
    show_owned_car(1, &g_cars[owned]);
    mui_present();
    int n = 0;
    for (int c = 0; c < 7; c++)
        if (c != owned && n < 6) k->list[n++] = g_cars[c];
    init_anim_data(&k->sa, 0x1b0, 0x137, k->list[cur].anim, 0x1a);
    init_animated_header(&k->hdr, 3);
    static const int IDX[2] = { 0, 1 }, DELAY[2] = { 0, 2 };
    add_buttons(&k->btn, &g_form_kopen, IDX, DELAY, 2);
    k->txt_state = 0;
    start_new_car_text(&k->list[cur], &k->txt, &k->txt_state);
}

static void mk_animate_frame(Kopen *k)
{
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    handle_special_animation(&k->sa);
    next_animated_header_frame(&k->hdr);
    handle_animated_buttons(&k->btn);
    next_animated_text_frame(&k->txt);
    mui_present();
}

/* KOPEN is enabled when the budget covers the price minus the trade-in value of the own car */
static void mk_update_buy(const Kopen *k, int cur, int owned)
{
    int cost = k->list[cur].price - car_trade_in_value(&g_cars[owned]);
    mui_enable(3, cur_slot()->money >= cost);
}

/* browse to car `to` (scroll_special_animation slides the pictures) */
static void mk_show_car(Kopen *k, int from, int to, int dir)
{
    if (dir) scroll_special_animation(k->list[from].anim, k->list[to].anim, &k->sa, 1);
    else scroll_special_animation(k->list[to].anim, k->list[from].anim, &k->sa, 0);
    set_special_anim(&k->sa, k->list[to].anim);
    mui_copy(0, 1, 0, 0, SCR_W, SCR_H);
    show_property_bars(1, 0xba, 0x10e, 0x24, k->list[to].perc, 4);
    k->txt_state = 0;
    start_new_car_text(&k->list[to], &k->txt, &k->txt_state);
    mui_present();
}

int menu_kopen(int new_game)
{
    static Kopen kopen;
    Kopen *k = &kopen;
    int snd = -1, need_form = 1, cancel = 0, cur = 0;
    Slot *s = cur_slot();
    int owned = s->car >= 0 && s->car < 7 ? s->car : 0;
    Slot backup = *s;
    mk_open_screen(k, owned, cur);
    set_special_anim(&k->sa, k->list[0].anim);
    for (int running = 1; running;) {
        mk_animate_frame(k);
        if (k->txt.done) start_new_car_text(&k->list[cur], &k->txt, &k->txt_state);
        if (need_form) {
            if (k->btn.done && k->hdr.done) {
                need_form = 0;
                mui_create_form(&g_form_kopen);
                mui_focus(0);
                mk_update_buy(k, cur, owned);
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        switch (ev) {
        case 0:                                   /* OK */
            play_sfx(&snd, "klik.wav");
            if (new_game) {                       /* a new player continues to the garage */
                close_screen();
                if (menu_garage(1)) return 1;
                mk_open_screen(k, owned, cur);
                need_form = 1;
            } else running = 0;
            break;
        case EV_ESC:
        case 1:                                   /* Annuleren */
            if (ev == EV_ESC) mui_focus(1);
            play_sfx(&snd, "klik.wav");
            running = 0;
            cancel = 1;
            *s = backup;
            break;
        case 2: {                                 /* 1: the previous car */
            int to = cur == 0 ? 5 : cur - 1;
            mk_show_car(k, cur, to, 0);
            cur = to;
            mk_update_buy(k, cur, owned);
            break;
        }
        case 4: {                                 /* 2: the next car */
            int to = cur == 5 ? 0 : cur + 1;
            mk_show_car(k, cur, to, 1);
            cur = to;
            mk_update_buy(k, cur, owned);
            break;
        }
        case 3: {                                 /* KOPEN */
            int cost = k->list[cur].price - car_trade_in_value(&g_cars[owned]);
            if (s->money < cost) break;
            close_screen();
            char msg[200];
            if (cost > 0)
                snprintf(msg, sizeof msg, "Weet je zeker dat je je auto voor een %s wilt inruilen? \nInruilkosten: Fl %d\n"
                         "(Budget: Fl %d)", k->list[cur].name, cost, s->money);
            else
                snprintf(msg, sizeof msg, "Weet je zeker dat je je auto voor een %s wilt inruilen? \nMeerwaarde: Fl %d\n"
                         "(Budget: Fl %d)", k->list[cur].name, -cost, s->money);
            if (janee_request(msg, "KOPEN")) {
                owned = k->list[cur].idx;
                s->car = owned;
                s->money -= cost;
                if (s->money > 999999) s->money = 999999;
                for (int i = 0; i < 6; i++) s->items[i] = 0;    /* upgrades and damage go with the old car */
            }
            cur = 0;
            mk_open_screen(k, owned, cur);
            need_form = 1;
            break;
        }
        case 5:
            do_help(&g_help_kopen, &snd);
            break;
        case EV_UP:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_prev(5));
            break;
        case EV_DOWN:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_next(5));
            break;
        default: break;
        }
    }
    close_screen();
    return !cancel;
}

/* ---- m_bediening.cpp: the controls ------------------------------------------------------------------------------ */
static const int KEY_Y[6] = { 0x4d, 0x6c, 0x8b, 0xaa, 0xc9, 0xe8 };
static const char *const KEY_FN[6] = { "Links", "Rechts", "Gas", "Rem", "Turbo", "Handrem" };

static const char *key_name(int vk)
{
    for (int i = 0; i < 45; i++)
        if (g_key_names[i].vk == vk) return g_key_names[i].name;
    return "";
}

/* PORT: with the joypad or a wheel / joystick chosen, the panel shows and sets the game controller mapping of the
 * six functions instead (controller.ini, pad.h); the keys stay as they are */
static int g_panel_pad;

static void show_key(int surf, int i, int vk, int active)
{
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(active ? 0x3a : 0x64);
    const char *name = g_panel_pad ? pad_display_name(pad_bind_name(i, 0), pad_display_kind()) : key_name(vk);
    mui_print(surf, 0x210, KEY_Y[i] + 0x64, "%s", name);
    mui_set_colour(keep);
}

static void show_key_config_button(int surf, int frame)
{
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    if (frame != 2) {
        mui_blit(surf, mui_anim_frame(0x12, frame), 0x1aa, 0x92, 2);
        mui_set_colour(0x3f);
    } else mui_set_colour(0x64);
    mui_print_centered(surf, 0x1aa, 0x92, 0x25d, 0xa7, F_STD, "Instellen");
    mui_set_colour(keep);
}

/* show_key_panel 0x402ba0: "Toetsen", the six functions and their keys; greyed when the device is not the keyboard */
static void show_key_panel(int surf, int active)
{
    mui_blit(surf, mui_anim_frame(0x10, active == 0), 0x1a1, 0x64, 2);
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    if (active) { show_key_config_button(surf, 0); mui_set_colour(0x3f); }
    else { show_key_config_button(surf, 2); mui_set_colour(0x64); }
    mui_print_centered(surf, 0x1a9, 0x69, 0x25d, 0x81, F_STD, g_panel_pad && active ? "Knoppen" : "Toetsen");
    mui_set_colour(active ? 0x1f : 0x64);
    for (int i = 0; i < 6; i++) mui_print(surf, 0x1a1 + 0xa, KEY_Y[i] + 0x64, "%s", KEY_FN[i]);
    for (int i = 0; i < 6; i++) show_key(surf, i, cur_slot()->keys[i], active);
    mui_set_font(F_STD);
    mui_set_colour(keep);
}

/* show_sensitivity 0x403260: the slider, only for the wheel / joystick devices */
static void show_sensitivity(int surf, int active)
{
    mui_blit(surf, mui_anim_frame(0x11, active == 0), 0xc1, 0x156, 2);
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    if (active) {
        show_percent_slider(surf, &g_form_bedien.c[7], cur_slot()->sensitivity);
        mui_set_colour(0x3f);
    } else mui_set_colour(0x64);
    mui_print_centered(surf, 0xc9, 0x15b, 0x17d, 0x173, F_STD, "Gevoeligheid");
    mui_set_font(F_STD);
    mui_set_colour(keep);
}

/* show_device 0x403390: the four device icons as radio buttons, then the panel that goes with the device */
static void show_device(int surf, int dev)
{
    for (int i = 0; i < 4; i++) {
        const MuiCtrl *c = &g_form_bedien.c[3 + i];
        mui_blit(surf, mui_anim_frame(0xc + i, i == dev), c->x, c->y, 2);
    }
    g_panel_pad = 0;
    show_key_panel(1, 0);
    show_sensitivity(1, 0);
    g_panel_pad = dev != 0;
    show_key_panel(1, 1);                          /* PORT: for the controllers too (the original: keyboard only) */
    if (dev >= 2 && dev <= 3) show_sensitivity(1, 1);
}

static void restore_key_help_area(int surf) { mui_copy(2, surf, 0x1b5, 0x175, 0x259, 0x19d); }

static void restore_key_background(int surf, int i)
{
    int y = KEY_Y[i] + 0x64;
    mui_copy(2, surf, 0x20f, y, 0x20f + 0x4e, y + 0x12);
}

static void show_key_help_text(int surf, int i)
{
    char buf[64];
    snprintf(buf, sizeof buf, g_panel_pad ? "Knop of as voor %s" : "Druk op een toets voor %s",
             KEY_FN[i]);
    restore_key_help_area(surf);
    mui_print_wrapped(surf, 0x1b6, 0x175, 0xa3, buf);
}

/* wait_key_press 0x4027c0: a key of the table 0x42a4a8 (Esc = cancel), -1 = none this frame; the pad's B cancels */
static int wait_key_press(void)
{
    if (plat_key_pressed(VK_ESCAPE) || (plat_pad()->pressed & PAD_B)) return VK_ESCAPE;
    for (int i = 0; i < 45; i++) {
        int vk = g_key_names[i].vk;
        if (vk > 0 && vk < 256 && plat_key_pressed(vk)) return vk;
    }
    return -1;
}

/* configure_keys 0x402fa0: the six functions in turn, the current key blinking; a key already used by an earlier
 * function is refused (fout.wav), one used by a later function is swapped; Esc restores the old keys */
static void configure_keys(void)
{
    Slot *s = cur_slot();
    Slot backup = *s;
    mui_blit(2, mui_anim_frame(0x10, 0), 0x1a1, 0x64, 2);       /* show_key_panel_bg 0x402e10 */
    int snd = -1, i = 0, abort = 0;
    mui_pump();             /* get_menu_event waited for the Enter that chose Instellen to be released */
    do {
        begin_draw();
        show_key_help_text(1, i);
        if ((long)(mui_ticks() / 100) & 1) restore_key_background(1, i);
        else show_key(1, i, s->keys[i], 1);
        int k = wait_key_press();
        if (k == VK_ESCAPE) {
            *s = backup;
            show_key_panel(1, 1);
            abort = 1;
        } else if (k != -1) {
            int dup = -1;
            for (int j = 0; j < 6; j++)
                if (s->keys[j] == k && j != i) { dup = j; break; }     /* get_possible_double_key 0x402d90 */
            if (dup == -1) {
                s->keys[i] = k;
                restore_key_background(1, i);
                show_key(1, i, k, 1);
                i++;
            } else if (dup < i) play_sfx(&snd, "fout.wav");
            else {
                int t = s->keys[dup];                          /* swap_keys 0x402840 */
                s->keys[dup] = s->keys[i];
                s->keys[i] = t;
                restore_key_background(1, i);
                restore_key_background(1, dup);
                show_key(1, dup, s->keys[dup], 1);
                show_key(1, i, s->keys[i], 1);
                i++;
            }
        }
        end_draw();
        mui_pump();
    } while (i < 6 && !abort);
    begin_draw();
    restore_key_help_area(1);
    end_draw();
}

/* PORT: configure_keys for a game controller or wheel: the six functions in turn take the button, stick half,
 * trigger, hat or axis that is pressed / moved (the first element of each in controller.ini; the other elements of an
 * action stay). The same rules as for the keys: already used by an earlier function = refused, by a later one =
 * swapped. Esc or the pad's Start breaks off and restores the old mapping. */
static int configure_pad(void)
{
    int snd = -1, i = 0, abort = 0, changed = 0;
    pad_backup();
    mui_blit(2, mui_anim_frame(0x10, 0), 0x1a1, 0x64, 2);
    mui_pump();
    pad_capture_begin();
    do {
        begin_draw();
        show_key_help_text(1, i);
        if ((long)(mui_ticks() / 100) & 1) restore_key_background(1, i);
        else show_key(1, i, 0, 1);
        char el[32];
        if (plat_key_pressed(VK_ESCAPE) || (plat_pad()->pressed & PAD_START)) {
            pad_restore();
            for (int j = 0; j < 6; j++) { restore_key_background(1, j); show_key(1, j, 0, 1); }
            abort = 1;
        } else if (pad_capture(el, sizeof el)) {
            printf("controls: %s = %s\n", pad_action_key(i), el);
            int dup = -1;
            for (int j = 0; j < 6; j++) {
                const char *p = pad_bind_name(j, 0);
                if (j != i && p && !strcmp(p, el)) { dup = j; break; }
            }
            if (dup >= 0 && dup < i) play_sfx(&snd, "fout.wav");
            else {
                if (dup > i) pad_swap_primary(i, dup);
                else pad_set_primary(i, el);
                changed = 1;
                for (int j = 0; j < 6; j++) { restore_key_background(1, j); show_key(1, j, 0, 1); }
                i++;
            }
        }
        end_draw();
        mui_pump();
    } while (i < 6 && !abort);
    begin_draw();
    restore_key_help_area(1);
    end_draw();
    return changed && !abort;
}

static void mbed_open_screen(AnimHeader *hdr, AnimButtons *btn, int dev)
{
    mui_screen_tag("bediening");
    mui_load_pcx(1, "bedien.pcx");
    show_device(1, dev);
    mui_print_centered(1, 0xc9, 0x69, 0x17d, 0x81, F_STD, "Besturing");
    mui_present();
    static const int IDX[2] = { 0, 1 }, DELAY[2] = { 0, 2 };
    add_buttons(btn, &g_form_bedien, IDX, DELAY, 2);
    init_animated_header(hdr, 4);
}

/* port extra: Left / Right on the focused sensitivity slider */
static int nudge(int v, int ev)
{
    v += ev == EV_LEFT ? -5 : 5;
    return v < 1 ? 1 : v > 100 ? 100 : v;
}

int menu_bediening(void)
{
    int snd = -1, need_form = 1, cancel = 0, pad_changed = 0;
    Slot *s = cur_slot();
    Slot backup = *s;
    int dev = s->device;
    pad_load();                                    /* controller.ini as it is now (it may have been edited) */
    AnimHeader hdr;
    AnimButtons btn;
    mbed_open_screen(&hdr, &btn, dev);
    for (int running = 1; running;) {
        menu_animate_frame(&hdr, &btn);
        if (need_form) {
            if (btn.done) {
                mui_create_form(&g_form_bedien);
                need_form = 0;
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        switch (ev) {
        case 0:
            play_sfx(&snd, "klik.wav");
            running = 0;
            break;
        case EV_ESC:
        case 1:
            if (ev == EV_ESC) mui_focus(1);
            play_sfx(&snd, "klik.wav");
            running = 0;
            cancel = 1;
            break;
        case 2:
            do_help(&g_help_bedien, &snd);
            break;
        case 3: case 4: case 5: case 6:            /* keyboard, joypad, wheel / joystick, Sidewinder FF */
            dev = ev - 3;
            begin_draw();
            show_device(1, dev);
            end_draw();
            s->device = (uint8_t)dev;
            break;
        case 7:                                    /* sensitivity */
            if (dev == 2 || dev == 3) {
                if (g_mui_kbd) break;              /* the keys move it with Left / Right */
                s->sensitivity = drag_percent_slider(&g_form_bedien.c[7], s->sensitivity, &snd);
            }
            break;
        case 8:                                    /* Instellen (PORT: the controllers' too) */
            begin_draw();
            show_key_config_button(1, 1);
            end_draw();
            mui_wait_mouse_released();
            begin_draw();
            show_key_config_button(1, 0);
            end_draw();
            mui_enable(0, 0);
            mui_enable(1, 0);
            mui_focus(2);
            if (dev == 0) configure_keys();
            else if (configure_pad()) pad_changed = 1;
            mui_enable(0, 1);
            mui_enable(1, 1);
            mui_focus(0);
            break;
        case EV_LEFT:
        case EV_RIGHT:
            if (mui_get_focus() == 7 && (dev == 2 || dev == 3)) {
                s->sensitivity = nudge(s->sensitivity, ev);
                begin_draw();
                show_percent_slider(1, &g_form_bedien.c[7], s->sensitivity);
                end_draw();
            }
            break;
        case EV_UP:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_prev(2));
            break;
        case EV_DOWN:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_next(2));
            break;
        default: break;
        }
    }
    if (cancel) *s = backup;
    if (pad_changed) {                             /* configure_pad's backup is of the last Instellen only */
        if (cancel) { pad_load(); }
        else if (!pad_save()) fprintf(stderr, "controller.ini could not be written\n");
    }
    close_screen();
    return 0;
}

/* ---- m_opties.cpp: the options -------------------------------------------------------------------------------- */
static void mo_show_checkbox(int surf, int idx, int val)
{
    const MuiCtrl *c = &g_form_opties.c[idx];
    mui_blit(surf, mui_anim_frame(0x2e, val), c->x, c->y, 2);
}

static void mo_show_resolution(int surf, int res)
{
    const MuiCtrl *c = g_form_opties.c;
    mui_blit(surf, mui_anim_frame(0x2d, res == 0), c[7].x, c[7].y, 2);
    mui_blit(surf, mui_anim_frame(0x2d, res == 1), c[8].x, c[8].y, 2);
    mui_blit(surf, mui_anim_frame(0x2c, res == 3), c[9].x, c[9].y, 2);
}

static void mo_show_difficulty(int surf, int d)
{
    const MuiCtrl *c = g_form_opties.c;
    mui_blit(surf, mui_anim_frame(0x2c, d == 0), c[10].x, c[10].y, 2);
    mui_blit(surf, mui_anim_frame(0x2c, d), c[11].x, c[11].y, 2);
}

static void mo_open_screen(AnimHeader *hdr, AnimButtons *btn)
{
    mui_screen_tag("opties");
    mui_load_pcx(1, "opties.pcx");
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(0x1f);
    mui_print(1, 0xfe, 0x70, "Geluidseffecten");
    mui_print(1, 0xfe, 0xb7, "Muziek");
    mui_print(1, 0x1c4, 0x149, "3D Kaart");
    mui_print(1, 0x11f, 0x138, "320 x 200");
    mui_print(1, 0x11f, 0x154, "640 x 480");
    mui_print(1, 0x127, 0x1b2, "Makkelijk");
    mui_print(1, 0x1c4, 0x1b2, "Moeilijk");
    mui_set_colour(0x3f);
    mui_print_centered(1, 0xe9, 0x4a, 0x233, 0x63, F_STD, "GELUID");
    mui_print_centered(1, 0xea, 0x10e, 0x234, 0x127, F_STD, "GRAPHICS");
    mui_print_centered(1, 0xe9, 0x189, 0x233, 0x1a2, F_STD, "Moeilijkheidsgraad");
    mui_print(1, 0x258, 0x1cc, "v%0.2f", g_version);
    mui_set_colour(keep);
    show_percent_slider(1, &g_form_opties.c[3], g_options.sfx_vol);
    show_percent_slider(1, &g_form_opties.c[4], g_options.music_vol);
    mo_show_checkbox(1, 5, g_options.sfx_on);
    mo_show_checkbox(1, 6, g_options.music_on);
    mo_show_resolution(1, g_options.resolution);
    mo_show_difficulty(1, g_options.difficulty);
    mui_present();
    static const int IDX[2] = { 0, 1 }, DELAY[2] = { 0, 2 };
    add_buttons(btn, &g_form_opties, IDX, DELAY, 2);
    init_animated_header(hdr, 5);
}

/* revert_options 0x408b70: options.ras again, the music follows */
static void revert_options(int *music, const char *name)
{
    int was_on = g_options.music_on;
    load_options();
    if (was_on != g_options.music_on) {
        if (!g_options.music_on) snd_stop(music);
        else play_music(music, name);
    }
    snd_set_volume_pct(music, g_options.music_vol);
}

int menu_opties(int *music, const char *music_name)
{
    int snd = -1, need_form = 1, cancel = 0, res_clicked = 0;
    AnimHeader hdr;
    AnimButtons btn;
    mo_open_screen(&hdr, &btn);
    for (int running = 1; running;) {
        menu_animate_frame(&hdr, &btn);
        if (need_form) {
            if (btn.done) {
                mui_create_form(&g_form_opties);
                need_form = 0;
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        switch (ev) {
        case 0:
            play_sfx(&snd, "klik.wav");
            running = 0;
            break;
        case EV_ESC:
        case 1:
            if (ev == EV_ESC) mui_focus(1);
            play_sfx(&snd, "klik.wav");
            running = 0;
            cancel = 1;
            break;
        case 2:
            do_help(&g_help_opties, &snd);
            break;
        case 3:                                    /* sound effects volume: the click follows the slider */
            if (g_mui_kbd) break;
            play_sfx(&snd, "click3.wav");
            g_options.sfx_vol = (uint8_t)drag_percent_slider(&g_form_opties.c[3], g_options.sfx_vol, &snd);
            break;
        case 4:                                    /* music volume */
            if (g_mui_kbd) break;
            g_options.music_vol = (uint8_t)drag_percent_slider(&g_form_opties.c[4], g_options.music_vol, music);
            break;
        case 5:
            g_options.sfx_on = !g_options.sfx_on;
            mo_show_checkbox(0, 5, g_options.sfx_on);
            if (!g_options.sfx_on) snd_stop(&snd);
            else play_sfx(&snd, "click3.wav");
            break;
        case 6:
            g_options.music_on = !g_options.music_on;
            mo_show_checkbox(0, 6, g_options.music_on);
            if (!g_options.music_on) snd_stop(music);
            else play_music(music, music_name);
            break;
        case 7: case 8: case 9:                    /* 320 x 200 / 640 x 480 / 3D Kaart: RESOLUTION 0 / 1 / 3 */
            g_options.resolution = ev == 7 ? 0 : ev == 8 ? 1 : 3;
            mo_show_resolution(0, g_options.resolution);
            res_clicked = 1;
            break;
        case 10: case 11:                          /* Makkelijk / Moeilijk */
            g_options.difficulty = ev - 10;
            mo_show_difficulty(0, g_options.difficulty);
            break;
        case EV_LEFT:
        case EV_RIGHT: {                           /* port extra: the keys move a focused slider */
            int f = mui_get_focus();
            if (f != 3 && f != 4) break;
            uint8_t *v = f == 3 ? &g_options.sfx_vol : &g_options.music_vol;
            *v = (uint8_t)nudge(*v, ev);
            begin_draw();
            show_percent_slider(1, &g_form_opties.c[f], *v);
            end_draw();
            if (f == 3) play_sfx(&snd, "click3.wav");
            else snd_set_volume_pct(music, *v);
            break;
        }
        case EV_UP:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_prev(2));
            break;
        case EV_DOWN:
            play_sfx(&snd, "click3.wav");
            mui_focus(focus_next(2));
            break;
        default: break;
        }
    }
    close_screen();
    if (cancel) revert_options(music, music_name);
    else {
        save_options();
        if (res_clicked) menu_resolution_chosen();   /* PORT: from now on the chosen resolution counts (menu.c) */
    }
    return 0;
}

/* ---- m_highscore.cpp: the standings ----------------------------------------------------------------------------- */
static void mhigh_show_table(int surf, int tab)
{
    mui_copy(2, surf, 0x130, 0x67, 0x269, 0x1ca);
    const Slot *s = cur_slot();
    if (s->etappe == 0) return;
    mui_set_font(F_STD);
    int keep = mui_get_colour();
    mui_set_colour(0x3a);
    if (s->etappe >= tab)
        for (int p = 0; p < 4; p++) {
            const HiRec *r = &s->hiscore[tab][p];
            int y = 0x6c + 0x24 * p;
            mui_print(surf, 0x145, y, "%.9s", r->name);
            mui_print(surf, 0x21c, y, "%02d:%02d", r->time / 60, r->time % 60);
            mui_blit(surf, mui_anim_frame(0x39, r->car), 0x1cc, y + 0xa, 0);
        }
    mui_set_colour(keep);
}

/* mhigh_show_tabs 0x405220: TOTAAL and the seven tracks (split at '-' onto two lines), the table, the route map */
static void mhigh_show_tabs(int surf, int sel)
{
    for (int i = 0; i < 8; i++) {
        int y = 0x3b + 0x2d * i;
        mui_set_font(F_STD);
        int keep = mui_get_colour();
        mui_blit(surf, mui_anim_frame(i == sel ? 0x3b : 0x3a, i), 0xb1, y, 2);
        mui_set_colour(i == sel ? 0x1f : 0x74);
        if (i == 0) mui_print_centered(surf, 0xb6, 0x3b, 0x11d, 0x6d, F_STD, "TOTAAL");
        else {
            char name[64];
            int type;
            read_track_info(i - 1, &type, name, 0x40);
            char *dash = strchr(name, '-');
            if (!dash) mui_print_centered(surf, 0xb6, y, 0x11d, y + 0x32, F_STD, name);
            else {
                char first[64];
                snprintf(first, sizeof first, "%.*s", (int)(dash - name), name);
                mui_print_centered(surf, 0xb6, y + 7, 0x11d, y + 0x19, F_STD, first);
                mui_print_centered(surf, 0xb6, y + 0x19, 0x11d, y + 0x24, F_STD, dash);
            }
        }
        mui_set_font(F_STD);
        mui_set_colour(keep);
    }
    mhigh_show_table(surf, sel);
    mui_blit(surf, mui_anim_frame(0, sel), 0x130, 0x102, 2);
}

static void mhigh_open_screen(AnimHeader *hdr, AnimButtons *btn)
{
    mui_screen_tag("standen");
    mui_load_pcx(1, "hiscore.pcx");
    mui_copy(1, 2, 0, 0, SCR_W, SCR_H);
    mui_set_font(F_STD);
    mui_print(1, 0x133, 0x4c, "    Naam         Auto        Tijd");
    int tab = cur_slot()->hiscore_tab;
    mhigh_show_tabs(1, tab < 8 ? tab : 0);
    mui_present();
    static const int IDX[1] = { 0 }, DELAY[1] = { 0 };
    add_buttons(btn, &g_form_hiscore, IDX, DELAY, 1);
    init_animated_header(hdr, 6);
}

int menu_highscore(void)
{
    int snd = -1, need_form = 1;
    AnimHeader hdr;
    AnimButtons btn;
    mhigh_open_screen(&hdr, &btn);
    for (int running = 1; running;) {
        menu_animate_frame(&hdr, &btn);
        if (need_form) {
            if (btn.done && hdr.done) {
                need_form = 0;
                mui_create_form(&g_form_hiscore);      /* mhigh_create_form 0x405610 */
                mui_load_pcx(2, "hiscore.pcx");
            } else mui_handle_mouse();
            continue;
        }
        int ev = get_menu_event();
        if (ev == 0 || ev == EV_ESC) {
            if (ev == EV_ESC) mui_focus(0);
            play_sfx(&snd, "klik.wav");
            running = 0;
        } else if (ev == 1) {
            do_help(&g_help_hiscore, &snd);
            /* port fix: the help left a copy of the screen in surface 2, which the tables are restored from; the
             * original then shows the old table under the next one */
            mui_load_pcx(2, "hiscore.pcx");
        }
        else if (ev >= 2 && ev <= 9) {
            int tab = ev - 2;
            begin_draw();
            mhigh_show_tabs(1, tab);
            end_draw();
            cur_slot()->hiscore_tab = (uint8_t)tab;
        } else if (ev == EV_UP) { play_sfx(&snd, "click3.wav"); mui_focus(focus_prev(1)); }
        else if (ev == EV_DOWN) { play_sfx(&snd, "click3.wav"); mui_focus(focus_next(1)); }
    }
    close_screen();
    return 0;
}
