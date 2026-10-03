# Sound: driver API, sequencer, AdLib device, logo overlay, speech (S3.3)

**Status: S3.3 delivered.** Authority for the overlay protocol, slot numbers and the comm block is `port/RE_GUIDE.md`
§2; this file adds everything inside the drivers. Symbols: `port/spec/sound_symbols.csv` (hand-written, about 120 rows). Disassembly source: `Game/ASOUND.117` / `Game/ASOUND.LOG`; addresses are
`SSSS:OOOO` with the code segment **01B6** (`cs:OOOO` = image offset `1B60h + OOOO`) for ASOUND.117, and the code
segment 0000 (image offset = cs offset) for ASOUND.LOG. Data-segment addresses are written `DS:OOOO`.
Confidence: **verified** = re-implemented in `tools/sound/asnd.py` and compared write-for-write with the real driver
under Unicorn (§13); **read** = read in the disassembly only; **guess** = inferred.

## 1. What the port implements

* **ASOUND (AdLib)** only. ISOUND (IBM/Tandy/PC speaker), RSOUND (Roland MT-32) and NSOUND (no sound, 625 bytes) are
  not ported; the game selects the driver by the `comm+0Dh` file name (setup menu 1-4). The port ignores that menu
  and always uses the AdLib model (SETUP's sound choice is stored but has no effect).
* The AdLib driver is **state 0 only** (plain OPL2). `snd_init` also probes Sound Blaster (DSP at base+6) and Pro Audio
  Spectrum (port B8Ah) and then enables dual-chip volume, SB DMA speech and so on (`[17F2]` = 1/2/3). Those paths
  (`cs:0320..033E`, `0381..03A3`, `0C8B..0CFE`, `18C8..1CC9`) are **not ported**. The harness reports state 0.
* The data is **not copied**: the port reads `ASOUND.117` / `ASOUND.LOG` at run time and uses their data segments
  (tracks, instruments, tables) exactly as the originals do. The code is re-implemented in C from this spec
  (`tools/sound/asnd.py` is the executable reference: `Engine`, `Game`, `Logo`).
* OPL output: feed `(reg, value)` writes to Nuked-OPL3 in OPL2 mode (49 716 Hz) as in S2.4
  (`third_party/nuked-opl3`). Time stamps come from the scheduler in §3, not from the driver.

## 2. Driver interface (ASOUND.117; slots 100-104)

Header (image offset 0): id `"F117A  AdLib10-23-91"`, `[18h]` code segment 438 (=01B6h), data segment 0 (= load
segment), slot base 100, `n` = 5, entries at `cs:0B76, 0B98, 0BB5, 0C08, 0C1C` (RE_GUIDE §2.2). The image is 14 378
bytes: **DS = image 0000..1B5F** (tables, instruments, tracks, variables), **CS = image 1B60..382A** (code and the
cs-variables `cs:0000..005D`). The driver is loaded at a paragraph boundary and run with DS = load segment, so
`DS:1B60+x` and `cs:x` are the same byte; the port can use one byte array for both (the Python model does).

| Slot | cs entry | C prototype | Behaviour |
|---|---|---|---|
| 100 | `0B76` | `snd_init(unsigned speech_seg)` | `DS:17E2 = speech_seg`, then `0x5E`: probes (state 0 here), then `reset_all()` (§4.5). F117.COM passes 0. |
| 101 | `0B98` | `snd_cmd(int cmd, int arg)` | `cmd > 2Dh` ignored; otherwise `cs:0911[cmd]` is called with `AX = 0` and `BX = 2*cmd` (several handlers use BX, §8). `arg` is the second stack word (`[bp+8]`); only cmds 23 and 40 read it. Returns AX (only cmd 2 returns a value). |
| 102 | `0BB5` | `snd_shutdown()` | `reset_all()`, set the DOS clock from the RTC (the PIT was retuned; **not needed**), F117.COM calls it at quit. |
| 103 | `0C08` | `int snd_poll()` | `[17DC] = 0`, `poll_body()`, returns `[17DC]` (0 = unchanged, 1 = fast, FFFFh = idle; §3, §7). |
| 104 | `0C1C` | `void snd_noise()` | `noise_tick()` (§7.2) — **not "stop"** as RE_GUIDE §2.3 says (that row is corrected: slot 104 is the effect-voice pitch randomiser, called after every poll while the fast rate is on). |

Slot 105 does not exist for ASOUND.117 (`n = 5`). Callers: RE_GUIDE §2.3 and §5 (command numbers per program).

## 3. Time base (corrects RE_GUIDE §5 "60 Hz")

The driver has **no timer of its own** in-game: the *game's* INT 8 handler (START `8D1E..8D5E`, same shape in VGAME
`1CC2`, END `4C28`, PLAYER `288C..29F3`) calls the driver. Per ISR tick, in this order: add the PIT reload
(`[AE03]`) to the 32-bit game clock, run the frame-sync/calibration every Nth tick (`timer_calibrate`, platform.md),
**call `snd_poll`**, then `snd_noise` **unless** `[AE07] == 1`. The PIT reload is `frame_period / [AE07]`, where the
frame period is measured from 16 vertical retraces (`8DD8`, platform.md key finding). The poll result steers `[AE07]`
(`8E6D..8EAC`):

| `snd_poll` returns | Effect | ISR rate | `snd_noise` called |
|---|---|---|---|
| FFFFh (negative) | `[AE07] = 1` (idle) | **1 per video frame** = 70.086 Hz in VGA mode 13h | no |
| >0 (1) | `[AE07] = [AE0A]` (class 4..6, measured: **4** at 70 Hz, 5 at 60 Hz) | 280 Hz (300 Hz) | yes |
| 0 | unchanged | unchanged | per the current mode |

So sequencer ticks are **70.09 Hz while idle** (not 60), and **4x faster (280 Hz) while a pitch-effect unit (§7) is
active**. Every voice timer is counted in poll ticks, so notes with effect instruments make music run 4x faster
for the duration of the effect; this is the original behaviour (effect counters are about 80 polls = 0.3 s). Tracks that
use effect instruments are mostly short effects, so it is normally inaudible; `cmd 3` mixes them with a long
sequence (see §13). The port's scheduler is a loop that, per frame, calls `poll` once (idle) or four times
(fast) with `noise` after each poll in the fast mode, exactly as above. The `cs:000E` word counts polls (used only
to alternate speech clip 2/3, §10). Frame rate: use the display refresh the port runs at; the original value for
mode 13h is 70.086 Hz. **Open (U-check, PLAN)**: `work/sound/asound_cmd03_steered.wav` renders cmd 3 with this rule
(the S2.4 renders used 60 Hz or the fixed 120 Hz that was accepted for cmd 3).

OPL time stamps for the OPL core: all writes inside one poll share the poll's time (the original writes them within
microseconds; the OPL2 hardware needs only the 3.3/23 µs register delays).

## 4. Data layout (ASOUND.117; DS offsets unless `cs:`)

### 4.1 Variables

| Address | Size | Name | Meaning |
|---|---|---|---|
| `17D0` | b | `dirty` | the **uninitialised local `[bp-4]`** of `voice_tick` (§5.4): "volume dirty" flag; its stack slot persists between voices and polls, so it is a memory byte, not a local |
| `17DC` | w | `poll_result` | steering result (§3) |
| `17DE` | w | `rate_state` | last announced steering (FFFFh idle / 1 fast), changed only on transitions |
| `17E0` | b | `pic_mask_save` | speech (§10) |
| `17E1` | b | `port61_save` | speech |
| `17E2` | w | `speech_seg` | segment base of the SPEECH.117 data (cmd 40 / `snd_init`) |
| `17E4` | w | `lcg` | random seed, initial 04D2h (1234) |
| `17E6` | w | `eng_last` | last engine value sent to OPL (0 / FFFFh = force) |
| `17E8` | w | `eng_fnum` | engine frequency word |
| `17EA` | w | `eng_vol` | engine volume word |
| `17EC` | w | `busy` | non-zero: `poll_body` returns immediately (set during voice start / reset / mute) |
| `17F0/17F1` | b | `unit_save0/1` | effect counters saved by cmd 21 |
| `17F2` | w | `state` | 0 AdLib only, 1 SB, 2 SB Pro, 3 PAS (this spec: always 0) |
| `17F4` | w | `eng_value` | engine RPM value 0..0301h (cmd 23 / 11) |
| `17F6` | b | `eng_on` | engine drone enabled (cmd 24 = FFh, cmd 25 = 0) |
| `17FA` | 128 b | `vol_tab` | stereo volume (state>1 only; unused here) |
| `187A` | 12 w | `fnum_tab` | `200 21E 23F 261 285 2AB 2D4 2FF 32D 35D 390 3C7` (semitone -> F-number) |
| `1892..1894` | 3 b | `flags` | tremolo depth, vibrato depth, CSM/note-select (all file values `01 01 01 00`) |
| `1896` | 18 b | `chan_op` | `{0,3, 1,4, 2,5, 6,9, 7,10, 8,11, 12,15, 13,16, 14,17}`: per channel (modulator, carrier) index into `op_off` |
| `18A8` | 18 b | `op_off` | OPL operator offsets `{00 01 02 03 04 05 08 09 0A 0B 0C 0D 10 11 12 13 14 15}` |
| `1902/1904` | w | `unit_base0/1` | pitch base of effect unit 0/1 (grows by `unit_delta` per poll) |
| `1906/1908` | w | `unit_delta0/1` | per-poll pitch step |
| `190A + 1Eh*v` | 30 b | voice record `v` = 0..6 (§4.2); voice 6 (`19BE`) is the engine drone |
| `19DC/19FC` | b | `unit_cnt0/1` | effect unit counters (0 = free) |
| `19FE/1A00` | w | `unit_mask0/1` | random-pitch masks |
| `1A02` | b | `cur` | index of the voice being processed (0..6) |
| `1A03` | b | `unit_toggle` | which unit the next effect note takes |
| `1A04/1A05` | b | `unit_ch0/1` | channels the units drive |
| `1A06` | 256 b | `shadow` | shadow of every OPL register written (read back for key-off, B0/BD bits, 40h bits 6-7) |
| `1B06 + 8*ch` | 8 b | `eff[ch]` | effect parameters of the current instrument on channel `ch`: `+0` counter (0 = none), `+2` mask, `+4` base, `+6` delta |
| `cs:000E` | w | `poll_count` | |
| `cs:0018` | 64 b | `speech_vol` | log curve `3F 2D 26 23 20 1D 1B 19 18 16 15 14 13 12 11 10 10 0F 0E 0E 0D 0D 0C 0C 0B 0B 0A 0A 09 09 09 08 08 07 07 07 06 06 06 06 05 05 05 04 04 04 04 03 03 03 03 02 02 02 02 02 01 01 01 01 00 00 00 00` |
| `cs:0058` | w | `sb_base` | I/O base for SB/PAS (unused in state 0) |

### 4.2 Voice record (1Eh bytes; field names are the ones used by `asnd.py`)

| Off | Name | Meaning |
|---|---|---|
| 00 | `t` | ticks until the next event; 0 = voice inactive |
| 01 | `slide` | signed pitch slide per tick (F-number units; 0 = none) |
| 02 | `rstep` | volume-offset step of the ramp (event F8) |
| 03 | `pstep` | pan-offset step (event F3; only used for state>1) |
| 04 | `pitch` | current note (0 = rest; note/12 = octave, note%12 = semitone) |
| 05 | `inst` | current instrument number (informational) |
| 06 | `vol` | track volume 0..63 (attenuation = 3Fh - ((vol+vofs)&3F)); 0 at voice start |
| 07 | `gate` | release shortening (event FB): key-off `gate` ticks before the note ends |
| 08 | `rel` | ticks until the key-off of the current note (0 = none pending) |
| 09 | `rctr` | ramp counter (event F8) |
| 0A | `rrel` | ramp reload; FFh at voice start |
| 0B/0C | `pctr/prel` | pan ramp counter / reload (F3) |
| 0D | `pan` | pan offset (F4; initial 40h); state>1 only |
| 0E | `start` | track start (the `FD` restart target) |
| 10 | `ptr` | read pointer (DS offset) |
| 12 | `ls1` | loop-1 start (initially = track start) |
| 14 | `ls2` | loop-2 start |
| 16 | `cnt1` | loop-1 remaining count (word) |
| 18 | `cnt2` | loop-2 remaining count (word) |
| 1A | `lptr` | "loop pointer": target of `FD` (0 = restart at `start`) |
| 1C | `tune` | signed F-number offset added to every note (event F7) |
| 1D | `vofs` | volume offset added to `vol` (event F5, ramped by F8) |

### 4.3 Instrument table (DS:002E, 42 instruments of 2Ch bytes; max used: 41)

Two 16h-byte operator patches: bytes 0..15h of `inst*2Ch + 2E` = modulator, bytes 16h..2Bh = carrier. One patch:

| Off | Meaning | OPL field |
|---|---|---|
| 00 | attack | reg 60+op, bits 7-4 |
| 01 | decay | reg 60+op, bits 3-0 |
| 02 | sustain level | reg 80+op, bits 7-4 |
| 03 | release | reg 80+op, bits 3-0 |
| 04 | EG type | reg 20+op bit 5 |
| 05 | KSR | reg 20+op bit 4 |
| 06 | total level | reg 40+op, 3Fh - (v&3F) |
| 07 | KSL | reg 40+op bits 7-6 |
| 08 | waveform | reg E0+op & 3 |
| 09 | multiplier | reg 20+op bits 3-0 |
| 0A | feedback | reg C0+ch bits 3-1 |
| 0B | AM | reg 20+op bit 7 |
| 0C | vibrato | reg 20+op bit 6 |
| 0D | connection | reg C0+ch bit 0 = (patch[0D] == 0) |
| 0E (first half only) | effect counter -> `eff[ch]+0` | pitch-effect unit (§7); 0 = ordinary note |
| 10 (first half) | mask -> `eff+2` | |
| 12 (first half) | base -> `eff+4` | |
| 14 (first half) | delta -> `eff+6` | |

Instruments with an effect (first-half `+0E` non-zero): 2, 3, 5, 10, 11, 12, 15-20, 24; instruments 42-59 of the table
printout are track data (the table ends at `0766`, the first track).

## 5. OPL access and voice primitives (all verified)

### 5.1 Register write

`opl(reg, val)` = `shadow[reg] = val; out 388h,reg; delay; out 389h,val` (state 0; `cs:036B`). The shadow is read by
`keyoff`, `pitch`, `vol`, `slide`, `r_bd`. Reg 04h writes appear only in the detection code.

### 5.2 Primitives

* `freq(ch, w)` (`cs:030B`): `opl(A0+ch, w&FF); opl(B0+ch, (w>>8)|20h)` (key on).
* `keyoff()` (`0E5E`): `ch=cur; opl(B0+ch, shadow[B0+ch] & DF)`.
* `vol()` (`0C2E`, state 0): `reg = 40h + op_off[chan_op[2*ch+1]]` (carrier); `opl(reg, (3F - ((vol+vofs)&3F)) | (shadow[reg] & C0))`.
* `pitch()` (`0D1C`): `q,r = divmod(pitch,12); f = fnum_tab[r] + s8(tune); opl(A0+ch, f&FF); opl(B0+ch, (shadow[B0+ch]&20) | (((f>>8)|(q<<2)) & FF))`.
* `slide()` (`0DCE`): `val = ((shadow[B0+ch]&1F)<<8) + shadow[A0+ch] + s8(slide)`; `opl(A0+ch, val&FF); opl(B0+ch, (shadow[B0+ch]&20) | (val>>8))`.
* `note_on()` (`0E8A`): `vol()`; if `eff[ch].counter == 0`: `pitch(); opl(B0+ch, shadow[B0+ch] | 20)`; else the effect-unit
  start of §7.1 (no pitch write here, the units' `freq()` calls set the pitch).
* `load_inst(n)` (`123E`): `opl(B0+ch, shadow & DF)` (key off); `p = 2E + 2C*n`; `setop(op_off[chan_op[2ch]], p, ch)`;
  copy the four effect fields `p+0E, p+10, p+12, p+14` to `eff[ch]`; `setop(op_off[chan_op[2ch+1]], p+16, ch)`.
* `setop(op, p, ch)` (`1048`) writes in this order (ASOUND.117): `opl(40+op, 3F)`; BD = `(shadow[BD]&3F) |
  (flags[0]?80:0) | (flags[1]?40:0)`; reg 08 = `flags[2]?40:0`; `C0+ch` = `(p[0A]<<1) | (p[0D]?0:1)`; `60+op` =
  `(p[0]<<4)|(p[1]&F)`; `80+op` = `(p[2]<<4)|(p[3]&F)`; `20+op` = `(p[0B]?80:0)+(p[0C]?40:0)+(p[4]?20:0)+(p[5]?10:0)+(p[9]&F)`;
  `E0+op` = `p[8]&3`; `40+op` = `((3F-(p[6]&3F)) | (p[7]<<6)) & FF`.

## 6. Hardware reset and snd_init (verified writes)

`reset_all()` (`018F` via `snd_init`/cmd 0): `busy = FFFF`; for each voice zero `t, slide, rstep, pstep`; zero the unit
counters, masks, bases, deltas; `hw_reset()` (`0210`: `bx=3F` first): `opl(5F..40, 3F)`, `opl(FF..60, 0)`,
`opl(3F..01, 0)`, `opl(01, 20)` (255 writes; the harness hides reg 04); restore `busy`. `snd_init` also runs the
detection (`005E`: AdLib timer test reg 04 <- 60,80,(read),02<-FF,04<-21,(read),04<-60,80; status & E0 must be C0, else
returns 41h "no AdLib"; port: skip). `[17D0]` and all other variables start from the values in the file image.

## 7. Poll and sequencer

### 7.1 `poll_body()` (`0430`) — verified

```
poll_count++                      (cs:000E)
lcg step                          (§9; ONE step per poll, before everything)
if busy != 0: return              (the result stays 0)
eng = eng_on && eng_value != 0
if eng: if voice6.t == 0 { voice6.ptr = E94; voice6.t = 1 } else { voice6.t = 2; voice6.rel = 2 }
update_all()                      (1882: cur = 0; voice_tick for voices 0..6)
if eng and eng_value != eng_last: eng_last = eng_value
        freq(6, eng_fnum)                      ; opl(A6, lo); opl(B6, hi|20)
        eng_vol = (eng_value % 55h) << 8 | (eng_value / 55h)) + 36h      ; 16-bit
        engine_vol()                           ; opl(50, 3F - eng_vol) ; opl(53, eng_vol)   (low bytes)
if unit_cnt0 | unit_cnt1 == 0:
        if rate_state != FFFF: rate_state = FFFF; poll_result = FFFF
        return poll_result
unit_tail()                       (below)
return poll_result                (0 unless a unit just started: 1)
```

`eng_fnum = (eng_value & FFFE) + 883h` (set by cmd 23/11), clamp `eng_value <= 301h`; `eng_value = 0` also clears
`eng_last`. The engine drone is voice 6 with track `E94` = `FC 09 F9 2B 00 03 00 00` (instrument 9, volume 2B, rest 3,
end); polls keep re-arming it so the instrument stays loaded; the pitch/volume come from the lines above.

`units_started()` (`05A5`, called by `note_on` when a unit starts): `if rate_state != 1 { rate_state = 1; poll_result = 1 }`.

`unit_tail()` (end of `0430`): for unit 0 (if `cnt0`): `base0 += delta0; cnt0--; if cnt0 == 0: ch = ch0; if !(cnt1 && ch == ch1):
opl(A0+ch,0); opl(B0+ch,0)`. For unit 1 (if `cnt1`): `base1 += delta1; cnt1--; if cnt1 == 0: ch = ch1; if cnt0 || ch != ch0:
opl(A0+ch,0); opl(B0+ch,0)`. All counters are bytes, bases/deltas 16-bit.

### 7.2 Effect units and `snd_noise()` (`0562`) — verified

Instruments with `eff.counter != 0` do not set a pitch; they claim one of two **units** that sweep a channel's frequency
randomly. `note_on` for such a note: `if ch == ch0: toggle = 0; if ch == ch1: toggle = 1;` then
if `toggle == 0`: `toggle = 1; if cnt0 { opl(B0+ch0, shadow & DF) } ch0 = ch; cnt0 = eff.counter; mask0 = eff.mask; base0 =
eff.base; delta0 = eff.delta` else `toggle = 0; if cnt0 { opl(B0+ch1, shadow & DF) } ch1 = ch; cnt1 = ...` (the second branch
tests `cnt0`, not `cnt1`: **original quirk, keep it**); both branches end with `units_started()`.

`snd_noise()`: `v = lcg_step(); if cnt0: freq(ch0, ((~v) & mask0) + base0); if cnt1: freq(ch1, (v & mask1) + base1)` (16-bit
arithmetic; `freq` writes A0+ch and B0+ch (|20)). Called once per poll, only in the fast mode (§3).

### 7.3 `voice_tick()` (`1360`) — verified

```
if t == 0: cur++, return                         (inactive voice: dirty untouched)
if rel != 0: rel--; if rel == 0: keyoff()
t--
if t == 0: read_events()
17D1:  if slide != 0: slide()
       dirty = 0
       if rctr == 0 && pctr == 0: cur++; return
       rctr--                                    (byte, wraps if it was 0)
       if rctr == 0: rctr = rrel; if rstep != 0: vofs += rstep; dirty = 1
       pctr--
       if pctr == 0: pctr = prel; if pstep != 0: pan += pstep; dirty = 1
       if dirty: vol()
cur++
```

`read_events()` (`13A5`): loop: `p = ptr; c = mem[p]`; if `c >= F3` it is an event, run it **after setting `dirty = 0`**
(`13C8`) and loop again; otherwise it is a note: `if dirty: vol()`; `pitch_b = mem[p]; t = mem[p+1]; ptr += 2;
if pitch_b == 0 || t == 0: keyoff() else { rel = t - gate (byte); note_on() }` and leave the loop. A note with duration
0 therefore ends the track (`t` stays 0): the end marker is `00 00`.

**Bytes below F3 (including 80..F2) are all note pitches**; pitch is used as `pitch/12` octave (can reach 20: octave bits
overflow into the key-on bit, original behaviour).

### 7.4 Events (`1735` jump table at `cs:173A`, index = byte - F3)

| Byte | Operands | Action |
|---|---|---|
| F3 | a, b | `prel = a; pstep = b; pctr = 1; ptr += 3` |
| F4 | a | `pan = a; ptr += 2; dirty = 1` |
| F5 | a | `vofs = a; ptr += 2` |
| F6 | n, o0..on-1, d | **random patch**: `r = lcg_step() & (n-1)`; `mem[ptr+2+n+1+d] = mem[ptr+2+r]`; `ptr += n+3` (modifies the track in RAM; the port must copy tracks to writable memory) |
| F7 | a | `tune = a; ptr += 2` |
| F8 | a, b | `rrel = a; rstep = b; rctr = 1; ptr += 3` |
| F9 | a | `vol = a; ptr += 2; dirty = 1` |
| FA | a | `slide = a; ptr += 2` |
| FB | a | `gate = a; ptr += 2` |
| FC | a | `inst = a; ptr += 2; load_inst(a)` |
| FD | — | if `lptr == 0`: `ptr = start` else `start = ptr = ls1 = ls2 = lptr` (never advances `ptr` otherwise: the track loops forever) |
| FE | n | loop 2: if `cnt2 == 0`: (n==0: `ptr += 2; ls2 = ptr; ls1 = ptr; cnt1 = cnt2 = 0`; else `cnt2 = n; ls1 = ls2; ptr = ls2`); else `cnt2--; ls1 = ls2; if cnt2 == 0 { ptr += 2; ls2 = ptr }; ptr = ls2` |
| FF | n | loop 1: if `cnt1 == 0`: (n==0: `ptr += 2; ls1 = ptr`; else `cnt1 = n; ptr = ls1`); else `cnt1--; if cnt1 == 0 { ptr += 2; ls1 = ptr } else ptr = ls1` |

Both loop counts repeat the body **n more times** (n+1 passes). `ls1` is re-seated after every loop-1 exit (a series of
FF loops in one track form consecutive segments); FE resets both loops. At voice start `ls1 = ls2 = start = ptr = track`.
`vol`, `vofs` are not reset by a new track on the same voice (only `slide, rstep, pstep, tune, vofs` are zeroed; `vol` keeps
its previous value until the track's first F9).

### 7.5 `dirty` flag (stale local)

`dirty` is the local `[bp-4]` of `1360`, **never initialised at entry**. The frame is at the same stack address for every
voice of every poll, so the value left by the previous non-skipped voice call (set to 1 by a ramp step at the end of
`17D1`, 0 otherwise) is the value seen by the first note of the next call before any event clears it. Model it as a
single byte (`DS:17D0`): `event` sets it to 0 first; F9/F4 then set it to 1; `17D1` sets it to 0 and a ramp step may
set it to 1; a note start calls `vol()` first if it is 1 (so a note after F9..., FF 00, ... writes the volume once
in `note_on` and once more only if no event ran between the ramp and the note). This reproduces all 40 commands.

## 8. Commands (`cs:0911`, 46 entries; AX = 0, BX = 2*cmd on entry) — verified for 0-39

`spawn(v, track[, ax])` (`05E3..05C0`, `05E7`) = voice `v` (0..6 = `5E3, 5DC, 5D5, 5CE, 5C7, 5C0, 5B9`): with `busy = 0` during the
call: zero `slide, rstep, pstep, tune, vofs, gate`; `rrel = FF; pan = 40`; `start = ptr = ls1 = ls2 = track`; `cnt1 = cnt2 = 0`;
`lptr = ax`; `t = 1`. The voice therefore reads its first events on the next poll.

| Cmd | Handler | Action |
|---|---|---|
| 0 | `018F` | `reset_all()` |
| 1 | `0761` | soft stop: `ls1 = E9A` of every voice (the next FF/FE loop jump lands on `00 00` = end) |
| 2 | `0839` | returns OR of the seven `t` bytes (non-zero = something plays) |
| 3 | `0694` | v0<-FE0, v1<-11AC, v2<-121A, v3<-12FC, v4<-1448, v5<-154C (PLAYER intro soundtrack) |
| 4 | `0876` | v1 <- `cs:[086E + ((2*4)>>3 & 6)]` = 1542 |
| 5 | `085F` | v0 <- `cs:[0857 + ((2*5)>>2 & 6)]` = 0F00 |
| 6 | `0887` | v4<-F28 |
| 7 / 8 / 9 | `08B7`/`08B0`/`089C` | v3<-F52 / F4A / F3C (cmd 9 does nothing if v3 is already playing F3C) |
| 10 | `0895` | v5<-F94 |
| 11 | `08E8` | `if eng_value == 0: set_engine(1)`; v3<-FA2 |
| 12 | `08FC` | v3<-FB4 |
| 13 | `088E` | v0<-F34 |
| 14 | `08DA` | v5<-F8A |
| 15 / 16 | `08E1`/`08CC` | v0<-F78 / F70 |
| 17 | `08C5` | v1<-F68 |
| 18 | `08D3` | v5<-F80 |
| 19 | `0903` | v3<-FC8, v2<-FD4 |
| 20 | `08BE` | v5<-F5A |
| 21 | `0782` | mute: `unit_save0/1 = cnt0/1; cnt = 0; busy = FFFF`; `opl(r, shadow[r] | 3F)` for r = 43,44,45,4B,4C,4D,53 |
| 22 | `07C7` | unmute: `opl(r, (shadow[r]&C0) | (voice.vol ^ 3F))` for the same r with voices 0..6; restore the counters; `if either: units_started()`; `busy = 0` |
| 23 | `03D5` | `set_engine(arg)` |
| 24 / 25 | `041E`/`0424` | `eng_on = FF` / `eng_on = 0; eng_last = FFFF` |
| 26 | `0634` | v5<-E9C |
| 27 | `0647` | v0<-EB4 |
| 28 | `064D` | `r = lcg_step() & 3; mem[EC0+6] = r+21; spawn(v0, EC0, ax = r+21)` |
| 29 / 30 | `0662`/`0669` | v0<-ED2 / ECA |
| 31 / 32 | `0686`/`068D` | v2<-EE4 / EEC |
| 33 | `0670` | `r = lcg_step() & 3; mem[EDA+6] = r+1; spawn(v0, EDA, ax = r+1)` |
| 34 / 35 | `063A`/`0640` | v5<-EAC / EA4 |
| 36 | `06E8` | v0<-876, v1<-992, v2<-A9A, v3<-B2E (END medal/promotion jingle 1) |
| 37 | `0720` | v0<-B66, v1<-BA0, v2<-BDE, v3<-C1C, v4<-B5E, v5<-B5E; then `v4.lptr = B6A; v5.lptr = BA6` |
| 38 | `0704` | v0<-C76, v1<-D10, v2<-DB2, v3<-E24 |
| 39 | `06BE` | v0<-766, v1<-7B4, v2<-7CE, v3<-7EE, v4<-830, v5<-85C (long tune, loops) |
| 40 | `0B90` | `speech_seg = arg` |
| 41..45 | `096D..09A1` | speech (§10) |

(cmd 28/33: the random byte is also left in AX, which `spawn` stores as the voice's `lptr`; harmless because those tracks
never execute `FD`; keep it for fidelity.) Cmd 21/22 are used around speech and do not touch the engine drone.
Commands per program (RE_GUIDE §5): START 2, 26-31, 33, 34; VGAME 0, 19, 23-25, 32, 40-45; END 2, 26, 28, 36-39; PLAYER 2, 3.

## 9. Random numbers (`cs:03C4`)

`lcg = ror16(lcg + 9248h, 3)` returning the new value. Seed `04D2h`. Steps happen: once per poll (start of `poll_body`),
once per `snd_noise`, once per cmd 28/33, once per F6 event. Since the seed is global and all sources interleave, the sound is
deterministic for a given command/poll schedule.

## 10. Speech (SPEECH.117, cmds 41-45; `0976..0B56`) — read, AdLib path

* Data: 26 408 bytes of unsigned 8-bit PCM at `speech_seg`. Clip lists (records `end, start, para`, all word offsets inside
  the segment `speech_seg + para`; the list ends at `end == 0`): cmd 41 `[0, 9250)`; cmd 42 `[4216, 9250)`; cmd 43 *even*
  `poll_count`: `[9251, 10571)` then `[12829, 15653)`, *odd*: `[10572, 12828)` then `[12829, 15653)`; cmd 44 `[15654, 18342)` then
  `[18343, 22118)`; cmd 45 `[15654, 18342)` then `[22119, 26407)`. Clip ends are exclusive; `speech_all` S2.4 slices at
  0, 4216, 9251, 10572, 12829, 15654, 18343, 22119, 26408.
* Rate: PIT counter 2 reloaded with 96h = 150 counts (`out 43h,94h; out 42h,96h`), i.e. **7954.5 Hz** (1 193 182/150),
  and every sample waits for the counter to wrap.
* Sound path: first `stop_all_wait` (`025A`): `busy = FFFF`; `opl(80..95, 0E)` (all release rates 14); zero the units; every
  voice (0..5) `t = 1, ptr = E9A`; `busy = 0` and **block until all six voices are inactive** (needs the ISR polling, so the port must
  keep polling inside this wait); then `opl_speech_setup` (`0A5B`): regs 20,23,40,43,60,63,80,83,C0,E0,E3,B0,A0,B0 <-
  23,28,3F,3F,AF,AF,0D,0F,05,00,02,01,8F,2E; PIT0 reload 4C90; busy-wait until the counter has moved 988h; A0<-0, B0<-20.
  Then the loop: for each clip/sample: `opl(43, speech_vol[sample >> 2])` (carrier level of channel 0 = 6-bit amplitude), wait for
  the next 7954 Hz period. At the end: B0<-0, B1<-0; restore PIC mask `[17E0]`, port 61h `[17E1]`, PIT2 `B6; 0; 0`.
* State 1/2 (SB) plays the same clips by DMA (`18C8` family): not ported.
* Port: play the PCM through the host mixer (linear 8-bit -> float) at 7954.5 Hz (or 8000 Hz) on its own channel; keep the
  `opl_speech_setup`/`43`-register writes out (the host mixes digital audio itself). While speech plays the original silences
  music and effects (stop_all_wait first); the port should do the same so timing of the 41..45 callers is unchanged.

## 11. Logo overlay (ASOUND.LOG; MPS_LOGO only) — verified

Same sequencer, older build. DS = image 11C0h (BSS up to 680h bytes); header and entries: RE_GUIDE §5. Differences from §4..§8:

* Voice records are 1Ah bytes at `DS:043C + 1A*v` (7 slots, 6 used). Offsets: `t 00, slide 01, rstep 02, pitch 03, inst 04, vol 05,
  gate 06, rel 07, rctr 08, rrel 09, start 0A, ptr 0C, ls1 0E, ls2 10, cnt1 12, cnt2 14, lptr 16, tune 18, vofs 19`.
* Events are F5..FF only (no F3/F4, no pan); `F9` and the ramp step call `vol()` immediately (no `dirty` flag).
* Globals: shadow `0516`, `eff` `0616`, units `050C/050D` counters, `050E/0510` masks, `0432/0434` bases, `0436/0438` deltas,
  `0512` cur, `0513` toggle, `0514/0515` unit channels, `03CE` lcg (seed 04D2), `043A` playing (0 = stopped), `chan_op` `0404`,
  `op_off` `0416`, `fnum_tab` `03E8`, flags `0400..0402`, instruments at `DS:0050 + 2C*n` (4 used: 0-3).
* `setop` order (`0BE2`): BD(+08 included), `40+op` (final level), `C0+ch`, `60`, `80`, `20`, `E0` (**no** initial `40 <- 3F`).
* Init (`019A` + `0220`): clear voices and units, `playing = FF`, `opl(FF..01, 0); opl(01, 20)`.
* `cmd 0` = init, `cmd 1` = every voice `ptr = 0100` (silence stream), `cmd 2` = start voices 0-5 on tracks
  `010E, 0120, 014C, 017A, 01A6, 01B6` (`spawn` = `0515`: `rrel = FF`, zero `slide, rstep, tune, vofs`, `gate = 0` word fields as §8).
* Timing: the overlay installs **its own INT 8** (`03F8`/`04AE`): slow mode PIT reload **4C90h = 60.88 Hz**, ISR `04E5`: sequencer
  step every ISR (`0344`: lcg step; if `playing`: `update_all`; if no unit active: back to the slow ISR; else `unit_tail`), chain to the
  old INT 8 every 3rd tick. Fast mode (a unit started, `04AE`): reload **0F88h = 300.1 Hz**, ISR `042F`: lcg step, noise (§7.2) every
  tick, sequencer every 5th tick (60 Hz). The jingle never starts a unit (verified; the fast mode is modelled but not exercised).
  Total length 6.57 s (400 ticks) of the 1 500 tested.
* The overlay also runs a CPU-speed calibration (`00FE`, delay loop vs PIT, sets OPL write delays `[03D2]`,`[03D4]`): the port skips it.
* Entries 100-104: `init(letter)` (stores `[03CC]`, hooks INT 8, detects AdLib), `cmd(n)` (n <= 2), `shutdown`, 103/104 `retf`, 105 returns
  the tick counter `cs:0050`. MPS_LOGO pairs the jingle with `LABSLOGO.SS` timing (S3.4).

## 12. Module outline for the C port (`f117port/src/snd_*.c`, S4)

* `snd_opl.c`: shadow + Nuked-OPL3 write queue with poll time stamps; `opl(reg,val)`.
* `snd_seq.c`: `struct seq_layout` (field offsets, table addresses) with two instances (Game, Logo); the shared voice engine of §5-§7
  and the hooks that differ (`setop` order, `dirty`, event table).
* `snd_game.c`: init/cmd/poll/noise, command table, engine drone, rate steering (`snd_poll_result`), speech.
* `snd_logo.c`: `cmd 0-2`, slow/fast ISR scheduler.
* `snd_host.c`: SDL3 audio stream, mixes OPL output (49 716 Hz) with the speech PCM.
Host glue: the original driver calls are replaced by direct C calls (`snd_cmd(n, arg)` etc. from the program ports).
The 7 voices, 42 instruments and tracks are loaded from `Game/ASOUND.117` at start-up (`seg:off` -> byte array offset).

## 13. Verification (tools/sound/asnd.py)

* `python tools/sound/asnd.py verify 2500`: **40/40 commands (0-39) are write-for-write identical** (same register, value and
  order, per poll) to the real `ASOUND.117` run under Unicorn, 2 500 polls each (cmd 3: 10 997 writes in 4 000 polls). Extra
  scenarios (soft stop mid-tune, mute/unmute, engine drone with changing RPM, cmd 9/11 re-triggers, random cmds 28/33, mixes of 6 commands)
  also identical. Not covered: cmds 41-45 (speech) and non-zero `state`.
* `python tools/sound/asnd.py verifylog 1500`: the logo overlay, 352 writes, identical.
* `python tools/sound/asnd.py steer CMD [secs]` renders a command with the §3 rate rule to
  `work/sound/asound_cmdNN_steered.wav` (needs `work/oplrender.exe`). cmd 3 turns out to be ~55 s long (the S2.4 render stopped at the
  first 2 s silence) and cmd 39 loops past 90 s.
* Quirks found and kept: the stale `dirty` stack slot, the `cnt0` test in unit 1's start, the 16-bit `rctr/pctr` byte wrap, the self-patching
  F6 event, cmd 28/33 leaving the random byte in `lptr`, octave bits overflowing into the key bit.

## 14. Open items

* Frame rate for the idle poll (70.086 Hz in mode 13h) is derived from `timer_calibrate`; PLAYER's ISR was only compared by shape.
* U5-style listening check of the steered cmd 3 (and 36-39) is a PLAN user task.
* ISOUND/RSOUND/NSOUND drivers (and SB/PAS states of ASOUND) are not specified.
