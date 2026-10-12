/* menu.h - the menu program of A2 Racer II (menu.dat, docs/MENU.md): player slots, garage, showroom, controls,
 * options, standings and the films, over the original's data from the CD (Mrcs / MENURC archives, menu.dat's tables).
 * The menu and the race exchange the career through Game.ini / Raser.ini / menu.ini as menu.dat and spel.dat do
 * (docs/GAMEFLOW.md); the save games are SLOTS.RAS and options.ras, byte-compatible with the original's files. All of
 * them live in the user folder (res_user_path).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_MENU_H
#define A2_MENU_H

/* The menu program (menu_winmain 0x4098f0). first_start = the launcher's \FIRST: play intro.smk and open the player
 * screen first. Otherwise it continues with menu.ini [PLAYER] save_game and takes over the result of the race that
 * just ended (menu_take_race_results, unless that was called already). Returns 0x28 = start the race (Game.ini,
 * Raser.ini and menu.ini written as ini_write_race_setup 0x401cc0 does) or 0xFF = quit the game (after exit.smk;
 * also when the window is closed). */
int menu_run(int first_start);

/* What menu.dat does at start-up when Game.ini [PLAYER] finished != 0: read_race_results 0x402390 (the three CPU
 * racers and the player into the standings of the etappe, money, damage), advance_etappe 0x409870 and
 * hiscore_build_totals 0x40aeb0; SLOTS.RAS is saved. Nothing happens when the race was not finished. */
void menu_take_race_results(void);

/* development (--menu-shot SCREEN file.png): open one screen - main, nspeler, garage, kopen, bediening, opties,
 * standen, janee, naam - with the active slot, and write it as a 640x480 PNG once its animations have settled */
int menu_shot(const char *screen, const char *png);

/* development (--menu-script "EVENTS"): menu_run fed with scripted events instead of the input (menu_ui.c) */
int menu_script(const char *events, int first_start);
#endif
