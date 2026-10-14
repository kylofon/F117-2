/* Sound call points of the front end. S5.9 (sound) fills these in: until then every call is a no-op that returns what a silent
 * driver returns. Call sites: SETUP none; MPS_LOGO snd_init_log / snd_cmd_log(2) / snd_cmd_log(0) / snd_shutdown_log (the .LOG
 * overlay, sound.md "11"); PLAYER fe_snd_cmd(3) at the first poll_input (the intro soundtrack), fe_snd_cmd(0) at player_exit,
 * fe_snd_cmd(2) polled before exit when several files were played, fe_snd_poll() in the timer ISR body (frontend.md 3.1, 8.3.3). */
#include "frontend/fe.h"
#include "gen/module_frontend.h"

/* ---- ASOUND.117 (slots 100-103; spec sound.md "2. Driver interface") */
int fe_snd_init(unsigned speech_seg) { (void)speech_seg; return 0; }       /* TODO(S5.9): snd_init(speech_seg) */
int fe_snd_cmd(int cmd, int arg) { (void)cmd; (void)arg; return 0; }       /* TODO(S5.9): snd_cmd(cmd, arg); cmd 2 returns the voice-active flag (0 = silent) */
void fe_snd_shutdown(void) { }                                              /* TODO(S5.9): snd_shutdown() */
int fe_snd_poll(void) { return 0; }                                         /* TODO(S5.9): snd_poll(): the result steers the divisor of the original ISR (0 = unchanged) */

/* ---- the logo jingle driver (ASOUND.LOG / RSOUND.LOG / ISOUND.LOG, slots 100-102 of MPS_LOGO's own thunk table 00DA:0049+7*slot) */
/* MPS_LOGO 00DA:0307; main repeats it until it returns 0 */
int snd_init_log(int a1, int a2) { (void)a1; (void)a2; return 0; }         /* TODO(S5.9): logo_reset (sound.md "11. Logo overlay") */
/* MPS_LOGO 00DA:030E; 2 = play the jingle, 0 = stop */
void snd_cmd_log(int cmd) { (void)cmd; }                                    /* TODO(S5.9): the jingle runs on the overlay's own INT 8 hook (60.9 Hz) */
/* MPS_LOGO 00DA:0315 */
void snd_shutdown_log(void) { }                                             /* TODO(S5.9) */
