# F-117A 2.0 Reverse Engineering Guide

## §1 Executable Map (from tools/survey.py)

**Total code size:** 285,672 bytes across 15 files (87 relocations total).

**Compiler:** Microsoft C 5.x/6 (startup `mov ah,30h/int 21h/cmp al,2/jae/xor ax,ax;push es;push ax;retf/mov di,DGROUP`; the survey's "Borland" label was wrong) — 7 LZEXE-packed main EXE files: START, VGAME, END, MPS_LOGO, PLAYER, SETUP, DSWAP).

**Entry points:** packed EXEs enter the LZEXE stub at `cs:000E`; unpacked, the MSC startup (`work/unpacked/`). Drivers have no entry point (cs:ip 0000:0000): they are overlays with an entry table (§2.2).

**Packers (corrected in S1.2):** the seven main EXEs are **LZEXE 0.91** (`LZ91` at header 0x1C); `python tools/unlzexe.py` → `work/unpacked/` (image sizes: VGAME 162 460, START 89 023, END 46 039, DSWAP 19 495, PLAYER 18 455, MPS_LOGO 14 721, SETUP 13 437). All RE on these EXEs uses `work/unpacked/`. `MGRAPHIC.EXE`, `MISC.EXE` and `?SOUND.117` are not packed.

**Drivers/modules:** 
- Audio: ASOUND/ISOUND/RSOUND/NSOUND.117 (AdLib/Roland/IBM-Tandy/speaker drivers, 10-15 KB each, heavy INT 1Ah/21h use)
- Graphics: MGRAPHIC.EXE (VGA driver, 12 KB, INT 10h/72h for video mode setup)
- Sound data: SPEECH.117 (26 KB, raw 8-bit unsigned PCM, no header)
- MISC.EXE ("MISCOVR.EXE 05-11-88", keyboard/joystick overlay); DSWAP.EXE (floppy disk-swap prompt)

**Chain loader:** F117.COM (1.7 KB): EXECs the programs in turn and keeps the drivers resident as overlays — see §2.1 (START/VGAME/END loop, DSWAP before each).

**INT usage:** Dominated by INT 21h (DOS services) and INT 10h (BIOS video), with sound/graphics drivers using INT 1Ah (timer) and device-specific interrupts.

**I/O ports:** Graphics driver uses standard VGA ports (03C4h-03DAh); audio drivers use device-specific ports (AdLib: 0388h-0389h; Roland: 0330h-0331h).

## §2 Overlay chain and driver protocol (S1.2)

Tools: `tools/unlzexe.py`; `tools/commscan.py` (comm-block census → `work/s12/comm_*.txt`,
`comm_summary.txt`); `tools/ovlscan.py` (driver headers, entry tables, slot use per program →
`work/s12/vectors.txt`). F117.COM is 1 722 bytes: disassemble with capstone at origin 0100h.

### 2.1 F117.COM

Startup:
1. `SP=07B8`; shrink own block to 7Ch paragraphs; **allocate the comm block**: 140h paragraphs
   (5 120 bytes, `INT 21h/48h`), segment stored at **`0000:04F0`** (BIOS inter-application area) and
   `cs:0612`. Also `0000:04F2 = 1`, `0000:04F4 = 0` (§2.4). The block is never cleared or freed.
2. Copy its own PSP command tail to the EXEC tail buffer (SETUP gets F117's arguments).

Every EXEC is `INT 21h/4B00h` (inherited environment, FCBs = own PSP:5C/6C); return code = `4Dh` AL.

| # | Program | Tail | Return code |
|---|---|---|---|
| 1 | `SETUP.EXE` | F117's own | must be **24h** (`$`), else exit to DOS |
| 2 | `MPS_LOGO.EXE` | `" X"`, X = `comm[0Dh] & 5Fh` (sound driver letter) | **2** → `INT 10h` mode 3, exit; else kept in `cs:06F9` |
| 3 | overlays (§2.2) | | sound `comm+0Dh` → `comm[1Ch]`, call its entry 0 `(0,0,0)`; `MISC.EXE` → `comm[1Eh]`; graphics `comm+00h` → `comm[1Ah]`; `comm[20h]` = graphics entry 0 `(2)` |
| 4 | `PLAYER.EXE` | `" title-1 title-2 title-3a credit c-pit bunker"` | only if MPS_LOGO returned 0; ignored (SS:SP not reloaded after this EXEC) |
| L | `DSWAP.EXE` | `" 0"` | 0 → quit |
| | `START.EXE` | empty | kept; 0 → quit |
| | `DSWAP.EXE` | `" 1"` | 0 → quit |
| | `PLAYER.EXE` | `" title-2"` | only if START returned **0Ch** |
| | `VGAME.EXE` | empty | 0 → quit |
| | `DSWAP.EXE` | `" 2"` | 0 → quit |
| | `END.EXE` | empty | **23h** → back to L; else quit |

Quit = far call sound entry 2 (shutdown), then `INT 21h/4C00h`. The comm block and the overlays stay
resident across all EXECs.

What each program means by its exit code:
- **SETUP**: 24h = configured. Other exits (e.g. `exit(0)` at `0000:018D`, Esc in menus) → quit.
- **MPS_LOGO** (plays `LABSLOGO.SS` with `?SOUND.LOG`; driver letter R/A/T/I/N from argv): 0 = no
  key (play the intro), 1 = Esc (`011Bh`), 2 = Alt-Q (`1000h`, quit to DOS), 3 = other key.
  1 and 3 skip the PLAYER intro.
- **START**: 0 = quit (Y at the quit prompt, `0000:7676`, or fatal error); **0Ch / 0Dh** = fly.
  0Dh when bit 1 of the 16-byte record `[E062]·16 − 3419h` is set (`0000:0078`): no `title-2` animation.
- **VGAME**: 0 = quit, else debrief. **END**: 23h = back to START, 0 = quit.
- **DSWAP n** (0/1/2 = `start.exe`/`vgame.exe`/`end.exe`): if `fopen(name,"r")` succeeds → exit 1
  (hard-disk install). Else counts floppies (`0040:0010` bits 6-7), searches `A:`/`B:` (`"%s:\%s"`)
  and shows `SWAP.PIC`/`SWAP.PAL` asking for the disk; exit 1 when found.
- **PLAYER** (`.PAN` player; argv = names without extension, first arg `-`): code ignored.

### 2.2 Overlay loader and driver header

`F117.COM:03CB` (SETUP, MPS_LOGO and DSWAP carry a C copy with longer messages):
1. `INT 21h/48h BX=FFFF` must fail ("Alloc 1Mb?"); allocate *largest − 400h* paragraphs.
2. `INT 21h/4B03h` at that segment, relocation factor = same segment. Errors: 2 → "`name` - Filename
   not found", 8 → "Not enough mem to load file", other → "Overlay load failed error - XY" (the high
   nibble letter is computed wrongly, always 'A').
3. Need = `(hdr[1Eh]>>4) + (hdr[20h]>>4)` paragraphs; more than allocated → "Overrun alloc mem";
   shrink the block to need + 8 paragraphs. Returns the load segment.

Driver image header (image offset 0 after the MZ header; relocations always include `0:0018`, `0:001A`):

| Off | Size | Meaning |
|---|---|---|
| 00 | 24 | ASCII id, NUL-padded: name + date (`MGRAPHIC.EXE03-20-91`, `F117A  AdLib10-23-91`) |
| 18 | w | code segment of the entries (relocated) |
| 1A | w | data segment (relocated; 0 = load segment) |
| 1C | w | **slot base**: MGRAPHIC 0, MISC 90, ?SOUND 100 |
| 1E | w | bytes before the code segment (sound drivers: whole image) |
| 20 | w | code segment bytes (sound drivers: 0) |
| 22 | w | entry count *n* (MGRAPHIC 84, MISC 6, sound 5) |
| 24 | n·w | entry offsets in the code segment |

### 2.3 Vector table and binding

Each EXE that uses drivers has a table of 5-byte `JMP FAR` stubs (`EA 0000 0000`) in DGROUP and binds
each driver at startup with `bind(seg)`: `stub = table + 5·(hdr[1Ch] + i)`, `stub[1..2] =
hdr[24h+2i]`, `stub[3..4] = hdr[18h]`. Calls are `CALL FAR DGROUP:table+5·slot`, so **a driver
function is identified by its slot**. Arguments: C far calls (pushed right to left, caller pops),
result in AX (DX:AX).

| Program | DGROUP | table | binds (`comm` word) | bind routine |
|---|---|---|---|---|
| SETUP | 024C | 08A6 | its own `misc.exe` | `0000:0BA2` |
| MPS_LOGO | 0332 | — | own loader for the sound driver | — |
| PLAYER | 02A3 | 11C0 | sound `[1Ch]` | `0000:0F86` |
| START | 0A95 | 69B8 | `[1Ah]`, `[1Ch]`, `[1Eh]` | `0000:862B` |
| VGAME | 1E23 | 011A | `[1Ah]`, `[1Eh]`, `[1Ch]` | `0FFD:0008` |
| END | 05FA | 23FA | `[1Ah]`, `[1Ch]`, `[1Eh]` | `0000:45CA` |
| DSWAP | 0225 | 08DC | `[1Ah]`, `[1Eh]` | `0000:085A` |

Slot use per program (call-site counts) is in `vectors.txt`: VGAME 57 slots, START 33, END 32,
DSWAP 16. MGRAPHIC slot 42 dominates (START 88, END 48, VGAME 26 sites). SETUP's sites for slots 5,
47 and 108 sit in library code it does not reach (it binds only MISC; unverified).

**MGRAPHIC.EXE** (slots 0-83, code segment 019C; entry list in `vectors.txt`, API = S3.2). Slot 0
`gfx_alloc(n)`: n = 0 → returns `A000h`; else allocates FA00h bytes (one 320×200 page) and returns its
segment, or prints "Insufficient memory for MCGA graphics" and exits. F117.COM calls it once with 2:
**`comm[20h]` = off-screen page segment** (VGAME copies it to `[E558]`).

**MISC.EXE** (slots 90-95):

| Slot | Function |
|---|---|
| 90 | `kbd_empty()`: INT 16h/01 → 0 if a key is waiting, FFFFh if not |
| 91 | `kbd_get()`: INT 16h/00 → AX = scan:ASCII |
| 92 | `getche()`: INT 21h/01 |
| 93 | `joy_button(n)`: port 201h bit n+4, inverted → 1 = pressed |
| 94, 95 | clear Caps/Num/Scroll/Insert lock bits at `0040:0017` |

**?SOUND.117** (slots 100-104; ASOUND code segment 01B6):

| Slot | Function (ASOUND) |
|---|---|
| 100 | `snd_init(x)`: stores x, installs the driver (timer); F117.COM passes 0 |
| 101 | `snd_cmd(n)`: n ≤ 2Dh (46 commands) via `cs:0911`, larger ignored; VGAME 17, START 15, END 15 sites |
| 102 | `snd_shutdown()`: uninstall, silence, DOS time ← RTC (F117.COM at quit only) |
| 103 | `snd_poll()`: runs an update, returns status `[17DC]` |
| 104 | `snd_noise()`: effect-unit pitch randomiser (called after each poll in the fast rate; **not** stop, corrected in S3.3: `port/spec/sound.md` §2) |

Command numbers and the `.LOG` music format: S2.4.

### 2.4 The comm block (5 120 bytes at `[0000:04F0]:0000`)

Each program reads `0000:04F0` once and keeps far pointers `comm:0000` and `comm:1272` (START, VGAME,
END, DSWAP; SETUP keeps `comm:120E`). Census of fixed offsets (`es:[bx+N]` after `les bx,[ptr]`):
`comm_summary.txt`, 60 offsets; accesses via other pointers or string calls are not counted.

| Off | Type | Written by | Meaning |
|---|---|---|---|
| 0000 | char[13] | SETUP | graphics driver file (`"Mgraphic.exe"`) |
| 000D | char[13] | SETUP | sound driver file (`Isound/Asound/Rsound/Nsound.117`, menu 1-4) |
| 001A | w | F117.COM | MGRAPHIC segment |
| 001C | w | F117.COM | sound driver segment |
| 001E | w | F117.COM | MISC segment |
| 0020 | w | F117.COM | off-screen page segment (MGRAPHIC slot 0) |
| 0022 | w | SETUP (=1), START | ? |
| 0024 | w | SETUP (=0), calibration | joystick calibration byte; START passes it to MGRAPHIC slot 60 on first run |
| 0026-002E | w | SETUP/START/VGAME | options (0026 = 1 after SETUP; 002A ← SETUP `[0776]`) — S3.5 |
| 0030 | w | SETUP | joystick in use (`[0FD4]`) |
| 0032 | w | SETUP | adapter: FFFFh unset, 3 = MCGA (driver name starts with `M`), else 2 |
| 0033-003E | b/w | START, VGAME, END | game options/state — S3.5/S3.6 |
| 0048 | struct | SETUP | joystick calibration data (far pointer to the calibrate routine; VGAME too) |
| 0070 | w | SETUP (=0) | ? |
| 0072 | b | SETUP | bit 0 = joystick (answered Y), bit 1 = mouse driver (INT 33h/0 = FFFFh) |
| 0074, 0076 | w | VGAME → END | mission result — S3.6/S3.9 |
| 1272 | 80 B | START | **active pilot record** (50h bytes per pilot, copied at `START:6D46`; `ROSTER.FIL` = 802 = 2 + 10·80, to confirm in S2.5); read/updated by VGAME and END (1272-12C1) |

BIOS words next to the pointer: **`0000:04F2`** = 1 from F117.COM; START does its one-time setup
(pilot record copy, intro) while it is non-zero, then clears it. **`0000:04F4`** = 0 from F117.COM;
START sets it to 1 after the one-time MGRAPHIC slot 69/60 initialisation. DSWAP keeps a pointer to it.

### 2.5 Port notes

- One process: `comm` becomes a C struct; the vector table becomes a function table indexed by slot
  (MGRAPHIC 0-83, MISC 90-95, sound 100-104).
- Top level = the §2.1 state machine (`SETUP → LOGO → [intro] → {START → [title-2] → VGAME → END}`);
  DSWAP is a no-op (always 1).

## §3 Function index (tools/f117index.py)

`python tools/unlzexe.py` -> `work/unpacked/<NAME>.EXE`; `python tools/f117index.py work/unpacked/<NAME>.EXE port/<name>`
writes `port/<name>_functions.csv/.json` (start, size, far, callers/callees, INT.AH, ports, strings, DS reads/writes).
Addresses `SSSS:OOOO` as stored (Ghidra = SSSS+1000). Functions/coverage: SETUP 105/81%, PLAYER 111/93%, START 267/98%,
VGAME 450/66% (the uncovered rest is far data segments 14b2/1853/1d50, not code), END 196/97%, MPS_LOGO 117/90%, DSWAP 101/97%.
Not applicable to the `.117`/MGRAPHIC drivers (no MSC startup); handled in S1.2.

## §4 Animation formats (S2.2)

**`.PAN`** (PLAYER.EXE): the whole file is one LZEXE-style bit stream (`tools/lzs.py`; control word is
fetched while the 16th bit of the previous one is taken; match offset = `0x2000 - (hi5<<8|lo)`, long
len byte 0/1 = chunk end, 1 = more). Decoded stream:
`'Z', speed, n:u16, n × u16 cel sizes, 768 B VGA palette (6-bit)` then n cels `'B',0,1,0 + RLE`.
`speed` = divisor of 60 (20 → 3 ticks/frame). `tab[0]` includes the header; cel i ends at Σtab[0..i].
Cel RLE (`PLAYER 0000:1006`) writes a delta onto the 320×200 screen (di from 0): `00 n v` fill,
`1..7F n` literal, `81..FF` skip n-80h, `80 w:u16` long op (w>0 skip, 0 end, 8000-BFFF literal run, C000-FFFF fill+byte).

**`.SS`** (`MADSS1`, MPS_LOGO 006d:0112): `"MADSS1" 1A name[9]`, u32 size, u16 nframes(+1), u32, u16, u16,
u8 mode, then nframes × `{u32 off, u16 flag, x, y, w-1, h-1}` (last record = end marker), u8 ncolours,
ncolours × `{r,g,b,slot}` (6-bit), then sprite data at that base + off. Sprite RLE: row marker `FA` →
literals (`FD n v` run, `FC` skip 1), other marker → (count,value) pairs; value `FC` = transparent;
`FE` end of row, `FF` end of sprite. Pixel p maps to palette entry p-1.

## §5 Sound: driver API, `.LOG` overlays, speech (S2.4)

**There is no separate music data file.** `?SOUND.LOG` (`ASOUND/ISOUND/RSOUND.LOG`, 6 120 / 2 458 / 5 481 B)
is a second MZ overlay of the same kind as `?SOUND.117` (§2.2) that `MPS_LOGO.EXE` loads itself
(`"rsound.log"`…`"tsound.log"` strings; the driver letter picks the file) to play the **logo jingle**.
Header differs from `.117`: id at image+10h (`"AdLib Logo   7-18-91"`), then `cseg`(+28h) `dseg`(+2Ah)
`size`(+2Ch) `slot base`(+2Eh = 100) `n`(+30h = 11) and n entry offsets from +32h. Code is image 0..11B2h,
copyright string at 11CFh, DS = image 11C0h (tune/voice data + 7 voice blocks of 1Ah bytes at DS:043C+1Ah·i).
Entries: 100 `init(x)` (hooks **INT 8** itself, PIT reload 4C90h = 60.9 Hz), 101 `cmd(n)`, 102 shutdown,
105 returns tick count; the rest are `retf`. `cmd` 0 = stop, 1 = pause (all streams → DS:0100), **2 = play
the jingle** (stream starts DS:010E/0120/014C/017A/01A6/01B6). The ISR calls the sequencer (`0000:116E`,
one `0D1A` step per voice) every 3rd, then every 5th tick. Per-voice byte stream: `<80h` = note data,
`F5h..FFh` = control (switch at `0D86`, includes 1- and 2-level repeat counters and loops); the exact event
grammar is **not decoded** — the port can run the original sequencer (S3.3) or emulate it as `tools/sound/drvemu.py` does.

**`?SOUND.117` (in-game; ASOUND shown)** is polled: the game's own timer ISR (START `0000:8E6D`,
PIT reload 4DAEh) calls `snd_poll` (slot 103) and steers its own rate from the result: -1 -> 60 Hz base (idle polls return -1; **correction S3.3: the ISR period is one video frame (70 Hz in mode 13h), not 60 Hz; see `port/spec/sound.md` §3**), >0 -> base/5 = 300 Hz, 0 -> unchanged. One poll = one tick of every voice. `snd_cmd(n)` slot 101 dispatches 0..2Dh via
`cs:0911`; all commands start OPL sequences that `snd_poll` advances. Detection in `snd_init`: AdLib timers
(reg 4 ← 60h/80h/21h, status & E0h = C0h), PAS at port B8Ah/B8Bh and SB at 220h are probed for speech.
Command numbers seen at call sites: START 2, 26-31, 33, 34; VGAME 0, 19, 23-25, 32, 40-45; END 2, 26, 28,
36-39; PLAYER 2, 3. Rendered (`work/sound/asound_cmdNN.wav`): **3** 18 s multi-voice cue sequence (PLAYER intro soundtrack; user: sounds like several effects strung together, not a tune), **36/37/38**
3.8/5.1/6.5 s jingles (END), **39** long looping tune, **11/12** 0.5-0.7 s effects, 6-10, 13-20, 26-35 short
UI/effect blips; 0, 1, 2, 21-25, 40 write nothing (stop/pause/state; 2 = query: returns OR of the 7 voice-active flags; 40 = `snd_cmd(40, seg)` sets the speech
buffer segment `[17E2]`); 41-45 = speech.
(U5 OK: logo, 36-39 at 60 Hz, speech. cmd 3 sounded too slow at 60 Hz; accepted at a fixed 120 Hz poll — PLAYER's real rate was not determined.)

**Speech** (`SPEECH.117`, 26 408 B = 8 clips) is loaded by VGAME at startup (`0000:0110`) and handed over by
cmd 40. Format: unsigned 8-bit PCM, **8000 Hz** (cmd 41-45 handlers `cs:096D..` load AX=1F40h). On AdLib it is
played by writing `sample>>2` through a table (`cs:0018`) into **OPL reg 43h** (operator volume) in a PIT-paced
loop (`0B0F`) — no SB needed. Clip table (record = `end:u16, start:u16, para:u16`, 0-terminated) at
`cs:09AA`: clip starts 0, 4216, 9251, 10572, 12829, 15654, 18343, 22119, end 26408. cmd 41 = clips 0+1,
42 = clip 1, 43 = (clip 2 or 3, alternating by poll parity) + clip 4, 44 = clips 5+6, 45 = clips 5+7.

Tools: `tools/sound/drvemu.py` (Unicorn 16-bit harness: loads the overlay at 2000h, applies relocations,
hooks AdLib 388/389/38A/38B and the PIT, calls `snd_init/cmd/poll` or the INT 8 ISR, logs OPL writes with
49 716 Hz time stamps), `tools/sound/oplrender.c` (Nuked-OPL3, `third_party/nuked-opl3`, log → WAV; build:
`gcc -O2 -o work/oplrender.exe tools/sound/oplrender.c third_party/nuked-opl3/opl3.c -Ithird_party/nuked-opl3`),
`tools/sound/render_all.py` (everything → `work/sound/*.wav`). Needs `pip install unicorn`.
Roland (`RSOUND.*`, MT-32) and IBM/Tandy/speaker drivers are not rendered.
