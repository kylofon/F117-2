# Debrief: END.EXE (S3.9)

Spec of the third program of the F-117A chain (`START -> VGAME -> END -> START ...`). END receives the finished mission from VGAME
through the comm block, plays the debrief (map replay, score, awards, promotion, ending scene), updates the **active pilot
record** and exits with code 23h; `START` then copies the record into the roster. END reads no mission file and writes no file.

* Source: `work/unpacked/END.EXE` (LZEXE-unpacked, 46 039 byte image, code segment 0000, DGROUP = segment 05FA, driver stub
  table `DS:23FA`). `python tools/spec_coverage.py end` = 196/196 (100 palette/RTL/timer/keyboard/file rows are in
  `platform_symbols.csv`; the 96 program rows are in `debrief_symbols.csv`).
* Verified: every piece of arithmetic, text assembly and screen sequence below was transcribed to Python and compared with the real
  code under Unicorn (`port/notes/s39/`, section 12): 10 200 random cases, 0 mismatches. (V) = verified by that test or by direct
  disassembly, (U) = needs a visual / runtime check, (B) = original bug or quirk that the port must keep.
* Toolkit: about 40 of END's functions are functionally identical copies of the START UI toolkit (`shell.md` Part B). They are listed in
  section 10 with the START address; only the differences are specified here.
* Notation: `DS:xxxx` = END DGROUP offset. `comm` = far pointer `DS:7222/7224` (offset 0, segment from `0000:04F0`), `pilot` = far pointer
  `DS:55DE/55E0` = `comm + 1272h` (`+N` = field offset in the 50h-byte record). Word = 16 bit, `[xxxx]` = DS word/byte (size given), `DS[xxxx]` = the near string pointer stored at DS:xxxx.
  Arguments are in C order (cdecl). `blit(spage,sx,sy,dpage,dx,dy,w,h)` = MGRAPHIC slot 42, `snd(n)` = slot 101, `text_wrap(...)` = 1A75.

## 0 What END does in one page

```
main():                                    // 0000:0010
    init()                                 // 3BF4: comm/pilot pointers, drivers, blob import, 5 pages, sprites
    if (comm.30 == 0) pilot.36++           // missions flown (not for training missions)
    [6440] = 3
    switch (comm.26):                      // how the flight ended
        1: cemetery screen                 // crash
        2: if (landing_class() != 0) rescue screen else { captured screen; [6982] = 1 }
        3: landed screen                   // safe landing
    summary_screen()                       // 2BEC  paragraph + rating on flag.pic
    map_screen()                           // 03D9  route replay on the theatre map, running score; stores the score in the pilot
    awards_screen()                        // 2265  promotion, medals, ribbons
    ending_scene()                         // 39D0  quips (only if comm.28 == 0 && comm.30 == 0)
    if (pilot.36 == 99) pilot.4E = 1       // retired
    if (comm.26 == 1 && comm.30 == 0) pilot.4E = 2     // KIA
    exit_program(0x23)                     // 3F44
```

Everything else is the machinery for those screens. END is the only program that evaluates the **event log** (`DS:5E38`, 6-byte records
written by VGAME's `mission_log`), so the scoring rules live here, not in VGAME.

## 1 Inputs and outputs

### 1.1 comm fields read by END (all written by START / VGAME)

| off | size | written by | meaning for END |
|---|---|---|---|
| 1A, 1C, 1E | w | F117.COM | MGRAPHIC / sound / MISC driver segments (`ovl_bind` x3 in `init`) |
| 26 | w | START (3), VGAME init (1), VGAME eject (2), VGAME normal landing (3) | **how the flight ended**: 1 = crashed / killed, 2 = ejected, 3 = landed safely. END dispatches on this (settles simflow open item 1). VGAME writes 1 at mission start (`39C4:3B0F`) and no crash path changes it (corrected 2026-10-13, see HANDOFFS C1/C2) |
| 28 | w | VGAME `mission_end(code)` | crash cause 0..6, see 1.2. 0 together with `26==2` = ejection survived |
| 2C | w | VGAME | enemy missiles launched at the player (SAM firing + air-to-air launches, VGAME `5433` / `6586`), **not kills** (corrected 2026-10-13, see HANDOFFS section 8 question 3); `< 3` selects the "quiet mission" quips |
| 2E | w | START mission generator | route cost (12 x route score); score bonus bands in 4.3 |
| 30 | w | START (`pilot.3C > 1`) and VGAME Alt-T | **non-scoring flag** (training mission / training toggle was used). Many branches, see 1.4 |
| 33 | b | START | 1 = night mission (clock base 18:00:00 instead of 06:00:00) |
| 34 | w | VGAME `mission_end` | `[3632]` damage bit mask (flight.md 6.1): bit 2 = flight controls, bit 3 = navigation |
| 36 | w | VGAME `mission_end` | `[C5C8]` hits taken (starts at 2) |
| 38..3E | 4 w | START | weapon id of bay 0..3 (name = `DS:1B98[2*id]`), used by event 4 |
| 48 | 40 B | SETUP | joystick calibration (passed to `joy_set_calib`) |
| 72 | w | SETUP | bit0 = joystick, bit1 = mouse |
| 74, 76 | w | VGAME `mission_end` | final x, y in level units (grid cell = `>> 11`, 16 x 16 cells) |
| 7A.. | blob | START `wld_export_to_comm` + VGAME `mission_world_export` | the mission world with the **event log**, see 1.3 |
| 1272.. | 50h | START | active pilot record |

END never writes comm. (V: no store through the comm pointer anywhere in the program.)

### 1.2 `comm+28h` and `comm+26h` (settles simflow open item 1)

| comm.26 | comm.28 | raised by (VGAME) | END screen / wording |
|---|---|---|---|
| 3 | 0 | normal landing (`4926(0)`, `[C06E]==0`) | `landed1` scene; "landed safely" paragraph |
| 2 | 0 | eject key survived (`cba0` sets 2, ends later with `4926(0)`) | grid class of the final position: != 0 `rescue`, == 0 `tass` (captured) |
| 1 | 1 | stranded on the ground outside an airfield (`39c4` 41b6) | `cemetary`: "Your plane crashed spectacularly into the ground." |
| 1 | 2 | terrain collision (`39c4` 41f8) | "Your plane exploded as it slammed into a hill." |
| 1 | 5 | hard landing / gear failure (`1e9e` 2b65) | "Your plane crashed onto the runway, cartwheeled and exploded." |
| 1 | 6 | fatal crash, eject attempt failed (`cba0` D01E) | "Your prudent attempt to eject from the aircraft failed." |
| 1 | 3, 4 | **never raised by VGAME** | texts exist ("Your main tanks ran out of fuel and you went down.", "Destroyed by enemy missiles, your plane crashed before you ejected.") |
| 1 | 0 | impossible in a normal run | the summary paragraph builds no text (the original prints stack garbage, (B)) |

`comm.28 != 0` is also the **"pilot is dead"** test: it selects the "killed in action" paragraph, the word "posthumously " in medal and
promotion lines, and suppresses the retirement text. `comm.28 == 0` with `comm.26 == 2` is the ejection that was survived.
Exit code 7 (quit without result) never reaches END (VGAME exits with 0).

### 1.3 The mission blob `comm+7Ah` (imported by `0113`)

`0113` sets the cursor `[543C:543E] = comm + 7Ah` and reads, with `blob_get(dst, size, count)` (0242: `movedata(cursor -> DS:dst, size*count)`,
cursor += size*count), exactly the field order of VGAME's `wld_blob_fields` (simflow.md section 4):

| # | DS dst | bytes | meaning (VGAME name) |
|---|---|---|---|
| 1 | 6983 | 2 | theatre object ids (`wld_obj_a/b`); unused by END |
| 2 | 6446 | 2 | `site_count` |
| 3 | 55E4 | 2 | number of leading sites with a threat level (unused) |
| 4 | 7210 | 2 | first non-target site index (unused) |
| 5 | 56EA | 16 * site_count | **site records**: +0 place-name index, +2 x, +4 y (level units, 16-bit), +6 threat (0 = dead), +8 flags, +A unit type, +C count, +E object id (low byte = noun-name index and object-class index) |
| 6 | 6D8A | 2 | `unit_count` (unused by END) |
| 7 | 64CA | 36 * unit_count | unit records (unused) |
| 8 | 6902 | 128 | **object class table** (byte per object type): bit7 = civilian, bit6 = friendly (own-side) installation, low nibble = score class 0..15 (0Dh: the text says "damaged" instead of "destroyed") (U meaning of the classes) |
| 9 | 6448 | 128 | kill counters per object type (VGAME `C0BA`); unused |
| 10 | 6EFA | 750 | NUL separated name pool |
| 11 | 55EA | 256 | **16 x 16 grid**, row = y>>11, col = x>>11; `& 3` = territory class: 0 = hostile, != 0 friendly |
| 12 | 543A | 2 | flight time estimate (unused) |
| 13 | 5438 | 2 | special unit index (unused) |
| 14 | 5DA4 | 16 | waypoints (unused) |
| 15 | 71E8 | 36 | two 18-byte mission slots: +0 kind, +2 target site, +4 departure site (slot 0: takeoff point), +6 flags, +8 template index (0..81) |
| 16 | 5E38 | 256 * 6 | **event log**: `{u16 tick; u8 x = X>>7; u8 y = Y>>7; u8 type; u8 arg}`, terminator record type 0. END's clock shows 1800 ticks per hour, i.e. 2 s per tick (U) |

`0242` also exists as the writer `0273` (dead). Name pointer table: `00C9` builds `DS:6DFA[0] = 6EFA` and for every NUL at
pool offset i (0..2EDh, while fewer than 128 entries) `table[j++] = &pool[i+1]` (entry 0 = first string; same as VGAME 4AD0).

Event types (`type & 3F`; bit 7 = primary objective, bit 6 = secondary objective flag; `arg & 7F` = index; flight.md 2.9 lists the writers):

| t | event | arg | t | event | arg |
|---|---|---|---|---|---|
| 1, 12 | registered object (site) destroyed | site index | 7 | site/target raised to "Radar track" | site index |
| 2 | unregistered world object destroyed | object type | 8 | mission start (first record) / end (last) | 0 |
| 3 | unit destroyed (arg bit 7 = unit flag 4000h) | unit type 0..28 | 9 | position sample (every 32 s) | 0 |
| 4 | store released | bay 0..3 | 10 | target photographed | site index |
| 5 | player hit by a missile | missile type 0..7 | 11 | cargo delivered | 0 |
| 6 | enemy unit fired at the player ("Visual ID") | unit type 0..28 | 0 | terminator | |

### 1.4 What `comm.30` (training / non-scoring) changes

`pilot.36` is not incremented (main), `pilot.30` is stored as 0 (map screen), no promotion (2AFA returns), no ribbons (2476), the summary
adds ", but will not be recorded as this was a training flight.", the dead/alive paragraph uses the "If this had been an actual combat
mission ... would now be dead" wording, the awards screen is skipped unless an objective was accomplished (then only the
Combat Readiness Ribbon, `pilot.44`), the ending scene is skipped, the pilot is never marked KIA.

### 1.5 pilot record fields (50h bytes at `comm+1272h`; layout owner: shell.md "Pilot record")

| off | read | written | meaning |
|---|---|---|---|
| +02 | init | | name (30 bytes); display name `DS:7228` = short rank + name |
| +20 | yes | `++` on promotion | rank 0..6 (2nd Lt, 1st Lt, Capt, Major, Lt Col, Col, B Gen) |
| +22 | yes | = 1 | Purple Heart (only ever 0/1) |
| +24, +26, +28, +2A | yes | `++` | Airman's Medal (max 9), Distinguished Flying Cross, Silver Star, Air Force Cross counts |
| +2C | yes | = 1 | Congressional Medal of Honor (only ever 0/1) |
| +2E | yes | max | best mission score |
| +30 | yes | = score (0 if `comm.30`) | last mission score; also the value the medal tests use |
| +32 (dword) | yes | `+= score` (16-bit, zero extended, carry into +34) | career score |
| +36 | yes | `++` | missions flown |
| +38, +3A, +3C, +3E, +40, +42 | yes | | theatre 0..8, war level 0..2, mission type 0..3, opponent level 0..3, landing realism 0..2, aircraft model 0..1 (score multipliers, 4.3) |
| +44 | yes | = 1 | Combat Readiness Ribbon (training mission with an objective) |
| +46 | yes | = 1 / 0 | OverSeas Short Tour Ribbon (missions 5..9; cleared when the long ribbon is earned) |
| +48 | yes | = 1 | OverSeas Long Tour Ribbon (missions >= 10) |
| +4A | yes | = 1 / 0 | Longevity Service Ribbon (missions 30..59; cleared by the next) |
| +4C | yes | = 1 | Longevity Service Ribbon with Gold Cluster (missions >= 60) |
| +4E | | = 1 / 2 | status: 0 active, 1 retired (99th mission), 2 KIA. START opens the roster screen when it is non-zero |

(START's roster screen does not display +44..+4C; they only drive the ribbon strip of the awards screen, section 7.4.)

### 1.6 Global state flags (DGROUP, all bytes)

| DS | name | set by | meaning |
|---|---|---|---|
| 6440 | `landing_class` | main (=3), 00A1 | 0 = hostile ground (captured), != 0 friendly; 3 for crash and landing |
| 6982 | `captured` | 3BBF | pilot was captured: medals become "would have been" lines, promotion withheld |
| 6D89 | `primary_done` | 3021 | primary objective accomplished (recomputed by every score call) |
| 720E | `secondary_done` | 3021 | secondary objective accomplished |
| 64C8, 55DC | `air_kills_a`, `air_kills_b` | 3021 | enemy aircraft shot down (flagged / war != 0, cold war unflagged); total printed = sum |
| 6984, 6D88 | `ground_kills`, `mil_kills` | 3021 | enemy installations destroyed, "unauthorized military" kills (Rules Of Engagement) |
| 5B9A | `civ_kills` | 3021 | civilian targets destroyed |
| 55DB, 55E8 | `friendly_ac`, `friendly_inst` | 3021 | friendly aircraft / installations destroyed |
| 55E6 | `promoted` | 2AFA | rank was incremented |
| 55E7 | `promo_withheld` | 2AFA | promotion earned but pilot was captured |
| 643A | `medal` | 2476 | medal awarded this mission: 1 PH, 2 AM, 3 DFC, 4 SS, 5 AFC, 6 CMH (0 none) |
| 55DA | `medal_ghost` | 2476 | same numbering, but not awarded (captured or training) |
| 55DD | `crr_ribbon` | 2476 | Combat Readiness Ribbon earned now |
| 55E2 | `ribbon` | 2476 | ribbon earned now: 1 short tour, 2 long tour, 3 longevity, 4 longevity with gold cluster |
| 5B9E | `hover_button` | 046E | button (1..2) currently highlighted on the map screen, 0 none |
| 55AA, 55AC | `route_x`, `route_y` | 0714 | last point of the drawn route (screen pixels) |
| 55AE, 55B0 | `icon_x`, `icon_y` | 0880 | top-left of the 48 x 40 event picture on screen |
| 54B8 | `icon_count` | 05CA | number of site icons |
| 55BE, 55C0 | `quip_n`, `quip_tab` | 39D0 | count and table pointer of the chosen quip table |

## 2 Main flow in detail

### 0000:0010 `end_main` void end_main(void) (V)
As the pseudo-code in section 0. Order matters: `pilot.36++` happens before the outcome screens; `[6440]=3` before the switch (so a crash
or landing has class 3); the captured screen sets `[6982]=1` **before** `summary_screen` / `awards_screen` read it; `pilot.4E` is written
last. `exit_program(0x23)` = `3F44(0x23)`. The two checks on `pilot.36 == 0x63` / `comm.26 == 1` use the already incremented count.

### 0000:00A1 `landing_class` int landing_class(void) (V)
`x = comm.74 >> 11; y = comm.76 >> 11; [6440] = grid[(y << 4) + x] & 3; return [6440]`. (`comm.74/76` are 16-bit level units, so x, y
are 0..15; no clamp.) 0 = hostile cell = captured.

### 0000:00C7, 00C8 `end_nop_a`, `end_nop_b` (V)
`ret` stubs called from the C start-up tables (platform.md names them).

### Exit: 0000:3F44 `end_exit` void end_exit(int code) (V)
`snd(0)`; if `[7220] != 0` free that segment (`mem_free_chk`); if `[2608]` (Ctrl-Break hook installed) `break_hook_remove`; if `[50CB]`
(timer installed) `timer_remove`; if `code == 0` set text mode (INT 10h AX=0003); MISC slot 94 (`misc94`); `exit(code)`.
Callers: main (`0x23`), `ui_check_break` 2023 (code 0, Ctrl-Break), quit prompt 3E61 (code 0). Code 0 quits the whole chain, 23h returns to START.

### 0000:3E61 `quit_prompt` void quit_prompt(int save_screen) (V)
Alt-Q handler (called by `wait_input` with 1 and by `poll_input` with 0). `snd(0)`; save the 768-byte palette buffer `DS:1F25` on the stack; `fade_out(1)`;
if `save_screen` `blit(0,0,0,1,0,0,320,200)` (screen into page 1); read `adv.pic` into the file buffer, decode it to page 0 (`pic_to_seg_b(buf, [55D0])`),
read `adv.pal` into `DS:1F25`, `fade_in(1)`; wait for a key (`kbd_empty` loop, then `kbd_get`); `(key & 5F) == 'Y'` -> `end_exit(0)`; otherwise
`fade_out(1)`, `blit(1,0,0,0,0,0,320,200)` (restore), restore the palette buffer, `fade_in(1)`.
(B) `poll_input` passes 0, so the screen is not saved and page 1 (the working copy) is shown again. Same as START `ui_quit_prompt` 7611 (shell.md).

## 3 Initialisation

### 0000:3BF4 `end_init` void end_init(void) (V)
1. `3CA1()`. 2. `ovl_bind(comm.1E)`, `ovl_bind(comm.1A)`, `ovl_bind(comm.1C)` (platform.md: becomes a no-op in the port). 3. `[7220] = [6444] =
mem_alloc_chk(0x88B8, 0)`; `[6442] = 0` (**(B) allocated again in 3CD0, the first block is leaked**). 4. `srand(bios_timer())`. 5. if `comm.72 & 1`
`joy_set_calib(comm+48h)`; if `comm.72 & 2` `mouse_init()`. 6. `break_hook_install()`. 7. `00C9()` (blob). 8. `strcpy(DS:7228, DS:1B8A[2*pilot.20])` (short rank,
e.g. `"Capt. "`, table in A.3), then `str_copy_far(DS:7228 + strlen, pilot+2)` (name) -> **display name**. 9. `3CD0()`, `3D4F()`. 10. tail call `timer_install()` (4B8C).

### 0000:3CA1 `init_pointers` void(void) (V)
`[7224] = [55E0] = word at 0000:04F0` (comm segment); `[7222] = 0`; `[55DE] = 0x1272`.

### 0000:3CD0 `init_pages` void(void) (V)
`[7220] = [6444] = mem_alloc_chk(0x88B8, 0)` (35 000 byte file buffer), `[6442] = 0`; slot 14 `page_select(0)`. For `i = 0..4`: `seg = gfx_alloc(i)` (slot 0; page 0 = A000h, others
64 000-byte blocks), `[55D0 + 2i] = seg`, if `i != 0` slot 75 `page_seg_set_b(seg, i)`; context `ctx[i]` at `DS:6D8C + 16h*i`: `+0 page = i`, `+0E ymin = 0`,
`+10 ymax = 0C7h`, `+12 xmin = 0`, `+14 xmax = 13Fh` (other fields stay 0; fg/bg/font are set by the screens). `ctx[0]` = `6D8C` draws on the screen,
`ctx[1]` = `6DA2` on the working page; `ctx[2..4]` (6DB8, 6DCE, 6DE4) are only used by dead code.

### 0000:3D4F `init_sprites` void(void) (V)
`load_pic("requestr.pic", page 2)`; `pool_init()` (slot 76); then `sprite_grab(page 2, x, y, w, h)` (slot 77):

| DS var | x | y | w | h | use |
|---|---|---|---|---|---|
| 5DA0 | 312 | 161 | 3 | 5 | neutral / dead site dot |
| 5DB6 | 234 | 104 | 86 | 20 | button panel of the map screen |
| 643E | 309 | 184 | 11 | 16 | mouse cursor (passed to `cursor_init`) |
| 5B9C | 315 | 161 | 5 | 5 | unused |
| 6D86 | 317 | 156 | 3 | 5 | unused |
| 720C | 306 | 161 | 3 | 5 | active threat dot |
| 7226 | 309 | 161 | 3 | 5 | removable threat dot (site flag 8) |

then `pool_free()` (slot 78) and `ui_cursor_init([643E])` (1FE5). Page 2 keeps requestr.pic until another screen overwrites it.

### 0000:00C9 `name_table_build` / 0000:0113 `blob_import` / 0000:0242 `blob_get` / 0000:0273 `blob_put`
`00C9`: `blob_import()` then the name table of section 1.3. `0113`: section 1.3. `0242 blob_get(void *dst, int size, int count)`; `0273 blob_put` (dead) is the
reverse copy. (V)

## 4 Scoring: `0000:3021 int score(unsigned limit)` (V, differential-tested)

Replays the event log from record 0 to `limit` (inclusive; stops earlier at a record with `type == 0`) and returns the 16-bit mission
score in AX (the high word is dropped by the callers; **(B)** a negative result prints as a negative number and is added to the career
score as an unsigned 16-bit value). It also recomputes the nine counters of 1.6 from zero on every call. Callers: `summary_screen`
(`limit = 0x100` = all), `map_screen` (0x100), `status_draw` (the index of the shown event).

### 4.1 Per-event rules

State: `S` (signed 32 bit, starts 0), `ng` (u16, starts 0), `end_seen` (starts 0), `war = pilot.3A`, `ws = 2*war`. For each record `r` (index `i`):
`t = r.type & 3F`, `pri = r.type & 80`, `sec = r.type & 40`, `a = (signed char) r.arg`, `o = a & 7F`, `site = 56EA + 16*o`, `cls = DS:6902[ byte site.+0E ]`.
Table names are in 4.2 (index by `war` unless written `[x]`).

| t | action |
|---|---|
| 1, 12 | `S -= TRACK[war]` if the site (indexed `16*a`, **not masked**) has flag `+9 & 10h` (refund of the track penalty). Then: `pri` -> `PRIMARY()`, `ground_kills++`; else `sec` -> `SECONDARY(primary_done)`, `ground_kills++`; else if `cls & 40` or `site.flags & 0500` -> `S += FRIENDLY_INST`, `friendly_inst++`; else if `site.flags & 1000` or `war != 0` -> `S += DEFENDED[war]`, `ground_kills++`; else (war 0, unflagged) `S += GROUND0[0]` (-5) and, only if that value is 0 (never), `mil_kills++` |
| 2 | `pri` -> `S += PRIMARY_VALUE[war]`, `primary_done = 1`, `ground_kills++` (no secondary correction, (B)); `sec` -> `S += SECONDARY_W[war]` (70/35/0 indexed by **war**, (B)), `secondary_done = 1`, `ground_kills++`; else `cls & 40` -> friendly installation; else `tile(r.x, r.y) & 3` != 0 -> friendly installation (destroyed something on friendly ground); else `cls & 80` -> `S += CIVILIAN[war]`, `civ_kills++`; else `S += KILLCLASS[war][cls & 0F]` and `(DD4[war] < 0) ? mil_kills++ : ground_kills++` |
| 3 | `S -= LAUNCH[war]` if `a & 80` (refund). Then `pri` -> `PRIMARY()`, `air_kills_a++`; `sec` -> `SECONDARY(primary_done)`, `air_kills_a++`; else `u = 179E + 32*o`, `radar = word u.+18`: `radar == FFFF` (no radar = friendly / civil) -> `S += FRIENDLY_AC`, `friendly_ac++`; else if `(a & 80) \|\| war != 0`: `radar == 0` -> `S += E02[war]`, else `S += (byte site(radar).+8 & 4) ? DB6[war] : DC2[war]` (**(B)** `radar` is used as a site index), `air_kills_a++`; else (war 0, unflagged) `S += DDA[0]` (-2), `air_kills_b++` |
| 6 | `S += LAUNCH[war]` (negative: -20 / -12 / 0) |
| 7 | `S += TRACK[war]` (negative: -10 / -6 / 0) |
| 8 | `if (S < 0) S = 0`; `if (i != 0) end_seen = 1` |
| 9 | `tile(r.x, r.y) & 3 == 0` (hostile cell): war 0: `S += 1`; war 1: `ng++`, `if (pilot.42 != 0 && (ng & 7) == 0) S += 1`, `if ((ng & 3) == 0) S += 1`; war 2: nothing |
| 10, 11 | `pri` -> `PRIMARY()`; else `sec` -> `SECONDARY(primary_done)`; (cargo and photo events score only as objectives) |
| 4, 5, > 12 | nothing |

`PRIMARY()`: `S += PRIMARY_VALUE[war]; primary_done = 1; if (secondary_done) S += E00 - DFE` (= -35: the secondary was worth 70 alone, 35 together).
`SECONDARY(k)`: `S += [DFE + 2k]` (70 if the primary is not done yet, 35 if it is); `secondary_done = 1`.
`tile(x, y) = DS:55EA[(y & F0) + (x >> 4)]` with `x, y` the record bytes.

### 4.2 Constants (DGROUP words, signed)

| name | DS | war 0 / 1 / 2 |
|---|---|---|
| PRIMARY_VALUE | DEC | 140 / 120 / 100 |
| SECONDARY first / second | DFE, E00 | 70, 35 (not war indexed); SECONDARY_W[war] = word DFE+2*war = 70 / 35 / 0 (reads E00 for war 1 and the next word for war 2) |
| TRACK | DF2 | -10 / -6 / 0 |
| LAUNCH | DE6 | -20 / -12 / 0 |
| DEFENDED | DF8 | 0 / 5 / 11 |
| GROUND0 | DE0 | -5 / -2 / 0 |
| CIVILIAN | DBC | -10 / -4 / +1 |
| FRIENDLY_INST | DCE | -25 / -25 / -25 |
| FRIENDLY_AC | DC8 | -15 / -15 / -15 |
| DDA | DDA | -2 / 0 / 0 (war-0 unflagged air kill) |
| DB6 / DC2 / E02 | | 0,6,12 / 0,3,6 / 0,1,3 (air kill values) |
| DD4 | DD4 | -5 / 4 / 4 (only the sign is used) |
| KILLCLASS | E08 | 3 rows of 16 words (row = war, column = `cls & 0F`): war 0 all 0; war 1: 2 6 1 1 3 8 2 2 5 3 1 9 4 0 0 0; war 2: 4 12 3 3 6 16 4 5 11 7 3 18 8 0 0 0 |

### 4.3 Final multipliers (all `S = trunc(S * m / 8)` in 32-bit signed arithmetic, in this order)

1. `m = DS:E68[pilot.38]` theatre 0..8 = 8, 10, 12, 13, 8, 8, 9, 8, 9.
2. `m = DS:E72[pilot.3C]` mission type = 8, 9, 8, 9.
3. `m = DS:E7A[pilot.3E]` opponent level = 8, 11, 14, 17.
4. `m = DS:E82[pilot.40]` landing realism = 5, 8, 10.
5. only if `end_seen && comm.26 == 2` (ejected): `m = DS:E88[ [6440] ]` = 2, 3, 6, 4.
6. by `comm.2E` (route cost): `7000 <= x < 9000` -> 9; `9000 <= x < 11500` -> 10; `x >= 11500` -> 11 (below 7000: none).

Return `S & FFFF`. The tests compare the AX result, the nine counters and every other DGROUP byte (3 000 random logs, 0 mismatches).
(U) which unit types the unit flag 4000h marks (probably "has fired at the player"); (U) site flags 0500 / 1000 meaning is START's (shell.md part C).


## 5 The summary screen: `0000:2BEC summary_screen` (V, differential-tested)

Shown right after the outcome scene and before the map. `ctx0` = `DS:6D8C` (draws on the screen), message buffer `DS:6986` (1 KB, starts empty).

1. `rating = score(0x100)` (as a signed 16-bit value; also recomputes the objective counters).
2. `screen_off()` (slot 69); `fa4("flag.pic")` into the file buffer; `pic_to_seg_b([7220], [55D0])` (decoded straight onto the screen page); `fda("flag.pal", 1F25)`;
   `42BE` = set all 256 DAC colours from `DS:1F25` (INT 10h AX=1012h); `screen_on()` (slot 70). No fade.
3. `obj = 2*primary_done + secondary_done`; `OBJ[obj]` = `DS:0DAE[2*obj]`: 0 = `""`, 1 = `"secondary objective "`, 2 = `"primary objective "`, 3 = `"primary and secondary objectives "`.
   `name` = display name `DS:7228` (e.g. `"Capt. Smith"`).
4. Paragraph 1 (`buf`), first matching case:

| condition | text (format at DS) |
|---|---|
| `comm.28 != 0 && comm.30 != 0` | `fmt(0817, name, t)` with `t = obj ? fmt(07D1, OBJ) : 07F5` |
| `comm.28 != 0` | `fmt(obj ? 0860 : 089B, name)` |
| `comm.26 != 1 && [6440] == 0` (captured) | `fmt(0964, name, t)`, `t = obj ? fmt(08CE, OBJ) : 0907` |
| `comm.26 == 2 && [6440] != 0` (rescued) | `obj ? fmt(09B3, name, OBJ) : fmt(09F1, name)` |
| `comm.26 == 3` | `obj ? fmt(0A44, name, OBJ) : fmt(0A71, name)` |
| otherwise | no text (B: uninitialised stack) |

5. `msg = buf + "\n\n"` (DS:0AB8). `t = comm.30 ? ", but will not be recorded as this was a training flight." (0ABB) : ""`;
   `msg += fmt(0AF5, rating, t)` ("The performance rating for this mission was %d%s.\n\n").
6. `air = air_kills_a + air_kills_b`: `air > 1 && air < 12` -> `fmt(0B29, air)`; `air >= 12` -> `fmt(0B4D, air)`; `air == 1` -> `"One enemy plane was shot down.\n\n"` (0BA1).
7. `gnd = ground_kills + mil_kills`: `gnd == 1` -> `0BC2` ("One enemy ground target was destroyed."), `gnd > 1` -> `fmt(0BEB, gnd)`.
8. Rules of engagement by `war = pilot.3A`:
   * war 0: `mil = air_kills_b + mil_kills`, `civ = civ_kills`; if `civ || mil`: `tmp = civ && mil ? fmt(0C1B, civ, mil) : mil ? fmt(0C44, mil) : fmt(0C5E, civ)`;
     `msg += fmt(0C6B, tmp) + ((civ + mil > 1) ? 0C98 : 0CB2)`.
   * war 1: if `civ`: `msg += fmt(0CCA, civ) + (civ > 1 ? 0D01 : 0D1B)` ((B) 0D1B lacks the final period).
   * war 2: nothing.
9. If `friendly_ac || friendly_inst`: `msg += 0D32`, then `0D77` if both, `0D93` if only aircraft, else `0D9D`.
10. `ctx0.font = 4`, `ctx0.fg = 0FDh`, `ctx0.bg = 0FEh`. Copy `ctx0` to a local, set its page to -1 (measure only) and call `text_wrap(local, msg, 0, 0, 260, 8, 0)`;
    `h = local.y` afterwards (the bottom of the measured text). Draw `text_wrap(ctx0, msg, 30, 184 - h, 260, 8, 1)`, i.e. centred lines, block bottom at y = 184.
11. `prompt_continue(160, 192)`; `snd(0)`.

(`%d` prints a signed 16-bit integer; texts are in Appendix A.1.)

## 6 The map debrief (route replay)

### 6.1 Screen build: `0000:02A4 map_screen_init` (V for the call order, (U) for the look)
1. `[5B9E] = 0`; `fade_out(1)` (15EF).
2. `load_pic("breficon.pic", page 2)` (event pictures), `load_pic("brefscrn.pic", page 3)` (frame), `load_pic(theatre_pic[pilot.38], page 4)` (the theatre map, names at DS:00CE, A.3),
   `fda("brefscrn.pal", 1F25)`.
3. `blit(3,0,0,0,0,0,320,200)`, `blit(3,0,0,1,0,0,320,200)` (frame on screen and working page); `fade_in(1)`; `screen_on()`; `wait_ticks(60)`.
4. `place_icons()` (6.2). Then `blit(4,0,0,1,0,0,320,169)`, `blit(4,0,0,0,0,0,320,169)` (map on page 1 and on the screen; the 31 rows below keep the frame).
5. `site_marks_draw(0)` (6.3). Then the button panel sprite `[5DB6]` at (229, 50) on ctx0 then ctx1 (`sprite_draw`, i.e. 86 x 20 panel with the two buttons).
6. `blit(1,0,0,4,0,0,320,169)` (page 4 := map with icons and panel = the clean background for the replay). `[5B9E] = 0`; `cursor_set_pos(272, 56)`.

### 6.2 `0000:05CA place_icons` (V, differential-tested)
For every site `i` in `0..site_count-1` (16-byte records at DS:56EA, `p`): *included(q)* = `!(q.+6 == 0 && !(q.flags & 1) && !(q.flags & 0200)) && !(q.+9 & 08)`.
If `p` is included: if `p.+6 != 0` clear flag bit 4000h (`p.+9 &= BF`). `j = site_count`; if `!(p.flags & 8) || (p.flags & 0400)`: `j` = index of the first included site
`q != p` with `q.name == p.name` (`q.+0`), or `site_count` if none. Then: `j == site_count` -> `x = p.x / 146`, `y = p.y / 195` (unsigned);
`j > i` -> the pair is drawn once at the midpoint `x = (q.x + p.x) / 292`, `y = (q.y + p.y) / 390` (16-bit sum); `j < i` -> nothing (the pair was added when `q` was the outer site).
Append entry `k = icon_count++`: `[54BA+2k] = x - 1`, `[5532+2k] = y - 2`, `byte [5440+2k] = i`, `byte [5441+2k] = (j < site_count) ? j : 0`.
(146 x 195 world units per map pixel; the same scale as START's briefing map.)

### 6.3 `0000:04D2 site_marks_draw(char pick)` (V, differential-tested)
For `k = 0 .. icon_count-1`: if `pick == 0` { `pick = wait_input(8)` (low byte); `snd(0x1C)` } (so the icons appear one every 8 ticks until **Space** skips the animation (S5.8 correction: `wait_input` returns 1 only for Space; Enter, Esc and button clicks play their click sound and return 0, so they do not skip); the tick sound plays
for every icon drawn while `pick` was still 0). `q = site byte[5440+2k]`, `(x, y) = ([54BA+2k], [5532+2k])`. If `byte[5441+2k] == 0`: one sprite `seg =
q.+6 != 0 ? (q.flags & 8 ? [7226] : [720C]) : [5DA0]` at (x, y) on ctx1 then ctx0 (`sprite_draw`, slot 22 at natural size). Else a pair: `[720C]` at (x-1, y) and `[5DA0]` at (x+2, y), ctx1 then ctx0 for each.

### 6.4 `0000:046E map_hover` void(void) -- hook of the menu (V)
`new = hit_test(DS:50B9, byte DS:50B8 (=2))`; `prev = [5B9E]`; if `prev != new`: `prev != 0` -> `pal_queue_push(0x100 - prev, DS:1324, 1)`; `new != 0` -> `pal_queue_push(0x100 - new, DS:131E, 1)`; `[5B9E] = new`.
`DS:1324` = (39h, 3Ch, 3Bh) light grey (normal), `DS:131E` = (3Fh, 32h, 2Bh) salmon (highlight): the two panel buttons are drawn in palette entries FFh and FEh.

### 6.5 `0000:03D9 map_screen` void(void) (V)
```
map_screen_init();  idx = replay_step(0);  done = 0;
do { r = menu_loop(menu 50B5);                // hook 046E, key 0, 2 rects: (228,47)-(317,59) and (228,60)-(317,70)
     if (r == 1) idx = replay_step(idx);     // "next"
     else if (r == 2 || r == 0x63) done = 1; // second button or Esc / right click
} while (!done);
sc = score(0x100);
if (comm.30 == 0) { pilot.30 = sc; if (pilot.2E < sc) pilot.2E = sc;            // unsigned compare
                    pilot.32 += sc;  /* 16 bit, carry into pilot.34 */ }
else pilot.30 = 0;
```
Any other `menu_loop` result (e.g. 100h + key) is ignored.

### 6.6 `0000:0714 replay_step(idx)` (V, differential-tested) -> next index
```
if (idx != 0) icon_erase();                            // 0E82: blit(2,0,160,1,X,Y,48,40); blit(2,0,160,0,X,Y,48,40)  with X,Y = [55AE],[55B0]
rec = 5E38 + 6*idx;
loop {
    t = rec.type & 3F;
    stop = !(t == 9 || (pilot.3A == 2 && (t == 6 || t == 7)));   // samples never stop; in war 2 neither do launches / tracks
    if (idx == 0) { [55AA] = map_x(rec.x); [55AC] = map_y(rec.y); }
    else if (t != 0) { line(ctx0, [55AA],[55AC], x, y, 0); line(ctx1, same); [55AA] = x; [55AC] = y; }   // x,y = map_x/map_y of the record
    status_draw(idx);  wait_ticks(8);
    if (stop) break;  rec += 6; idx++;
}
if (t != 0) { event_icon_draw(rec, idx); old = idx++; event_text_draw(rec, old); return idx; }
blit(4,0,0,0,0,0,320,169); blit(4,0,0,1,0,0,320,169);   // log finished: back to the clean map
status_draw(0);  return 0;
```
`map_x(v) = ((v << 7) & FFFF) / 146`, `map_y(v) = ((v << 7) & FFFF) / 195` (`0ECD`, `0EE0`; v = the record's x>>7 / y>>7 byte).
`gfx_line(ctx, x0, y0, x1, y1, colour)` (4206) clips to x 0..319, y 0..167 and draws colour index **0** ((U) check that index 0 is visible in `brefscrn.pal`). Pressing "next" after the end
restarts the replay at record 0.

### 6.7 `0000:0880 event_icon_draw(rec, idx)` (V)
Chooses an icon number 0..15 from `t`: 1: `site(arg&7F).flags_lo & 8 ? 15 : 0`; 2, 12: 2; 3: 1; 4: 6; 5: 3; 6: 5; 7: 4; 8: `idx == 0 ? 8 : comm.26 == 3 ? 7 : comm.26 == 1 ? 14 : [6440] == 0 ? 11 : 13`;
10: 10; 11: `template(mission slot (pri ? 0 : 1)).kind == 3 ? 9 : 12` (`kind` = word at `DS:1456 + 12*template + 4`, template = word `[71E8 + 18*slot + 8]`). (t = 9 is never drawn.)
Position: `X = map_x(rec.x); Y = map_y(rec.y)`; `X < 115 ? X += 10 : X -= 58`; `Y < 89 ? Y += 10 : Y -= 40` (the picture is placed on the far side of the event point); `[55AE] = X; [55B0] = Y`.
Blits: `blit(1,X,Y,2,0,160,48,40)` (save background), `blit(2,(icon%6)*48,(icon/6)*40,1,X,Y,48,40)` (picture from breficon.pic), `blit(1,X,Y,0,X,Y,48,40)` (show).
(U) what each of the 16 pictures shows (`work/pic/` has breficon.pic).

### 6.8 `0000:0A98 event_text_draw(rec, idx)` (V)
`o = arg & 7F`; `noun = pri ? "PRIMARY OBJECTIVE"(00E0) : sec ? "SECONDARY OBJECTIVE"(00F2) : names[ byte site(o).+0E ]` (index **not masked**, (B)); `place = site(o).+0`;
`name(i)` = string `names[i]` (pointer table DS:6DFA). Text by `t` (formats in A.1):

| t | text |
|---|---|
| 1, 12 | class `(DS:6902[site.+0E & 7F] & 0F) == 0D` -> `fmt(0106 "%s damaged", name(place))`; else `place != 0` -> `fmt(0111 "%s at %s destroyed", noun, name(place))`; else `fmt(0124 "%s destroyed", noun)` |
| 2 | pri -> `0141`, sec -> `015D`; else class of `o` (as an object type) == 0D -> `fmt(017B, name(o))`; else `fmt(0186, name(o))` |
| 3 | `fmt(0131 "%s %s shot down", unit(o).name, unit(o).nick)` (`unit(o)` = `DS:179E + 32*o`; the nickname starts with a blank, so two blanks appear) |
| 4 | `fmt(0242 "%s released", DS:1B98[2 * comm.38[2*o]])` |
| 5 | `fmt(0205 "Hit by %s missile", DS:1B3E[2*o])` |
| 6 | `fmt(0232 "%s %s Visual ID", unit.name, unit.nick)` |
| 7 | `place != 0` -> `fmt(0217 "%s %s Track", name(place), noun)`; else `fmt(0223 "%s Radar Track", noun)` |
| 8 | `idx == 0`: `fmt(024E "Takeoff point: %s", name(n))` with `n = site(slot0.+4).+0` or, if 0, the site's noun byte; `idx != 0`: `"Mission end: "(0260)` + by `comm.26`: 1 `"Crashed"`, 2 `comm.28 == 0 ? ([6440] != 0 ? "Good Bailout" : "Captured") : "Bailed & Died"`, 3 `"Good Landing"` |
| 10 | `place != 0` -> `fmt(01DF "%s at %s photographed", noun, name(place))`; else `fmt(01F5, noun)` |
| 11 | `"Cargo delivered"(0193)` + (pri `", PRIMARY OBJECTIVE achieved"`(01A3) \| sec `", SECONDARY OBJECTIVE achieved"`(01C0)) |

Display: `ctx1.fg = E2h`; `gfx_fill_rect(ctx1, 140,180, 238,195)`; `ctx1.font = 3, fg = 0Fh, bg = 8`; `text_wrap(ctx1, text, 140, 180, 106, 8, 1)`; `blit(1,140,180,0,140,180,110,16)`.

### 6.9 `0000:0DA3 status_draw(idx)` (V)
`ctx1.fg = E2h`; `gfx_fill_rect(ctx1, 75,180, 248,195)`; `ctx1.font = 3, fg = 0Fh, bg = 8`; `text_shadow(ctx1, fmt("TIME: %s", fmt_time(rec[idx].tick)), 78, 180)`;
`text_shadow(ctx1, fmt("SCORE:  %d", score(idx)), 78, 188)` (the running score counts events 0..idx); if `idx != 0` `text_shadow_centered(ctx1, "In Flight", 195, 184)`;
`blit(1,75,180,0,75,180,174,17)`.

### 6.10 `0000:0EF3 fmt_time(unsigned ticks, char *buf)` (V, differential-tested) -> buf
`ticks += ((word[71F0] + word[7202]) & 0F) << 8` (the two words are the template indices of the mission slots 0 and 1, `DS:71E8 + 8` and `DS:71E8 + 26`; 16-bit wrap).
`buf = "00:00:00"`; `buf[0] += comm.33` (night); `h = ticks / 1800`; `buf[1] += h % 10` (the tens digit is never advanced, (B)); `buf[1] += night == 0 ? 6 : 8`; if `buf[1] > '9'` { `buf[1] -= 10; buf[0]++` };
`m = (ticks / 30) % 60` -> `buf[3] += m/10; buf[4] += m%10`; `s = ((ticks << 1) & FFFF) % 60` -> `buf[6] += s/10; buf[7] += s%10`. So the clock starts at 06:00:00 (day) or 18:00:00 (night) plus a template dependent offset of up to 15 * 256 ticks.

## 7 Awards

### 7.1 `0000:2265 awards_screen` void(void) (V, differential-tested)
```
if (comm.30 != 0 && !primary_done && !secondary_done) return;      // training without an objective: no awards screen
clear [55E2] [55DD] [643A] [55E7] [55DA] [55E6];
show_screen_pic("awards", callback 2200);         // 1901
ctx0.fg = 0FDh; ctx0.bg = 0FEh;
promotion_check();  award_check();
if (medal || crr_ribbon || ribbon || promoted) snd(0x24);
promo_text();  medal_text();  snd(0);
```
Callback `2200` (V): `blit(0,0,0,1,0,0,320,200)`, `blit(0,0,0,4,0,0,320,200)` (the screen into pages 1 and 4), `load_pic("medals.pic", page 3)`, ribbons strip (7.4), `blit(1,0,0,0,0,0,320,200)`.

### 7.2 `0000:2AFA promotion_check` (V, differential-tested)
Skipped when `comm.30 != 0`. Tables (words, index = candidate rank `i`): `DS:05B0` career score {300, 1125, 3000, 7000, 16000, 27720}, `DS:05BC` average score per mission {100,150,200,250,280,280},
`DS:05C8` missions {2,5,10,20,40,99}.
```
for (i = 5; pilot.20 <= i; i--)                      // signed compare; i counts down to the current rank
    if (career >= SCORE[i] && missions >= MISSIONS[i] && career / missions >= AVG[i]) {     // dword career = +32/+34, signed 32-bit divide
        if (pilot.20 == 5 && missions == 99) { promoted = 1; pilot.20++; }      // Colonel -> Brigadier General only on the 99th mission
        else if (pilot.20 >= 5) { }                                             // Colonel and not mission 99: nothing
        else if ([6982] == 1) promo_withheld = 1;                               // captured: rank unchanged
        else { promoted = 1; pilot.20++; }
        return;
    }
```
The loop never promotes by more than one rank. (The thresholds increase monotonically, so testing `i = rank` alone gives the same result.)

### 7.3 `0000:2476 award_check` (V, differential-tested)
```
if (pilot.44 == 0 && comm.30 != 0 && (primary_done || secondary_done)) { pilot.44 = 1; crr_ribbon = 1; }
if (comm.30 == 0) {                                   // ribbons by missions flown n = pilot.36 (already incremented)
    if (pilot.46 == 0 && 5 <= n && n < 10)  { pilot.46 = 1; ribbon = 1; }
    if (pilot.48 == 0 && n >= 10)           { pilot.46 = 0; pilot.48 = 1; ribbon = 2; }
    if (pilot.4A == 0 && 30 <= n && n < 60) { pilot.4A = 1; ribbon = 3; }
    if (pilot.4C == 0 && n >= 60)           { pilot.4A = 0; pilot.4C = 1; ribbon = 4; }
}
if (pilot.22 == 0 && comm.36 >= 7 && (comm.34 & 8) && (comm.34 & 4)) { pilot.22 = 1; medal = 1; }     // Purple Heart: 7+ hits with navigation and flight controls damaged
last = pilot.30;  ghost = (captured != 0 || comm.30 != 0);
if (pilot.2C == 0 && last > 1200) { if (ghost) medal_ghost = 6; else { medal = 6; pilot.2C = 1; } return; }
if ((pilot.2A + 1) * 900 <= last)  → medal 5 (AFC), counter pilot.2A++     // each: if (ghost) medal_ghost = N else { medal = N; counter++ }; return
if ((pilot.28 + 1) * 600 <= last)  → medal 4 (SS),  counter pilot.28++
if ((pilot.26 + 1) * 300 <= last)  → medal 3 (DFC), counter pilot.26++
if (pilot.24 < 9 && (pilot.24 + 1) * 100 <= last) → medal 2 (AM), counter pilot.24++
```
At most one score medal per mission (a Purple Heart can come in addition, but see 7.5). The products are 16-bit. Medals are awarded for dead pilots too (the "posthumously" wording); `ghost` covers captured pilots
and training flights (in a training flight `pilot.30 = 0`, so a ghost medal cannot occur there; only the captured case produces them).

### 7.4 Ribbon strip: `0000:22EB ribbons_draw`, `0000:23D6 ribbon_draw(id, j, n)` (V)
`22EB`: ids of up to 12 ribbons in a list: for `k = 0..5` if `word[pilot+22+2k] != 0` add `k`; for `k = 0..4` if `word[pilot+44+2k] != 0` add `6+k`; then add `11, 12, 13` while fewer than 12 are listed (the three always-shown ribbons).
For each position `j` of `n` ids: `q = n/3, if n%3 == 0 q--`; `base = (3*q > j) ? 130 : {0: 130, 1: 141, 2: 136}[n%3]`; `dx = base + (j%3)*11`; `dy = 149 + 2*(j/3 - q)`;
`blit(3, 260, 2*id, 1, dx, dy, 11, 2)`. (Rows of three 11 x 2 ribbon bars, the last row centred, stacked upwards from y = 149.)

### 7.5 Texts: `2A01 promo_text`, `2703 medal_text`, `26A9 show_award_text`, `28ED rank_picture` (V)
`26A9 show_award_text(s)`: `blit(1,152,19,0,152,19,158,52)` (restore the text area from page 1); `ctx0.font = 4`; `text_wrap(ctx0, s, 152, 19, 158, 8, 1)`; `prompt_continue(231, 63)`.
`2A01 promo_text` (`name` = DS:7228):
```
shown = 0
if (promoted) { pre = comm.28 ? "posthumously " : ""; buf = fmt(DS[4F0F], name, pre, RANKNAME[pilot.20]); rank_picture(); shown = 1; }
else if (promo_withheld) { buf = fmt(DS[4F0D], name, NEXTRANK[pilot.20]); shown = 1; }
else if (!(medal || crr_ribbon || ribbon || medal_ghost)) { buf = fmt("%s remained at his present rank.", name); shown = 1; }   // 0712
if (shown) show_award_text(buf);
if (pilot.36 == 99 && comm.28 == 0) show_award_text(DS[4F11]);     // retirement text
if (promoted && medal == 1) medal = 0;                             // (B) a Purple Heart earned together with a promotion is counted but never shown
```
`rank_picture()` runs before the text is shown. `RANKNAME[r]` = `DS:05F2 + 2r` (the long name of the new rank), `NEXTRANK[r]` = `DS:05F4 + 2r`.
`28ED rank_picture`: `blit(3,180,0,1,219,79,80,112)` (empty panel), then by the new rank `r`: `r in {1,2,3,4,6}`: `sy = (r-1)*29`, `blit(3,286,sy,1,243,98,34,29)` and `blit(3,286,sy,1,243,141,34,29)` (a pair of shoulder boards);
`r == 5` (Colonel): `blit(3,281,116,1,241,98,39,29)` and `blit(3,242,116,1,241,141,39,29)`; finally `blit(1,219,79,0,219,79,80,112)`.
`2703 medal_text`: `blit(4,219,79,0,219,79,80,112)` and `blit(4,219,79,1,219,79,80,112)` (clear the picture area), then
* `medal != 0` (m = 1..6): `pre = comm.28 ? "posthumously " : ""`; text `fmt("%s was %sawarded the %s.", name, pre, MEDALTEXT[m])` (DS:05D2, long citation); `blit(3,180,0,1,219,79,80,112)`;
  medal picture `blit(3, ((m-1)%3)*60, ((m-1)/3)*100, 1, 231, 87, 60, 96)`; `blit(1,219,79,0,219,79,80,112)`; `show_award_text(text)`.
* `crr_ribbon`: `show_award_text(fmt(DS:0647, name))` ("... awarded the Combat Readiness Ribbon for successfully completing this training mission.").
* `ribbon != 0`: format `(medal || crr_ribbon) ? 06A4 ("%s is also awarded the %s for flying %d missions.") : 06D6`, args `name, RIBBONNAME[ribbon] (DS:05EA), pilot.36`; `show_award_text`.
* `medal_ghost != 0`: `fmt(comm.30 ? DS[4F09] : DS[4F0B], MEDALSHORT[ghost] (DS:05DE + 2*ghost))` ("If this had been a real mission you'd be getting the %s." / "Because your capture exposed our stealth technology, the squadron commander will not recommend you for the %s.").
  (B) `MEDALSHORT[1]` (Purple Heart) is an empty string, but a ghost Purple Heart cannot occur.

Medal sheet geometry (page 3 = `medals.pic`): medal pictures 60 x 96 in a 3 x 2 grid (columns 60 px apart, rows 100 px apart), the empty panel at (180, 0) 80 x 112, ribbons at x = 260, rank insignia at (286, 0..) and (281/242, 116).

## 8 Ending scenes

All scenes use `show_screen_pic(name, callback)` (`1901`, section 10) and then `prompt_continue(x, y)` (`206A`: font 3, shadowed "Press Selector to continue" centred at (x, y), then `wait_input(0xFFFF)`).

| scene | function | picture | callback | sound | prompt (x, y) |
|---|---|---|---|---|---|
| crashed | 370A | cemetary | 3682 | snd 0x25 | 235, 172 |
| rescued | 3B9A | rescue | 3B41 | snd 0x26 | 250, 192 |
| captured | 3BBF | tass | none | snd 0x26, fg 0F, bg 0 | 160, 192 |
| landed | 3887 | landed1 | 372F | snd 0x27, animation | 160, 172 |
| ending | 39D0 | desk / endhangr / flight | 398F or none | none | per screen |

* `3682 crash_cb` (callback, V): `font 4, fg FDh, bg FEh`; `text_wrap(ctx0, crash_text[comm.28], 175, 40, 120, 10, 1)`; texts at DS:0E90 (1), 0EC2 (2), 0EF1 (3), 0F24 (4), 0F68 (5), 0FA6 (6). Other codes: (B) uninitialised pointer.
* `3B41 rescue_cb` (V): the text of DS:105A + DS:1096 ("Your bail-out was successful. An SAR aircraft homes on your beacon and lifts you to safety."), font 4, fg FDh, bg FEh, `text_wrap(ctx0, s, 200, 109, 100, 8, 1)`.
* `3BBF captured_screen` sets `[6982] = 1` first (so it is set even though the picture comes later).
* `3887 landed_screen` (V): after the picture: `snd(0x27)`; `wait_ticks(120)`; `blit(2,0,0,0,51,44,210,112)`; `wait_ticks(12)`; `blit(3,0,0,0,51,30,212,96)`; `wait_ticks(12)`; `blit(4,0,0,0,52,4,212,108)`; `wait_ticks(12)`;
  four frames `i = 0..3`: `blit(4,214,16*i,0,128,126,38,16)` + `wait_ticks(12)` (a blinking light); `ctx0.fg = 0Fh, bg = 0`; prompt; `snd(0)`.
  Callback `372F landed_cb` (V): `load_pic("landed2.pic", 2)`, `("landed3.pic", 3)`, `("landed4.pic", 4)`; then the sky ramp of the palette buffer (entries 192..223 = `DS:2165`, 96 bytes): find the first event record with
  `type & 3F == 8` (`i = 0..FF`); `d = rec[i].tick - rec[0].tick` clamped to 1800; if `d == 0` copy the 96 bytes `DS:1396` (dusk ramp) over `DS:2165`; else for each of the 32 entries and channel c:
  `DS:2165[3k+c] = trunc((target[c] - DS:1396[3k+c]) * 1800 / d)` (byte), `target` = `DS:13F6` if night else `DS:1336` (the first colour of that table only). (B) The search finds the *start* record, so `d` is always 0 and the dusk ramp is
  always used; the `d != 0` branch is dead in practice.
* `39D0 ending_scene` (V, differential-tested): only when `comm.28 == 0 && comm.30 == 0`. Sets `font 4, fg FDh, bg FEh`, picks (table, count, screen index, callback):

| condition (first match) | table / count | screen (DS:1036[i]) |
|---|---|---|
| no promotion, no medal, no objective done, `pilot.36 != 99` | 48E7 / 8 | 0 `desk`, cb 398F |
| (primary or secondary done) and `[6440] == 0` and no promotion, no medal | 4510 / 7 | 0 `desk`, cb 398F |
| `pilot.36 == 99 && pilot.20 == 6` | 4A4A / 4 | 2 `flight` |
| `pilot.36 == 99` | 4E53 / 4 | 2 `flight` |
| promoted or medal | 4C32 / 5 | 1 `endhangr` |
| `comm.2C < 3` | 4AEB / 5 | 1 `endhangr` |
| otherwise | 464E / 6 | 1 `endhangr` |

  `show_screen_pic(name, cb)`; `s = table[rand_n(count)]`; `text_wrap(ctx0, s, X[i], Y[i], W[i], 8, 1)` with X = {20, 50, 56} (`DS:103C`), Y = {20, 18, 12} (`DS:1042`), W = {170, 220, 213} (`DS:1048`);
  `prompt_continue(PX[i], PY[i])` with PX = {95, 160, 135} (`DS:104E`), PY = {80, 50, 45} (`DS:1054`). The quips are in A.2.
  `398F desk_cb`: `load_pic("requestr.pic", 2)`; `blit(2, 241 + 8*pilot.20, 156, 0, 275, 111, 8, 7)` (the rank insignia on the desk).


## 9 What END hands back

* **Pilot record** (`comm+1272h`, the only thing END writes): section 1.5. START's next run (`roster_startup_sync`, shell.md 1.7) copies the active record into `roster[active.idx]` (`0000:04F2 == 0` after the first
  run) and saves `ROSTER.FIL`; a non-zero `pilot.4E` (retired / KIA) makes START open the roster screen so the player picks or creates another pilot.
* **comm block**: not written. `comm.26/28/30/34/36/74/76` stay as VGAME left them; START rewrites `26 = 3, 28 = 0, 2C = 0, 30 = training` before the next mission.
* **Files**: none written (the file writer `10C1` / `4199` is dead code); `TEMP.WLD` is not read (the world comes through the comm blob). Read: `*.pic`/`*.pal` (Appendix A.1), `medals.pic`.
* **Exit code** 23h from `main` (`F117.COM` then starts `START` again); 0 (Ctrl-Break, or Alt-Q + `Y`) ends the whole chain.
* **Sound** commands (sound.md): 0x1A click (Enter / button 0 / Space), 0x23 Esc / button 1, 0x1C icon tick, 0x24 (36) awards fanfare, 0x25 (37) crash, 0x26 (38) rescue / captured, 0x27 (39) landing tune, 0 stop.

## 10 UI toolkit and RTL-level helpers (map to START, shell.md Part B)

These END functions have the **same algorithm** as the START function listed (compared instruction by instruction with `port/notes/s39/cmpfn.py END_ADDR START_ADDR`: only register choices and operand order differ unless noted).
Data addresses differ: START `E088/E08A/E08C/E08E/E090/E092/E093` (cursor) = END `7212/7214/7216/7218/721A/721C/721D`; START palette buffer `64E3` = END `1F25`; START contexts `DC02/DC18` = END `6D8C/6DA2`.

| END | START | name | note |
|---|---|---|---|
| 0FA4 | 2864 | `load_file_far(name, off, seg)` | open, read up to FFFFh bytes, close; returns bytes read |
| 0FDA | 289A | `load_file_near(name, buf)` | |
| 100D | - | `load_pic(name, page)` | `fh = open(name); pic_to_page(fh, page); close(fh)` (4648; reads through the 512-byte reader 4160) |
| 1039, 1061, 1089, 10C1 | - | dead: PIC-to-segment from a file, the same after `lseek`, file writer | never called |
| 10FA | 2A84 | `text_centered(ctx, s, cx, y)` | `ctx.x = cx - text_width/2; ctx.y = y;` slot 5 |
| 112A | (36C4) | `text_shadow_centered(ctx, s, cx, y)` | as START 36C4 `ui_text_shadow` but centred: if `ctx.bg != -1` draw in `ctx.bg` at (cx+1,y), (cx,y+1), (cx+1,y+1), then normally |
| 11A2 | 2B06 | `wait_ticks(n)` | `n` x `pal_apply_queue()` |
| 11B7, 1332, 1470, 14CE, 1755 | 2B1B, 2CB4, 2DF2, 2F4C, 31FA | popup text, frame, button, route line (no skip / return), map label | **dead** in END; 11B7 saves its background at page 2 (0, h+4) |
| 15D7 / 15EF | 3070 / 3088 | `fade_in(speed)` / `fade_out(speed)` | `pal_fade(1F25, 0, 100h, speed)` / `(1F25, 100h, 0, speed)` |
| 1607 | 30A0 | `pal_fade(pal, from, to, speed)` | quadratic brightness fade; stops the timer ISR during the fade; END does not wait for the palette queue first |
| 1708 | 31AD | `pal_fade_calibrate(buf)` | counts `pal_set` calls in 4 BIOS ticks into `[55BA]` |
| 185C | 3517 | `pal_queue_push(first, rgb, count)` | queue at DS:1EE8, count byte DS:1F24 (platform.md) |
| 188D | 3567 | `ui_text(ctx, s, x, y)` | |
| 1992 | 3693 | `sprite_draw(ctx, x, y, seg)` | slot 22 at natural size (words 0 and 2 of the sprite) |
| 19C3 | 36C4 | `text_shadow(ctx, s, x, y)` | |
| 1A3B / 1A75 | 373C / 3776 | `text_width(ctx, s)` / `text_wrap(ctx, s, x, y, width, line_h, centre)` | page -1 = measure only; returns the widest line |
| 1BAA | 38AC | `menu_loop(MenuDesc *)` | returns button 1..n, 63h = Esc / right button, 100h + key for a hot key |
| 1C3B | 393D | `hit_test(rects, n)` | 1-based hit or 0 |
| 1CB1, 1CD3 | 39B3, 39D5 | rectangle order helpers | dead |
| 1CF5 | 39F7 | `poll_input(MenuDesc *)` | joystick / mouse / keyboard + cursor drawing; **Alt-Q calls `quit_prompt(0)`** |
| 1FE5 | 3E20 | `cursor_init(sprite_seg)` | `[7212] = seg; [721C] = byte[seg:0]; [721D] = byte[seg:2]; [721E] = [721F] = 2; [7214] = [7216] = [7218] = [721A] = 0` |
| 2023 | 3E5E | `check_break()` | `if ([2608]) end_exit(0)` |
| 2032 | 3E6D | `kbd_flush()` | dead |
| 2049 | 3E84 | `cursor_set_pos(x, y)` | `[7214] = x; [7216] = y;` if `comm.72 & 2` `mouse_set_pos()` |
| 18AE, 1C87 | 3588, 3989 | popup restore, cursor erase | unreferenced code between listed functions |

PIC decoder and DAC helpers (video.md section 9): `4648 pic_to_page(fh, page)`, `46BA pic_to_seg_a`, `4722 pic_to_seg_b(buf_seg, dst_seg)` (the only one END uses for screens: decode from the file image
in memory, 200 rows of 320), `4796 pic_to_seg_c`, `47FF pic_row`, `4829 pic_begin`, `4874 lzw_reset`, `48AA rle90_fill`, `4923 lzw_next` (= START `86A8..8983`; `47FE` is a `ret`), `49D2 gfx_fill_rect(win, x0, y0, x1, y1)` (= START 8A32:
fills the rectangle with `win.fg`), `4A40 edge_table_clear`, `4A71 gfx_fill_rect_bg` (same, colour `win.bg`; dead). `42BE pal_set_default`, `4393 pal_apply_queue`, `42D0 pal_set` are in platform.md.

### 0000:1901 `show_screen_pic(const char *name, void (*cb)(void))` (V)
```
sprintf(f, "%s.pic", name);  fade_out(1);
load_file_far(f, [6442], [6444]);              // whole file into the 35 000-byte buffer
pic_to_seg_b([7220], [55D2]);                  // decode into page 1
sprintf(f, "%s.pal", name);  load_file_near(f, 0x1F25);      // 768 byte palette into the palette buffer
blit(1,0,0,0,0,0,320,200);                     // page 1 -> screen (the palette is still black)
if (cb) cb();
fade_in(1);  screen_on();
```
Used by every scene and the awards screen.

### 0000:206A `prompt_continue(int cx, int y)` (V)
`ctx0.font = 3`; `text_shadow_centered(ctx0, "Press Selector to continue", cx, y)`; `wait_input(0xFFFF)`.

### 0000:2092 `wait_input(unsigned ticks)` (V, differential-tested) -> 0 or 1
Joystick (`comm.72 & 1`, slot 93 `joy_button(0/1)`), mouse (`comm.72 & 2`, `mouse_poll()` 44B9 + `mouse_button(0/1)` 44A4), keyboard (slots 90 `kbd_empty`: 0 = key waiting, 91 `kbd_get`).
`ticks == 0xFFFF` never counts down (wait forever); `ticks` is decremented once per `pal_apply_queue()` (each ~one video frame).
```
if (ticks != 0) do { pal_apply_queue(); if (ticks != FFFF) ticks--; pressed = joy0||joy1||mouse0||mouse1; } while (pressed && ticks != 0);   // wait for release
r = 0; first = 1;
for (;;) {
    if (!first) { if (r) break; pal_apply_queue(); if (ticks != FFFF) ticks--;
                  if (joy) { if (joy0) r = 1; if (joy1) r = 2; }  if (mouse) { mouse_poll(); if (mouse0) r = 1; if (mouse1) r = 2; } }
    first = 0;
    if (key waiting) { k = kbd_get(); if (k == 0x1000) quit_prompt(1); k &= FF; if (k == 0D) r = 1; if (k == 1B) r = 2; if (k == 20) r = 3; }
    if (ticks == 0) break;
}
switch (r) { 0: return 0; 1: snd(1A); return 0; 2: snd(23); return 0; 3: snd(1A); return 1; }
```
(B) `ticks == 0` on entry polls the keyboard exactly once (START's version treats 0 as 65 536). Only callers: `site_marks_draw` (8) and `prompt_continue` (FFFF).

## 11 Original bugs and quirks to keep

1. `end_init` and `init_pages` both allocate the 35 000-byte file buffer; the first block is leaked (harmless).
2. `score`: the event types 4 and 5 and the 6D88 branch of event 1 (`GROUND0 == 0`) never score; `PRIMARY_VALUE` is applied without the secondary correction for event 2; the secondary value of event 2 is indexed by war level, all others by "primary done";
   `radar` of a unit type is used as a site index; for events 1, 12 the site index is the sign-extended byte (not masked with 7F); the result is a 16-bit value that can be negative and is then added to the career score as unsigned.
3. `event_text_draw`: the noun name index is the full byte `site.+0E` (no `& 7F`); the nickname strings start with a blank (`"MIG-23  Flogger"`).
4. `fmt_time`: the hour tens digit is never advanced; the clock base is 06:00 (day) / 18:00 (night) plus a mission-template dependent offset; hours do not wrap at 24.
5. `promo_text`: a Purple Heart earned together with a promotion is stored in the record (`pilot.22 = 1`) but its line is suppressed.
6. `landed_cb`: the "first event of type 8" is the start record, so the time-dependent sky fade never runs; the dusk ramp `DS:1396` is always copied into the palette.
7. `summary_screen`: with `comm.28 == 0 && comm.26 == 1` the paragraph is uninitialised stack text; `0D1B` ("target was destroyed") lacks the final period.
8. `crash_cb`: crash codes 0 and > 6 print an uninitialised pointer; codes 3 and 4 have texts but VGAME never raises them.
9. `poll_input` calls `quit_prompt(0)`, which does not save the screen (page 1 is shown again when the prompt is cancelled).
10. The unused sprites `[5B9C]`, `[6D86]` and the dead functions of section 10 can be dropped in the port.

## 12 Tests and port notes

* `port/notes/s39/`: `s39_ref.py` (numbers: score, promotion, awards, clock, landing class, icon placement), `s39_ref_ui.py` (screens as lists of UI operations, all texts), `s39_emu.py` (Unicorn harness: END image, DGROUP at 05FA,
  comm block at 3000:0, driver stubs `retf`), `s39_test.py`, `s39_test_ui.py`. `python port/notes/s39/s39_test.py 1000` and `python port/notes/s39/s39_test_ui.py 300` (about 30 s). `cmpfn.py` = the START/END function diff used for section 10.
  Result (S3.9): numeric 6 x 1000 (time, landing class, score, promotion, awards, icons) + screens 14 scenarios x 300 (wait_input, site marks, hover, init names, event icon + text, status, replay step, summary text,
  awards screen, ending quips, scenes, callbacks 3682/3B41/372F/2200, main flow, map screen tail) = **0 mismatches**. The harness stubs file loading, drawing, waiting and sound and records them as ops; the real arithmetic, `sprintf`
  and `rand` run natively. Not run natively: `02A4` (call order read from the disassembly), `3E61`, `1CF5`, `1BAA`, the toolkit copies.
* Port layout: `debrief.c` with `debrief_main()`; `score.c` (section 4 verbatim, tables as `static const int16_t`); text assembly may use `snprintf` but keep the buffers' content byte-exact (the tests compare strings);
  `end_ui` = the shared START toolkit of the port (S5.x). Event log and pilot record are the shared structures of the comm block (HANDOFFS.md, S3.10).
* The port reads the pictures `*.PIC/.PAL` and `MEDALS.PIC` from the game directory like the original; no new files are written.

### Port notes (S5.8, `f117port/src/debrief`)

* Layout: `score.c` (3021, 2AFA, 2476, 05CA, 0EF3, 00A1, 0ECD/0EE0), `end_ui.c` (the toolkit copies with END's DGROUP offsets: cursor 7212..721F, poll_input, menu_loop, wait_input, prompt_continue, the palette queue 1EE8/1F24, `pal_fade` with the calibration byte 55BA, `show_screen_pic`, `quit_prompt`, `end_exit`), `end_map.c` (section 6), `end_awards.c` (7), `end_screens.c` (5, 8), `end_main.c` (start-up, blob, main), `endtest.c`.
* Reused from `src/shell` (state-free): `ui_text`, `ui_text_width`, `ui_text_wrap`, `ui_text_shadow`, `ui_text_centered`, `ui_sprite_draw`, `ui_wait_ticks`, `ui_load_file_far/near` (the debrief_symbols.csv rows carry the shell names). The other toolkit twins keep END's offsets, so they are END's own C (`end_pal_fade`, `end_pal_queue_push`, `end_poll_input`, `menu_loop`, `hit_test`, ...): the START versions read START's DGROUP.
* PORT deviations: the first 35000-byte allocation of `end_init` is not made (it leaked); the fades do not stop and restart the timer ISR; calibration is a constant 15 `pal_set` per speed unit (as START); `crash_cb` draws nothing for the codes 0 and > 6 and the summary builds no paragraph for `26h = 1, 28h = 0` (the original prints uninitialised stack text there).
* Test (`ctest debrief`, `port/notes/s58/`): the original END under Unicorn on eight comm blocks (the `shell` test's mission with VGAME's result fields, an event log and a pilot record): landed (night, war 1, kills, both objectives, the short tour ribbon), ejected and rescued, ejected and captured (war 2, ghost medal), crashed (KIA), promoted with a Purple Heart that is not shown, training with the Combat Readiness Ribbon, the 99th mission lost (KIA) and won (Brigadier General, retirement, the career-score carry). Screen and DAC CRC at every `wait_input` and every 3rd menu poll (about 50 per scenario), the pilot record in the comm block byte for byte, exit 23h: 0 differences.

### Open items
1. (U) Visual check in DOSBox-X (PLAN.md `U-debrief`): colour index 0 route line, the 16 event pictures of `breficon.pic`, clock start, awards strip.
2. (U) meaning of unit flag 4000h (event 3 arg bit 7), of the object-class bits (7 civilian, 6 friendly) and of site flags 0100/0400/1000 beyond shell.md.
3. (U) The screen looks of `02A4` (coordinates are from the disassembly, not run natively in the test).


## Appendix A: text constants and tables (generated from the END image)

### A.1 Text constants (DS offset, content; a backslash followed by n = newline, `%s`/`%d` printf conversions)

| DS | text |
|---|---|
| 0042 | `libya.pic` |
| 004c | `persian.pic` |
| 0058 | `n_cape.pic` |
| 0063 | `c_europe.pic` |
| 0070 | `vietnam.pic` |
| 007c | `mid_east.pic` |
| 0089 | `kuwait.pic` |
| 0094 | `cuba.pic` |
| 009d | `korea.pic` |
| 00a7 | `breficon.pic` |
| 00b4 | `brefscrn.pic` |
| 00c1 | `brefscrn.pal` |
| 00ce | `B` |
| 00d0 | `L` |
| 00d2 | `X` |
| 00d4 | `c` |
| 00d6 | `p` |
| 00d8 | `\|` |
| 00da | `` |
| 00dc | `` |
| 00de | `` |
| 00e0 | `PRIMARY OBJECTIVE` |
| 00f2 | `SECONDARY OBJECTIVE` |
| 0106 | `%s damaged` |
| 0111 | `%s at %s destroyed` |
| 0124 | `%s destroyed` |
| 0131 | `%s %s shot down` |
| 0141 | `PRIMARY OBJECTIVE destroyed` |
| 015d | `SECONDARY OBJECTIVE destroyed` |
| 017b | `%s damaged` |
| 0186 | `%s destroyed` |
| 0193 | `Cargo delivered` |
| 01a3 | `, PRIMARY OBJECTIVE achieved` |
| 01c0 | `, SECONDARY OBJECTIVE achieved` |
| 01df | `%s at %s photographed` |
| 01f5 | `%s photographed` |
| 0205 | `Hit by %s missile` |
| 0217 | `%s %s Track` |
| 0223 | `%s Radar Track` |
| 0232 | `%s %s Visual ID` |
| 0242 | `%s released` |
| 024e | `Takeoff point: %s` |
| 0260 | `Mission end: ` |
| 026e | `Crashed` |
| 0276 | `Good Bailout` |
| 0283 | `Captured` |
| 028c | `Bailed & Died` |
| 029a | `Good Landing` |
| 02a7 | `TIME: %s` |
| 02b0 | `SCORE:  %d` |
| 02bb | `In Flight` |
| 02c5 | `00:00:00` |
| 02ce | `%s.pic` |
| 02d5 | `%s.pal` |
| 02dc | `Press Selector to continue` |
| 02f7 | `` |
| 02f8 | `PURPLE HEART for being wounded in combat operations` |
| 032c | `AIRMAN's MEDAL for exceptional mission performance` |
| 035f | `DISTINGUISHED FLYING CROSS for valor in the line of duty` |
| 0398 | `SILVER STAR for heroism while flying a combat mission` |
| 03ce | `AIR FORCE CROSS for valor and heroism on a combat mission` |
| 0408 | `CONGRESSIONAL MEDAL OF HONOR, America's highest military honor, for heroism above and beyond the call of duty` |
| 0476 | `` |
| 0477 | `Airman's Medal` |
| 0486 | `Distinguished Flying Cross` |
| 04a1 | `Silver Star` |
| 04ad | `Air Force Cross` |
| 04bd | `Congressional Medal of Honor` |
| 04da | `OverSeas Short Tour Ribbon` |
| 04f5 | `OverSeas Long Tour Ribbon` |
| 050f | `Longevity Service Ribbon` |
| 0528 | `Longevity Service Ribbon with Gold Cluster` |
| 0553 | `First Lieutenant` |
| 0564 | `Captain` |
| 056c | `Major` |
| 0572 | `Lieutenant Colonel` |
| 0585 | `full Colonel` |
| 0592 | `Brigadier General` |
| 05a4 | `medals.pic` |
| 05af | `` |
| 05b0 | `,e¸X>Hld` |
| 05be | `` |
| 05c0 | `È` |
| 05c2 | `ú` |
| 05c4 | `` |
| 05ca | `` |
| 05cc | `\n` |
| 05ce | `` |
| 05d0 | `(` |
| 05d2 | `c` |
| 05d4 | `ø,_Îvw¡­½Úõ(Sdlr` |
| 0601 | `` |
| 0602 | `` |
| 0603 | `` |
| 0604 | `` |
| 0605 | `` |
| 0606 | `` |
| 0607 | `` |
| 0608 | `` |
| 0609 | `` |
| 060a | `` |
| 060b | `` |
| 060c | `` |
| 060d | `` |
| 060e | `` |
| 060f | `` |
| 0610 | `` |
| 0611 | `` |
| 0612 | `` |
| 0613 | `` |
| 0614 | `` |
| 0615 | `` |
| 0616 | `` |
| 0617 | `` |
| 0618 | `awards` |
| 061f | `posthumously ` |
| 062d | `` |
| 062e | `%s was %sawarded the %s.` |
| 0647 | `%s is awarded the Combat Readiness Ribbon for successfully completing this training mission.` |
| 06a4 | `%s is also awarded the %s for flying %d missions.` |
| 06d6 | `%s is awarded the %s for flying %d missions.` |
| 0703 | `` |
| 0704 | `posthumously ` |
| 0712 | `%s remained at his present rank.` |
| 0733 | `` |
| 0734 | `Insufficient system memory - AllocBuffer$` |
| 075e | `Buffer dealloc error$` |
| 0774 | `` |
| 0775 | `secondary objective ` |
| 078a | `primary objective ` |
| 079d | `primary and secondary objectives ` |
| 07bf | `flag.pic` |
| 07c8 | `flag.pal` |
| 07d1 | `the %s successfully accomplished.  ` |
| 07f5 | `both objectives unaccomplished.  ` |
| 0817 | `If this had been an actual combat mission, %s would now be dead, with %s` |
| 0860 | `%s was killed in action after accomplishing his mission.  ` |
| 089b | `%s died in the line of duty, mission incomplete.  ` |
| 08ce | `Accomplishing the %s helped calm the squadron commander.` |
| 0907 | `The squadron commander was not pleased that the assigned objectives remain unaccomplished.  ` |
| 0964 | `After an embarrassing international incident, %s returned to his squadron.  %s` |
| 09b3 | `%s survived ejection after successfully accomplishing the %s.` |
| 09f1 | `%s survived ejection but failed to accomplish either of the assigned objectives.  ` |
| 0a44 | `%s landed safely after accomplishing the %s!` |
| 0a71 | `%s landed safely but failed to accomplish either assigned objective.  ` |
| 0ab8 | `\n\n` |
| 0abb | `, but will not be recorded as this was a training flight.` |
| 0af5 | `The performance rating for this mission was %d%s.\n\n` |
| 0b29 | `%d enemy aircraft were shot down.\n\n` |
| 0b4d | `In an impressive display of dogfighting skills, %d enemy aircraft were shot down!\n\n` |
| 0ba1 | `One enemy plane was shot down.\n\n` |
| 0bc2 | `One enemy ground target was destroyed.\n\n` |
| 0beb | `%d enemy ground installations were destroyed.\n\n` |
| 0c1b | `%d civilian and %d unauthorized military` |
| 0c44 | `%d unauthorized military ` |
| 0c5e | `%d civilian ` |
| 0c6b | `In violation of your Rules Of Engagement, %s` |
| 0c98 | `targets were destroyed.\n\n` |
| 0cb2 | `target was destroyed.\n\n` |
| 0cca | `In violation of your Rules Of Engagement, %d civilian ` |
| 0d01 | `targets were destroyed.\n\n` |
| 0d1b | `target was destroyed\n\n` |
| 0d32 | `Friendly forces are outraged by the attack and destruction of their ` |
| 0d77 | `aircraft and installations.` |
| 0d93 | `aircraft.` |
| 0d9d | `installations.` |
| 0dac | `` |
| 0e90 | `Your plane crashed spectacularly into the ground.` |
| 0ec2 | `Your plane exploded as it slammed into a hill.` |
| 0ef1 | `Your main tanks ran out of fuel and you went down.` |
| 0f24 | `Destroyed by enemy missiles, your plane crashed before you ejected.` |
| 0f68 | `Your plane crashed onto the runway, cartwheeled and exploded.` |
| 0fa6 | `Your prudent attempt to eject from the aircraft failed.` |
| 0fde | `cemetary` |
| 0fe7 | `landed2.pic` |
| 0ff3 | `landed3.pic` |
| 0fff | `landed4.pic` |
| 100b | `landed1` |
| 1013 | `desk` |
| 1018 | `endhangr` |
| 1021 | `flight` |
| 1028 | `requestr.pic` |
| 1035 | `` |
| 105a | `Your bail-out was successful. An SAR aircraft homes on your` |
| 1096 | ` beacon and lifts you to safety.` |
| 10b7 | `rescue` |
| 10be | `tass` |
| 10c3 | `` |
| 10c4 | `requestr.pic` |
| 10d1 | `adv.pic` |
| 10d9 | `adv.pal` |

### A.2 Quip tables (39D0): `[55c0]` = table, `[55be]` = count; entry = `rand_n(count)`

Table DS:48e7, count 8: no objective, no award, not the 99th mission (screen desk)

| i | ptr | text |
|---|---|---|
| 0 | 48f7 | `What happened to the part about piece a cake?` |
| 1 | 4925 | `Time for more simulator action.` |
| 2 | 4945 | `Hmmm, now where's that operations manual...` |
| 3 | 4971 | `The Colonel never told me the part about them shooting back...` |
| 4 | 49b0 | `Those MiGs are a lot better than those simulators.` |
| 5 | 49e3 | `Working on a promo to wing weenie...` |
| 6 | 4a08 | `I can't believe those guys...` |
| 7 | 4a26 | `Mom said there'd be days like this.` |

Table DS:4510, count 7: objective done but captured (screen desk)

| i | ptr | text |
|---|---|---|
| 0 | 451e | `Looks like I zigged when I should've zagged.` |
| 1 | 454b | `Well, guess I'd better learn Russian.` |
| 2 | 4571 | `At least I got my picture in the paper` |
| 3 | 4598 | `At least Tass spelled my name right.` |
| 4 | 45bd | `I can't believe they used that photo.` |
| 5 | 45e3 | `This is not going to look good on my resume.` |
| 6 | 4610 | `What would Mom say if she saw that... home town boy gone Bad?` |

Table DS:4a4a, count 4: mission 99 and rank 6 (General), screen flight

| i | ptr | text |
|---|---|---|
| 0 | 4a52 | `- General, At Last!\n- Off we go into the wild blue General's Office.\n- Now I'll leave the fighting to them.\n- When in doubt, get a lieuteneant to do it.` |
| 1 | 4a66 | `- Off we go into the wild blue General's Office.\n- Now I'll leave the fighting to them.\n- When in doubt, get a lieuteneant to do it.` |
| 2 | 4a97 | `- Now I'll leave the fighting to them.\n- When in doubt, get a lieuteneant to do it.` |
| 3 | 4abe | `- When in doubt, get a lieuteneant to do it.` |

Table DS:4e53, count 4: mission 99, rank not 6, screen flight

| i | ptr | text |
|---|---|---|
| 0 | 4e5b | `- Wow, 99 missions!\n- Now I go to that nice, quiet airline job, flying to Hawaii?\n- Maybe I'll write a book, like Schwarzkoff.\n- The lecture circuit sounds nice and cushy...` |
| 1 | 4e6f | `- Now I go to that nice, quiet airline job, flying to Hawaii?\n- Maybe I'll write a book, like Schwarzkoff.\n- The lecture circuit sounds nice and cushy...` |
| 2 | 4ead | `- Maybe I'll write a book, like Schwarzkoff.\n- The lecture circuit sounds nice and cushy...` |
| 3 | 4eda | `- The lecture circuit sounds nice and cushy...` |

Table DS:4c32, count 5: promoted or medal, screen endhangr

| i | ptr | text |
|---|---|---|
| 0 | 4c3c | `- It's party time!\n- Now boys, let's wait til we get to the club.\n- Do we have to...?\n- Well...` |
| 1 | 4c9c | `- You toasted 'em tonight, huh!\n- You bet!\n- Here's to Mikoyan-Gurevich...\n- The great designer himself...` |
| 2 | 4d07 | `- I don't think they ever even knew I was there.\n- They do now, though.` |
| 3 | 4d4f | `- Think we ought'a salute him?\n- Nah, his head's big ehough already.\n- Be even bigger tomorrow morning!\n- Yeah, but he's got the 48 hour pass.` |
| 4 | 4dde | `- Those guys never knew what hit 'em.\n- They probably still don't.\n- They know they got hit, though.\n- I heard that!` |

Table DS:4aeb, count 5: ordinary landing, comm+2Ch < 3 (quiet), screen endhangr

| i | ptr | text |
|---|---|---|
| 0 | 4af5 | `- Gonna slouch yer way to 99, eh?\n- Naw, he just needs his beauty sleep.` |
| 1 | 4b3e | `- Smooth mission, huh?\n- Hey, this man's name is Silk.` |
| 2 | 4b75 | `- Heard you had a quiet time up there.\n- Catching some ZZs?` |
| 3 | 4bb1 | `- Too bad it was so quiet up there.\n- Pretty country, though, huh?.` |
| 4 | 4bf5 | `- Hear you had a milk run today.\n- Did you spill some of it?` |

Table DS:464e, count 6: ordinary landing, comm+2Ch >= 3, screen endhangr

| i | ptr | text |
|---|---|---|
| 0 | 465a | `- You made it back...!\n- Yep, just barely.\n- No way man, you kicked 'em.` |
| 1 | 46a3 | `- That's another notch in your gunbelt.\n- Aw, it was nothing, really.\n- Yea, yea, sure Pal.` |
| 2 | 46ff | `- Way to go, man!\n- There may not be any bad guys left, when he's done.\n- Hey, man, business as usual, just business as usual` |
| 3 | 477d | `- You think you did enough tonight?\n- Maybe enough for one night, but there's always tomorrow.\n- Mr. Modesty strikes again.` |
| 4 | 47f9 | `- Was it Hot out there, or what?.\n- Maybe a little... warm...\n- Wait til you meet some real fighters, buddy.` |
| 5 | 4866 | `- Ever wish you'd joined the Army?\n- hmmm, maybe once - when I was very young.\n- I know what you mean bro, I know what you mean.` |

### A.3 Name tables

| table | entries |
|---|---|
| DS:1b8a short rank (pilot+20h), used for the display name | 0=`2nd Lt. `; 1=`1st Lt. `; 2=`Capt. `; 3=`Major `; 4=`Lt. Col. `; 5=`Col. `; 6=`Brig. Gen. ` |
| DS:05f4 long rank name of rank+1 (index = current rank); 05f2+2*rank = name of the current rank | 0=`First Lieutenant`; 1=`Captain`; 2=`Major`; 3=`Lieutenant Colonel`; 4=`full Colonel`; 5=`Brigadier General` |
| DS:05d2 medal text (index [643a]) | 0=`c_europe.pic`; 1=`PURPLE HEART for being wounded in combat operations`; 2=`AIRMAN's MEDAL for exceptional mission performance`; 3=`DISTINGUISHED FLYING CROSS for valor in the line of duty`; 4=`SILVER STAR for heroism while flying a combat mission`; 5=`AIR FORCE CROSS for valor and heroism on a combat mission`; 6=`CONGRESSIONAL MEDAL OF HONOR, America's highest military honor, for heroism above and beyond the call of duty` |
| DS:05de medal short name (index [55da] = 1..6; entry 0 unused, entry 1 is empty, entry 6 is the first word of DS:05ea) | 0=`CONGRESSIONAL MEDAL OF HONOR, America's highest military honor, for heroism above and beyond the call of duty`; 1=``; 2=`Airman's Medal`; 3=`Distinguished Flying Cross`; 4=`Silver Star`; 5=`Air Force Cross`; 6=`Congressional Medal of Honor` |
| DS:05ea ribbon / CMH name (index [55e2]) | 0=`Congressional Medal of Honor`; 1=`OverSeas Short Tour Ribbon`; 2=`OverSeas Long Tour Ribbon`; 3=`Longevity Service Ribbon`; 4=`Longevity Service Ribbon with Gold Cluster` |
| DS:0dae objective wording (index 2*primary+secondary) | 0=``; 1=`secondary objective `; 2=`primary objective `; 3=`primary and secondary objectives ` |
| DS:1b3e missile names (event 5 arg) | 0=`None`; 1=`SA-2`; 2=`SA-5`; 3=`SA-8B`; 4=`SA-10`; 5=`SA-11`; 6=`SA-12`; 7=`SA-13` |
| DS:1b98 weapon names (index = weapon id from comm+38h[bay]) | 0=`AIM-9M Sidewinder`; 1=`AIM-120A AMRAAM`; 2=`AGM-88A HARM`; 3=`Penguin-3 ASM`; 4=`AGM-84A Harpoon`; 5=`AGM-650 Maverick`; 6=`GBU-12 Paveway`; 7=`MK 20 Rockeye`; 8=`Durandal Bomb`; 9=`MK 82-0 Slick`; 10=`MK 82-1 Snakeye`; 11=`MK 20 Rockeye II`; 12=`MK 122 Fireye`; 13=`CBU-72 FAE`; 14=`MK 35 IN Cluster`; 15=`ISCB-1 Minelets`; 16=`135MM/IR Camera`; 17=`1900 lbs. Fuel`; 18=`Special Equip` |
| DS:00ce theatre picture names (index pilot+38h) | 0=`libya.pic`; 1=`persian.pic`; 2=`n_cape.pic`; 3=`c_europe.pic`; 4=`vietnam.pic`; 5=`mid_east.pic`; 6=`kuwait.pic`; 7=`cuba.pic`; 8=`korea.pic` |
| DS:4f09 award / promotion format pointers | 0=`If this had been a real mission you'd be getting the %s.`; 1=`Because your capture exposed our stealth technology, the squadron commander will not recommend you for the %s.`; 2=`The capture and public trial of %s will delay his promotion to %s.`; 3=`For demonstrated skill and daring achievment, %s was %spromoted to the rank of %s`; 4=`Congratulations on the successful completion of your 99th mission and retirement from the Air Force.` |

### A.4 Unit types (DS:179e + 20h*t, t = 0..28): name, nickname, radar word (+18h)

| t | name | nickname | +18h |
|---|---|---|---|
| 0 | `MIG-23` | ` Flogger` | 0011 |
| 1 | `MIG-25` | ` Foxbat` | 0012 |
| 2 | `MIG-29` | ` Fulcrum` | 0013 |
| 3 | `MIG-31` | ` Foxhound` | 0014 |
| 4 | `Su-27` | ` Flanker` | 0013 |
| 5 | `IL-76` | ` Mainstay` | 0010 |
| 6 | `F-4E` | ` Phantom` | 0012 |
| 7 | `F-14` | ` Tomcat` | 0013 |
| 8 | `F-18` | ` Hornet` | ffff |
| 9 | `An-72` | ` Coaler` | 0000 |
| 10 | `F-18` | ` Hornet` | ffff |
| 11 | `MIG-23` | ` Flogger` | 0000 |
| 12 | `F-14` | ` Tomcat` | ffff |
| 13 | `F-4E` | ` Phantom` | ffff |
| 14 | `Yak-38` | ` Forger` | 0011 |
| 15 | `Tu-95` | ` Bear` | 0000 |
| 16 | `Mi-24` | ` Hind` | 0011 |
| 17 | `F-5` | ` Tiger` | 0016 |
| 18 | `767` | ` Boeing` | ffff |
| 19 | `MIG-17` | ` Fresco` | 0011 |
| 20 | `F-1` | ` Mirage` | 0014 |
| 21 | `F-15` | ` Eagle` | ffff |
| 22 | `F-16` | ` Falcon` | ffff |
| 23 | `UH-60` | ` Blackhawk` | 0000 |
| 24 | `A-6` | ` Intruder` | ffff |
| 25 | `MIG-21` | ` Fishbed` | 0016 |
| 26 | `EF-111` | ` Aardvark` | ffff |
| 27 | `C-5` | ` Galaxy` | 0000 |
| 28 | `A-10` | ` Tbolt II` | ffff |

### A.5 DGROUP map

All data symbols (115 rows: tables, flags, buffers, sprites, cursor) are in `port/spec/debrief_symbols.csv` (`kind = data`); the function index is the `func` rows (96).
