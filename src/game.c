/* game.c - see game.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "game.h"
#include "audio.h"
#include "ini.h"
#include "menu.h"
#include "plat.h"
#include "race.h"
#include "res.h"
#include "rules.h"
#include <stdio.h>
#include <stdlib.h>

int game_race(void)
{
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    int etappe = ini_int(g, "ETAPPE", "etappe", 0), car = ini_int(g, "PLAYER", "car", 0);
    if (ini_int(g, "PLAYER", "copland", 0)) car = 6;      /* init_player_car: cop mode drives the police car */
    ini_free(g);
    if (etappe < 0 || etappe > 6) etappe = 0;
    static Race r;
    rules_loading_screen(etappe, 0);
    if (!race_init(&r, etappe, car)) {
        char m[200];
        snprintf(m, sizeof m, "Etappe %d could not be loaded.", etappe);
        plat_message(m, 1);
        return 1;
    }
    rules_loading_screen(etappe, 10);
    int alive = 1;
    while ((alive = plat_poll()) != 0) {
        int go = race_frame(&r, plat_time());
        plat_swap();
        if (!go) break;
    }
    race_free(&r);
    audio_stop_all();
    return alive;
}

int game_run(void)
{
    int first = 1;
    for (;;) {
        if (!first) menu_take_race_results();
        const char *script = getenv("A2_TEST_MENU");   /* tests: every menu gets these scripted events */
        int code = script ? menu_script(script, first) : menu_run(first);   /* a2_II.exe: menu.dat "\FIRST" */
        first = 0;
        if (code != 0x28) return 0;                    /* 0xFF = quit (exit.smk was played by the menu) */
        if (!game_race()) return 0;                    /* the window was closed */
    }
}
