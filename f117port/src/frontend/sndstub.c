/* Sound call points of the front end, adapters onto the sound module (src/sound, S5.9). Call sites: SETUP none; MPS_LOGO snd_init_log /
 * snd_cmd_log(2) / snd_cmd_log(0) / snd_shutdown_log (the .LOG overlay, sound.md "11"); PLAYER fe_snd_cmd(3) at the first poll_input (the intro
 * soundtrack), fe_snd_cmd(0) at player_exit, fe_snd_cmd(2) polled before exit when several files were played, fe_snd_poll() in the timer ISR body
 * (frontend.md 3.1, 8.3.3). START and END use fe_snd_cmd / fe_snd_poll through their own wrappers. */
#include "frontend/fe.h"
#include "gen/module_frontend.h"
#include "sound/snd.h"

/* ---- ASOUND.117 (slots 100-104; spec sound.md "2. Driver interface") */
int fe_snd_init(unsigned speech_seg) { snd_init(speech_seg); return 0; }
int fe_snd_cmd(int cmd, int arg) { return snd_cmd(cmd, arg); }              /* cmd 2 returns the voice-active flag (0 = silent) */
void fe_snd_shutdown(void) { snd_shutdown(); }
/* The sound step of a tick: snd_poll (its result steers the fast / idle state of the original ISR) and, in the fast state, snd_noise. */
int fe_snd_poll(void) { return snd_isr_step(true); }
/* PLAYER's and END's skipped 7th tick: no poll, but the ISR still runs snd_noise in the fast state. */
void fe_snd_poll_skipped(void) { (void)snd_isr_step(false); }

/* ---- the logo jingle driver (ASOUND.LOG / RSOUND.LOG / ISOUND.LOG, slots 100-102 of MPS_LOGO's own thunk table 00DA:0049+7*slot) */
/* MPS_LOGO 00DA:0307; main repeats it until it returns 0 */
int snd_init_log(int a1, int a2) { (void)a1; (void)a2; snd_log_init(); return 0; }
/* MPS_LOGO 00DA:030E; 2 = play the jingle, 0 = stop */
void snd_cmd_log(int cmd) { snd_log_cmd(cmd); }
/* MPS_LOGO 00DA:0315 */
void snd_shutdown_log(void) { snd_log_shutdown(); }
