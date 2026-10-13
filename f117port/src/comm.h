#pragma once
/* The comm block: 5120 bytes (140h paragraphs) that F117.COM allocates once and every program of the chain
 * reads and writes (port/spec/HANDOFFS.md section 2; RE_GUIDE 2.4). It lives in mem[] at the segment F117.COM
 * stores at 0000:04F0, so ported code that reaches it through its own far pointer (SETUP FD6/FD8, START
 * DS:E096, VGAME DS:E548, END DS:7222/7224) and the port's C code see the same bytes: comm_get() is the
 * C view. Offsets in hex are the original's; the static asserts at the end keep the struct honest.
 *
 * All fields are little-endian words unless a type says otherwise. Writers are listed in HANDOFFS.md. */
#include "mem.h"
#include "symbols_start.h"      /* generated (tools/gen_symbols.py): START_DS_<name> */

#define COMM_PARAS 0x140u                    /* 5120 bytes */
#define COMM_SIZE  (COMM_PARAS * 16u)

/* BIOS inter-application area words (HANDOFFS section 2). */
#define COMM_SEG_PTR      0x04F0             /* 0000:04F0 segment of the comm block */
#define COMM_FIRST_RUN    0x04F2             /* F117.COM = 1; START clears it (roster screen while non-zero) */
#define COMM_INIT_DONE    0x04F4             /* F117.COM = 0; START sets 1 after MGRAPHIC slots 69/60 */

#pragma pack(push, 1)
typedef struct Comm {
    char gfx_driver[13];                     /* 00 SETUP: graphics driver file, "Mgraphic.exe" */
    char snd_driver[13];                     /* 0D SETUP (menu 1-4): sound driver, e.g. "Asound.117"; MPS_LOGO uses [0] & 5Fh */
    u16  seg_mgraphic;                       /* 1A F117.COM: segment of MGRAPHIC */
    u16  seg_sound;                          /* 1C F117.COM: segment of the sound driver */
    u16  seg_misc;                           /* 1E F117.COM: segment of MISC */
    u16  seg_offscreen;                      /* 20 F117.COM: off-screen page segment (MGRAPHIC slot 0 result) */
    u16  clearance;                          /* 22 SETUP = 1 (SETTLED G3): always 1 in a real run */
    u8   joy_calib_byte;                     /* 24 SETUP = 0: joystick calibration byte (HANDOFFS: b; RE_GUIDE: w) */
    u8   pad_25;
    u16  mission_state;                      /* 26 START 3; VGAME 1 at flight start, 2 eject, 3 landed (SETTLED C1, C2) */
    u16  end_code;                           /* 28 START 0; VGAME mission_end: 0 landed, 1 stranded, 2 terrain, 5, 6 (C3) */
    u16  unused_2a;                          /* 2A SETUP: [0776] = 0, never read */
    u16  missiles_launched;                  /* 2C START 0; VGAME ++ per enemy missile launched at the player (Q3) */
    u16  fuel_lbs;                           /* 2E START: 12 x route score, estimated fuel in lbs (C10) */
    u16  nonscoring;                         /* 30 START (pilot+3C > 1); VGAME bit 0 = Alt-T training; 0 = mission counts (C4) */
    union {                                  /* 32: the word and the byte overlap in the original */
        u16 adapter;                         /* 32 SETUP: FFFFh unset, 3 = MCGA, else 2 */
        struct {
            u8 adapter_lo;                   /* 32 */
            u8 night;                        /* 33 START: 1 = night mission (written after SETUP's word; read by VGAME, END clock) */
        };
    };
    u16  damage_mask;                        /* 34 VGAME mission_end: damage bit mask */
    u16  hits_taken;                         /* 36 VGAME mission_end: hits taken */
    u16  weapon_id[4];                       /* 38 START: weapon id of bays 0-3 */
    u16  weapon_qty[4];                      /* 40 START: quantity per bay */
    u8   joy_calib[40];                      /* 48 SETUP: joystick calibration block (VGAME r/w, END joy_set_calib) */
    u16  unused_70;                          /* 70 SETUP = 0, nobody reads it (G2) */
    u16  input_devices;                      /* 72 SETUP: bit 0 = joystick, bit 1 = mouse */
    u16  final_x;                            /* 74 VGAME mission_end: final x (level units); END grid cell = >> 11 */
    u16  final_y;                            /* 76 VGAME mission_end: final y */
    u8   pad_78[2];
    u8   world_blob[0x1272 - 0x7A];          /* 7A START wld_export_to_comm; VGAME writes back + the 0x600-byte event log */
    u8   pilot[0x50];                        /* 1272 active pilot record (50h bytes), START/END write, VGAME reads (C5) */
    u8   pad_12c2[COMM_SIZE - 0x12C2];       /* 12C2..13FF unused */
} Comm;
#pragma pack(pop)

_Static_assert(sizeof(Comm) == COMM_SIZE, "comm block is 5120 bytes");
_Static_assert(START_DS_ptr_comm == 0xE096, "START keeps the comm far pointer at DS:E096 (HANDOFFS 2)");
_Static_assert(__builtin_offsetof(Comm, snd_driver) == 0x0D, "comm+0D");
_Static_assert(__builtin_offsetof(Comm, seg_mgraphic) == 0x1A, "comm+1A");
_Static_assert(__builtin_offsetof(Comm, mission_state) == 0x26, "comm+26");
_Static_assert(__builtin_offsetof(Comm, fuel_lbs) == 0x2E, "comm+2E");
_Static_assert(__builtin_offsetof(Comm, adapter) == 0x32, "comm+32");
_Static_assert(__builtin_offsetof(Comm, night) == 0x33, "comm+33");
_Static_assert(__builtin_offsetof(Comm, weapon_id) == 0x38, "comm+38");
_Static_assert(__builtin_offsetof(Comm, weapon_qty) == 0x40, "comm+40");
_Static_assert(__builtin_offsetof(Comm, joy_calib) == 0x48, "comm+48");
_Static_assert(__builtin_offsetof(Comm, input_devices) == 0x72, "comm+72");
_Static_assert(__builtin_offsetof(Comm, world_blob) == 0x7A, "comm+7A");
_Static_assert(__builtin_offsetof(Comm, pilot) == 0x1272, "comm+1272");

/* Pilot record offsets (HANDOFFS section 6; FORMATS.md has the whole 50h record). END writes these. */
#define PILOT_RANK        0x20               /* ++ on promotion, 0-6 (2nd Lt ... Brig Gen) */
#define PILOT_PURPLE      0x22               /* = 1 */
#define PILOT_AIRMANS     0x24               /* ++ (max 9), then +26 DFC, +28 Silver Star, +2A Air Force Cross */
#define PILOT_MEDAL_HONOR 0x2C               /* = 1 */
#define PILOT_BEST_SCORE  0x2E               /* max */
#define PILOT_LAST_SCORE  0x30               /* = score (0 when comm+30h says training) */
#define PILOT_CAREER      0x32               /* dword += score */
#define PILOT_THEATRE     0x38               /* read by END (+3A war level, +3C mission type, +3E opponent, +40 realism, +42 model) */
#define PILOT_MISSIONS    0x36               /* ++ */
#define PILOT_STATUS      0x4E               /* 1 retired (mission 99), 2 KIA: START opens the roster screen when non-zero */

/* The C view of the block F117.COM allocated (valid after chain_run has stored the segment at 0000:04F0). */
static inline Comm *comm_get(void) { return (Comm *)mp(rd16(0, COMM_SEG_PTR), 0); }
