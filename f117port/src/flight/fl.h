#pragma once
/* Private helpers of the flight module (S5.7, port/spec/flight.md): the player flight model 1E9E, detection, the enemy AI, weapons and countermeasures,
 * damage. The prototypes of the spec's functions are in gen/module_flight.h (included by sim/sim.h). Everything works on VGAME's DGROUP by offset, as the
 * Python references of S3.6 / S3.8 (port/notes/s36/r2_fm.py, port/notes/s38/s38_ref.py) do; the record bases are theirs. */
#include "sim/sim.h"
#include "sim/sim_priv.h"
#include "gen/module_render3d.h"

#define UNIT_BASE   0xC13E          /* unit (aircraft) records, 0x24 bytes, count [DED0] */
#define SITE_BASE   0xB2A2          /* site / ground object records, 16 bytes, count [E540] (all) / [C0B4] (objects) */
#define PROJ_BASE   0x3C0C          /* projectile slots, 0x1C bytes x 12 (0-7 enemy, 8-11 player) */
#define DECOY_BASE  0x39AA          /* countermeasure records, 12 bytes x 4 (0 = decoy, 1-3 flares / chaff) */
#define TRACER_BASE 0xE44E          /* gun rounds, 12 bytes */
#define AC_BASE     0x2FC2          /* aircraft type table, 32 bytes */
#define WPN_BASE    0x3362          /* projectile prototype table, 0x12 bytes */

static inline s16 fl_abs16(s16 v) { return (s16)(v < 0 ? (u16)-(u16)v : (u16)v); }   /* the RTL abs(): -32768 stays -32768 */
static inline s16 fl_sx8(int v) { return (s8)(v & 0xFF); }
static inline s16 fl_rate(void) { return (s16)DSW(0x3660); }
static inline s16 fl_pilot_w(int off) { return (s16)rd16(ds_far(0x98F4).seg, (u16)(ds_far(0x98F4).off + off)); }   /* pilot record (comm+1272h) word */

/* 0000:CB62 rand_n of VGAME: (rand() * n) >> 15 with a SIGNED multiply (imul), unlike the RTL's rand_n of the other programs */
static inline int fl_rand_n(int n) { s32 p = (s32)rand_fn() * (s16)n; return (s16)(p >> 15); }

/* comm word += n (far pointer DS:E548) */
static inline void fl_comm_add(int off, int n) { u8 *p = (u8 *)comm_get() + off; u16 v = (u16)(p[0] | p[1] << 8); v = (u16)(v + n); p[0] = (u8)v; p[1] = (u8)(v >> 8); }

/* strcpy / strcat between DS offsets */
void fl_strcpy(u16 dst, u16 src);
void fl_strcat(u16 dst, u16 src);
