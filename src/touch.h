/* touch.h - the race's on-screen controls for touch screens (PORT: the original knows only keys and joysticks).
 * Shown from the first touch until a key or controller is used (controller.ini [touch], pad.h): steering on the left
 * (left / right buttons, or a drag zone with analogue steering), gas and brake on the right with turbo and handbrake
 * above them, look back above the steering, pause and camera at the top left. Every finger works on its own
 * (steering and gas at once); a finger keeps the control it went down on, and slides between left / right and
 * between gas / brake. The controls are fed to the race as one more device of pad.h, so both steering models and
 * the in-race actions take them as they take a controller. Optional: steering by tilting the device.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_TOUCH_H
#define A2_TOUCH_H

/* what the race shows: driving, the P pause (a tap goes on), the quit question (yes / no buttons), an end screen
 * (finish results, game over: a tap goes on) */
enum { TOUCH_RACE, TOUCH_PAUSE, TOUCH_QUIT, TOUCH_END };

/* once per frame after plat_poll and before the race reads its input; speed_panel = the HUD's speed panel is drawn
 * (the gas / brake cluster keeps clear of it when the screen is not wide enough) */
void touch_frame(int mode, int speed_panel);
void touch_draw(void);                       /* on top of the overlay, in window pixels */
void touch_end(void);                        /* the race is over: no touch input left in pad.h */

int  touch_tapped(void);                     /* TOUCH_PAUSE: a finger went down this frame */
int  touch_held(void);                       /* TOUCH_END: a finger that went down on the end screen is held */
int  touch_answer(void);                     /* TOUCH_QUIT: 1 = yes was tapped, 0 = no, -1 = neither */
#endif
