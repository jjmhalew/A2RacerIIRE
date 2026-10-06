/* game.h - the launcher a2_II.exe (docs/GAMEFLOW.md 1) in one program: the menu, then the race when the menu says
 * so (0x28), then the menu again, until the menu quits (0xFF). Like menu.dat and spel.dat, the two halves exchange the
 * career only through Game.ini / Raser.ini / menu.ini (in the user folder, res_user_path).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_GAME_H
#define A2_GAME_H

int game_run(void);                       /* the whole game; returns the process exit code */
/* the race program (spel.dat) on its own: the etappe and car of Game.ini, until it ends (state 4) or the window
 * closes; returns 0 when the window was closed */
int game_race(void);
#endif
