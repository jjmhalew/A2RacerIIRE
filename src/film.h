/* film.h - plays the CD's Smacker films (intro.smk, win.smk, lose.smk, exit.smk: 320x240, 15 fps, 22 kHz sound) as the
 * menu does through smackw32.dll (docs/MENU.md); Raser.ini [MOVIES] SIZE=1 shows them full screen, 0 at half size.
 * A key, a pad button or a click skips the film.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_FILM_H
#define A2_FILM_H
/* 1 = played to the end, 0 = skipped, -1 = could not be opened, -2 = the window was closed */
int film_play(const char *name, int fullscreen);
#endif
