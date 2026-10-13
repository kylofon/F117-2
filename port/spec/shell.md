# Shell: START.EXE (S3.5)

**Status: S3.5 delivered.** Symbols: `shell_symbols.csv` (merged from four reader passes; `work/s35/syms_*.csv`).
Disassembly: `python tools/dsann.py START 0A95 FROM TO` (START DGROUP = 0A95). Program map: START game code
0000:0010-8010 (~130 functions); library (RTL / video / platform helpers) already in `platform.md` / `video.md`.
Chain, exit codes, comm: `RE_GUIDE.md` 2.1-2.4. **Corrections to RE_GUIDE 2.4:** `DS:CBD8` points to `0000:04F2`
(F117.COM first-run word), the comm block is `DS:E096/E098`, the active pilot record is `DS:CACA/CACC` (comm+1272h).
**ID quiz is dead code in this build** (three NOPs at 0x1D-0x1F in `ui_main` replace its call): the port skips it.
Sections: A = ready room/hangar/arming/briefing map (0010-1F3F), E = orders (20D6), B = UI toolkit/briefing text
(24F7-40F0), C = mission generator `4239` + orders helpers (4239-623D), D = roster/pilot options/shell init/terrain (623E-8010).
Claims marked (U) need a visual or runtime check; reader notes follow verbatim.


---
# Part A: ready room, hangar, arming, briefing map

### S3.5 reader a: START.EXE 0000:0010 .. 0000:1F3F (lines 1-3388 of a.txt)

Scope: `ui_main` (0010), ready room (020f), hangar (0311/038c/0448), the dead ID-quiz (057e/0703/0786),
arming screen (07ee/0963/0b01/0bd4/0d65/0e23/0e96) and the briefing map (0f4e/0fa3/11db/1308/141d/149c/183a/196a/
1ab6/1b37/1c2f/1f3f). Next function 20D6 (mission-orders text) starts at a.txt line 3389 and belongs to reader b.
All coordinates are in 320x200 pixels. "(V)" = verified in the disassembly, "(U)" = unclear or needs a visual check.
Helpers that are outside this section are referred to by address and role only (other readers name them; platform and
video helpers use the names of `platform_symbols.csv` / `video_symbols.csv`).

## 0 Conventions and helpers used by this section (V unless marked)

* **Pages** (MGRAPHIC page table at DS:CAB0, word per page, slot 57/75 sets it): 0 = visible screen (A000), 1 = working
  page (all UI is drawn on page 1, then blitted to 0), 2 = "save-under / frame art" page (`requestr.pic`, pic 11 by
  default; the arming screen temporarily loads `weapons3` there), 3 and 4 = sprite sheets / clean copies (hangar,
  weapon pictures, briefing background, map).
* **Draw contexts** (TextCtx/Win, `video.md` section 3): `DS:DC02` = page 0, `DS:DC18` = page 1 (both 16h bytes, +04 fg,
  +06 bg, +08 x, +0A y, +0C font). Typical sequence: draw on `DC18`, then `blit_rect(1,..,0,..)` the touched rectangle to the screen.
  Text colours are palette indices that double as button colours (see palette trick).
* **blit_rect** = MGRAPHIC slot 42 `blit_rect(spage, sx, sy, dpage, dx, dy, w, h)`: `lcall 0A95:6A8A`.
  **snd_cmd(n)** = slot 101 `lcall 0A95:6BB1`. **screen_off/on** = slots 69/70 (`6B11`/`6B16`).
* **Button tables** (UI "hotspot lists") live in DGROUP: `word hook; byte key; byte n; {x0,y0,x1,y1}[n]` (8 B per entry,
  inclusive pixel rectangles). `ui_wait(tbl)` = `38AC`: loop { call hook() if non-zero; `39F7` polls joystick/mouse/keyboard,
  moves the cursor (E08A/E08C), draws the cursor sprite; returns 1 = click/Enter, 2 = Esc/right button, 62h =
  Del/Backspace, else an ASCII key }. Result: click -> index of the hit entry (1-based, `393D(entries,n)`) and snd 1Ah, or snd 22h
  on a miss; Esc -> **63h** (snd 23h); key == tbl.key -> **100h + key** (so space = 120h); 0 never returned.
  `ui_hit(entries, n)` = `393D` returns the 1-based entry under the cursor, 0 if none. `39B3(e)`/`39D5(e)` make an entry
  unhittable/hittable again by swapping x0/x1 so x0>x1 / x0<=x1 ("disable"/"enable" button).
* **Palette trick** (V): button colours are palette indices. `pal_queue(first, rgb_ptr, count)` = `3517` appends a 6-byte
  record to the queue at DS:64A6 (count byte DS:64E2); `pal_apply_queue` (8427, called in every wait loop) writes them to
  the DAC. Highlighting a button = queuing a new RGB for its colour. Button n (1..18) of the fixed tables uses colour
  `0x100-n` (FFh..EEh); site buttons of the briefing use `0B7h+n`; map range rings use C0h+i. `DS:64E3` is a 256x3 byte
  palette buffer (6-bit RGB) used by fades `3088(flag)`=fade out, `3070(flag)`=fade in (via `30A0`).
  Fixed RGB triples in DGROUP (6-bit r,g,b): 11AC white 3F3F3F, 11AF red 3F0000, 11BB (15,3F,3F), 11BE salmon 3F322B,
  11C1 red 3F0000, 11C4 light grey 393C3B ("normal"), 11C7 tan 3E291B ("highlight"), 11CA grey 2A2A2A ("disabled"),
  11CD (0A,12,0F), 11D6 dark grey 1D1D1D.
* **ui_show_screen(pic, hook)** = `3641(pic, hook)`: fade out (`3088(1)`), `35FC(pic, page 1)` (load `<name>.pic`, name table DS:0CBA, via `2864`
  + `pic_to_seg_b` 8782), `826C(pic)` palette, blit page 1 -> 0 (320x200), call `hook()` if non-zero, fade in (`3070(1)`), screen_on.
  Picture names (DS:0CBA, index): 0 air_id, 1 armsscrn, 2 brefscrn, 3 folder1, 4 hangscrn, 5 homescrn, 6 ordrscrn, 7 rostscrn,
  8 breficon, 9 folder2, 10 ordrsprt, 11 requestr, 12 rostsprt, 13 weapons1, 14 weapons2, 15 weapons3, 16 libya, 17 persian,
  18 n_cape, 19 c_europe, 20 vietnam, 21 mid_east, 22 kuwait, 23 cuba, 24 korea, 25..29 planes1..5, 30 hangsprt, 31 adv, 32 climbin.
  `35FC(pic, page)` loads any of them into any page.
* Other callees: `3989` cursor_erase (restores the old cursor rect, blit page1 -> 0 of E08E/E090/E092/E093); `3E84(x,y)` set cursor
  position (also moves the mouse driver); `3588` erase message box (blit saved rect from page 2 to pages 1 and 0, rect DS:B2F6/B2F8/B2FC/B302);
  `2B1B(str,x,y,font,center,width)` message box with wrapped text; `3F33(ticks)` / `405F(ticks)` wait with abort (0 = timeout,
  1 = Enter/click, 2 = Esc, 3 = space; 405F adds the click sound); `2B06(n)` wait n retraces; `3693(ctx,x,y,sprseg)` = sprite_scaled wrapper (slot 22);
  `3567(ctx,str,x,y)` text at x,y (slot 5); `3776(ctx,str,x,y,maxw,line_h,center)` wrapped text (maxw = line width limit in pixels, center = 1 centres each line in maxw; ctx.page == -1 = measure only, returns the widest line); `8A32` gfx_fill_rect; `3CEA(tbl,x,y)` modal
  dialog (descriptor `{text,label1,label2,x,y, button table...}`); `5625(name)` (reader c) walks the mission state structures with `588D(ptr,size,count)` (it copies them into `comm+7Ah` only; the `name` argument is never read and START never creates TEMP.WLD, (corrected 2026-10-13, see HANDOFFS C6); was: U: save of the mission to TEMP.WLD for VGAME; direction to be confirmed); `40F0` generates a mission
  (decode of the orders); `76C0(n)` = rand() % n; `76D1(code)` shutdown + exit(code); `6A7C(name)` pointer to the last
  word of a name.

## 1 Entry (0010) and ready room (008D, 020F, 02BA)

### 0000:0010 `ui_main` void ui_main(void) (V)
1. `7312()` init (comm pointers, MGRAPHIC pages, sound...). 2. First-run test `comm04F2 != 0` followed by **three NOPs
   (0x1D..0x1F)**: the original `call 0703` (ID quiz, copy protection) has been removed in this build; port: do not call
   `quiz_run`. 3. `6D07()` copies the active pilot record into its roster slot and does the roster housekeeping (reader d).
4. `0E23()` arming roll (random unavailable weapons). 5. `020F()` ready room (returns when the player launches).
6. `5625("temp.wld")` (DS:0042) hands the planned mission to the game (U: write; see helper note). 7. `comm+26h = 3; comm+28h = 0; comm+2Ch = 0;
   comm+30h = (pilot.mission_type > 1)` (1 = training; conflicts with the RE_GUIDE note that 0030 is the joystick flag, see open items).
8. `76D1(code)` -> exit(code) with **0Dh if site[E062].flags_hi & 2 else 0Ch** (site = DS:CBDE + 16*E062).
Callers: `c0_start`. Callees as listed.

### 0000:008D `roster_home_card_draw` void(void) -- hook of the ready room screen (V)
Called by `ui_show_screen(5 homescrn, 0x8D)`. Draws the active pilot's card (`pilot` = far ptr DS:CACA):
* Cursor is pre-positioned: `D2A2 != 0` -> (168,78) (inside hotspot 5 = hangar) else (277,53) (hotspot 2 = briefing).
* font 3 (`DC24`), fg 8 (`DC1C`) on ctx DC18, all via `3567(DC18, str, x, y)`:
  1. rank + surname `sprintf("%s%s", rankstr[pilot.rank], 6A7C(D894+50h*pilot.slot))`, string cut at 24 chars (byte at buf+18h := 0) at (105,122). Rank table DS:1D78 -> "2nd Lt ","1st Lt ","Capt ","Major ","Lt Col ","Col ","B Gen ".
  2. theatre name table DS:9ABE[pilot+38h] ("Libya","Persian Gulf","North Cape","Central Europe","Vietnam","Middle East") at (103,132).
  3. war level DS:A155[pilot+3Ah] ("Cold War","Limited War","Conventional War") at (115,139).
  4. mission type DS:9522[pilot+3Ch] ("Air Missions","Strike Missions","Air Training","Strike Training") at (114,151).
  5. enemy skill DS:92FA[pilot+3Eh] ("Green","Regular","Veteran","Elite") at (130,167).
  6. crash mode DS:94D0[pilot+40h] ("No Crashes","Easy","Realistic") at (130,175).
  7. flight model DS:94AC[pilot+42h] ("MicroProse F117A","Lockheed F117A") at (130,183).
  8. `sprintf("%d", roster_difficulty_rating())` (fmt DS:0050) at (189,115).
* Finally `blit_rect(1,103,115,0,103,115,100,73)`.
Pilot record offsets used (50h bytes, = `comm+1272h`): +00 roster slot (0..9), +02 name, +20 rank, +38 theatre, +3A war level, +3C mission type
(0 air,1 strike,2 air training,3 strike training), +3E skill, +40 crash, +42 flight model (0 = 4 weapon bays, 1 = 2 bays), +4E flag (non-zero: ready room opens on the roster screen).

### 0000:02BA `roster_difficulty_rating` int(void) (V)
`r = (b60[theatre] + b54[mission_type] + b58[skill] + b5C[crash]) / 3 - flight_model + 2` (unsigned division by 3), then `if ((unsigned)r > 10) r = 10`. Byte tables at DS:0054 `{5,7,0,0}`, 0058 `{0,3,5,8}`, 005C `{0,3,6,0}`, 0060 `{0,2,5,6,3,3,4,2,5}`
(9 theatres). Used for the card's rating number (U: label of the number on homescrn).

### 0000:020F `ui_ready_room` void(void) (V)
```
done = 0; fresh = 1; state = 0;
if (comm04F2 != 0 || pilot.f4E != 0) state = 4;        // go to roster screen first
while (!done) {
  if (state == 0) {
     for (;;) {
        if (fresh) ui_show_screen(5 /*homescrn*/, roster_home_card_draw);
        state = ui_wait(&tbl_6F42);                       // 5 hotspots; 63h (Esc) -> wait again WITHOUT redraw
        if (state != 63h) break;  fresh = 0 -> (jumps to the wait only)
     }
  }
  fresh = 1;
  switch (state) {
    1: cursor_erase(); state = orders_screen();   // 5BF0 (ordrscrn, sets pilot options; returns next state)
    2: cursor_erase(); state = brief_screen_run(); // 0FA3 ... 
    3: cursor_erase(); state = 0963(arming);
    4: cursor_erase(); state = roster_screen();   // 623E (rostscrn; clears comm04F2)
    5: cursor_erase(); done = hangar_launch(); state = 0;   // 038C
    default: ;                                   // state stays (e.g. 0..)
  }
}
```
Each sub-screen returns the next state (0 = back to home, 5 = go to hangar after arming, 3 = to arming from the briefing). Hotspot table DS:6F42 (hook 0, key 0, n 5): 1 (3,40)-(54,128) orders; 2 (256,11)-(299,95) briefing map; 3 (203,38)-(225,105) arming; 4 (69,45)-(132,80) roster; 5 (149,53)-(187,104) hangar/fly.
(Mapping of hotspots to screens verified from the dispatch; the object names on homescrn are (U).)

## 2 Hangar / launch (0311, 038C, 0448)

### 0000:0311 `ui_hangar_hook` void(void) (V) -- hook of `ui_show_screen(4 hangscrn, 0x311)`
`35FC(30 /*hangsprt*/, page 3)`; `blit_rect(1,0,0,4,0,0,320,200)` (clean backdrop copy to page 4); then frame 0 of the hangar strip:
`blit_rect(3, r0.sx, r0.sy, 0, r0.dx, r0.dy, r0.w, r0.h)` and the same to page 1, where `r0` = first 12-byte record of DS:006A.

### 0000:038C `ui_hangar_launch` int(void) (V)
```
if (site[E062].hi & 2) ui_show_screen(32 /*climbin*/, 0); else ui_show_screen(4 /*hangscrn*/, ui_hangar_hook);
if (D2A2 == 0) { ui_dialog(DS:7200 "You need to receive a mission briefing first." [OK], 0xA5, 0x9E); return 0; }
r = ui_dialog(DS:73A8 "Are you ready for your mission?" [Yes][Not yet], 0xA1, 0xA4);
if (r == 1) { palette[255] = DS:11BE (3 bytes copied to DS:67E0); cursor_erase(); 2B06(20);
              if (!(site[E062].hi & 2)) ui_hangar_anim();
              screen_off(); DS:DC06 = 0; gfx_fill_rect(DC02, 0,0,0x13F,0xC8); return 1; }
if (r == 2 || r == 63h) { cursor_erase(); return 0; }
return r;
```
The fill blanks the screen (colour 0 on page 0). Returns 1 = launch.
`site.hi & 2` (flag 0200h of the site record) = "no hangar" start (climb-in picture, exit code 0Dh).

### 0000:0448 `ui_hangar_anim` int(void) (V)
`snd_cmd(1Fh)`; for i in 0..17: `idx = (signed char)DS:0136[i]` (-1 = none); `DS:ADFC = 0` (never changed -> delay 8);
if idx != -1: restore `blit_rect(4,30,108,1,30,108,105,92)`, draw hangsprt frame: `blit_rect(3, R[idx].sx,.sy, 1, .dx,.dy,.w,.h)` with `R` = 12-byte records at DS:006A
(6 words: sx,sy,dx,dy,w,h; 13+ records, e.g. rec0 (0,0)->(30,108) 105x92), then `blit_rect(1,30,108,0,30,108,105,92)` to the screen.
`idx2 = (signed char)DS:0148[i]`: if != -1 draw directly on page 0 `blit_rect(3, S[idx2]...)` with `S` = records at DS:0106 (4 records, dest (124,0), sizes 73x39/41/46/56).
Then `if (3F33(8 - ADFC)) return 0;` (any key/click aborts the animation and skips the tail). After the loop (0544..0574): restore `blit_rect(4,30,108,0,30,108,105,92)`, `3F33(0x78)`, `3088(1)` (fade out); return value unspecified. The return value is not used by `ui_hangar_launch`.
Byte table DS:0136 (18): `0,FF,FF,1,FF,2,3,4,FF,FF,5,6,7,8,9,10,11,12`; DS:0148: `0,1,2,3,FF x14`.

### 0000:057B, 057C `ui_nop_a`, `ui_nop_b` (V)  `ret` stubs, called only from the C startup init tables (8F73/8F76).

## 3 ID quiz (copy protection; NOT reachable in this build)

Dead code: the only call (in `ui_main`) was patched out. Port: implement only if a "classic protection" option is wanted; otherwise skip. It sets `comm+22h` (security clearance) which is
left at SETUP's value (1).

### 0000:057E `quiz_draw` void(void) (V) -- hook of `ui_show_screen(0 air_id, 0x57E)`
* `srand(bios_timer())`; `ans = rand()%20` -> `DS:B282`. cat = ans>>2 (0..4 = planes1..5), `sx = (ans&1)*160`, `sy = ((ans&2)>>1)*79`.
* `blit_rect(0,0,0,1,0,0,320,200)` (screen -> page 1); `35FC(25+cat, page 4)`; `blit_rect(4,sx,sy,1,146,48,150,79)` (picture of the aircraft, one quadrant of the sheet);
  `blit_rect(2,241,180,1,146,122,19,5)` (small bar; page 2 content (U)); `blit_rect(1,...)` to screen for a 240x? area (full 320x200 at the end).
* Wrong choices: `w1 = rand()%20` != ans, `w2 = rand()%20` != ans and != w1 (retry loops).
* Prints 18 names (all 20 aircraft except w1, w2) in 3 columns of 6: font 1, fg starts at 0xFF and decrements per name (colour 0x100-k for button k+1),
  x = 31, y = 142 (+9 per row); every 6th name: x += 95, y = 142. Names = DS:936A table (20): F-14D Tomcat, F-15C Eagle, F-16G Falcon, F/A-18A Hornet, AV-8B Harrier, EF-111A Raven,
  A-6E Intruder, A-10 Thunderbolt, B-1B Bomber, E-3C Sentry, Su-24 Fencer, Yak-38 Forger, Mig-21 Fishbed, Mig-23 Flogger, Mig-25 Foxbat, Mig-29 Fulcrum, Mig-31 Foxhound, Su-27 Flanker, Tu-26 Backfire, Il-76 Mainstay.
  `DS:B270[k]` = aircraft index shown on button k+1.
* Finally `blit_rect(1,0,0,0,0,0,320,200)`.

### 0000:0703 `quiz_run` void(void) (V)
`comm+22h = 0; ui_show_screen(0, quiz_draw); r = ui_wait(&tbl_6BC6)` repeated while r == 63h; if `DS:B26F[r] == B282` (i.e. `B270[r-1]`): `comm+22h = 1`, message DS:015A
"Correct. Security clearance confirmed."; else `comm+22h = 0`, DS:0181 "Incorrect.  Your security clearance level is now reduced."; `cursor_erase(); 2B1B(msg,160,90,font 4,center 1,w 160); 405F(240); 3588()`.
Button table DS:6BC6 (hook 0786, 18 entries, 3 columns x 6 rows): x 30-124 / 125-219 / 220-315, y 141+9*row .. +8.

### 0000:0786 `quiz_hover_hook` void(void) (V)
`h = ui_hit(6BCA, [6BC9]=18)`; if h != 0 && h != D096: `pal_queue(0x100-h, 0x11C7 (tan), 1)`; if D096 != 0 && D096 != h: `pal_queue(0x100-D096, 0x11D6 (dark grey), 1)`; `D096 = h`.

## 4 Arming screen (weapon bays)

Weapon ids (rows 1..18 -> id 0..17): 0 short-range IR AAM, 1 medium-range AAM, 2 radar-homing (HARM-like), 3 direct anti-ship, 4 sea-skimming anti-ship, 5 fire-and-forget optical/IR,
6 laser-guided glide bomb, 7 retarded cluster bomb, 8 runway bomb, 9 free-fall bomb, 10 retarded large-area bomb, 11 LG cluster bomb, 12 free-fall incendiary, 13 LG fuel-air, 14 retarded incendiary,
15 retarded mine cluster, 16 recon pallet, 17 fuel tank (+1900 lbs), 18 special equipment container, 19 = empty bay. Description strings: DS:A587 table (20 pointers).
Bays: `comm+38h + 2*i` (i = 0..3) = weapon id (word), `comm+40h + 2*i` = quantity (word, from byte table DS:1EC5[row]: `0,4,3,1,2,1,2,2,2,2,3,3,2,2,2,2,1,1,1,1`), `comm+2Eh` = estimated fuel in lbs (`12 x route score`, written by the mission generator at `4F51`; shown here and in the briefing as "Est. Fuel required"; "payload" was wrong, (corrected 2026-10-13, see HANDOFFS C10)).
Number of bays = `(2 - pilot.flight_model)*2` (4 or 2). Bay box positions: DS:01D6 (x) and DS:01E6 (y), index `flight_model*4 + slot`: (126,8),(254,8),(254,89),(126,89) for model 0; (126,8),(254,8) for model 1.

### 0000:07EE `arm_screen_draw` void(void) -- hook of `ui_show_screen(1 armsscrn, 0x7EE)` (V)
* `DS:D878 = DS:D879 = (byte)pilot.flight_model` (weapons 0 and 1 are therefore disabled for flight model 1).
* For i in 0..3: if `D2A2 == 0` or `i >= (2-flight_model)*2`: `comm[38h+2i] = 0x13` (empty).
* For i in 0..15: if `D878[i] != 0` (unavailable): palette colour `255-i` := DS:11CA (grey) (copy to DS:67E0-3i) and `39B3(entry i of DS:6C5E)` (disable button); else `39D5(entry i)` (enable).
* `D096 = 0`; `35FC(15 weapons3, page 2)`, `35FC(13 weapons1, page 3)`, `35FC(14 weapons2, page 4)`.
* For i in 0..(bays-1): `arm_draw_bay(i, comm[38h+2i])`. If `D2A2 == 0`: `comm+2Eh = 0`. font 3, fg 15: `sprintf("%d lbs.", comm+2Eh)` (the fuel estimate, (corrected 2026-10-13, see HANDOFFS C10)) at (118,57), `blit_rect(1,118,57,0,118,57,44,5)`.
* `arm_draw_fuel()`; cursor to (271,148) (button 23).

### 0000:0963 `arm_screen_run` int(void) (V)
```
slot = 0;
ui_show_screen(1, arm_screen_draw);
for (;;) {
  r = ui_wait(&tbl_6C5A);
  if (r == 63h || r == 24) { cursor_erase(); 35FC(11 requestr, page 2); return 0; }
  if (r == 23)            { cursor_erase(); 35FC(11, 2);                return 5; }        // proceed to hangar
  if (1 <= r <= 18) {                                    // weapon row r -> id r-1
      if (comm[38h+2*slot] == 0x12) {                    // special equipment container cannot be replaced
          cursor_erase(); snd_cmd(22h); 35FC(11,2);
          2B1B("The Special Equipment container is needed for this sortie!", 160, 100, font 4, center 1, w 120);
          405F(210); 3588(); 35FC(15 weapons3, 2); continue;
      }
      snd_cmd(1Eh); comm[38h+2*slot] = r-1; comm[40h+2*slot] = DS:1EC5[r];
      arm_draw_bay(slot, r-1); arm_draw_fuel(); continue;
  }
  if (19 <= r <= 22) {                                    // bay select (box colour 0x100-r)
      snd_cmd(1Dh);
      if (pilot.flight_model != 0 && r > 20) continue;    // model 1 has only 2 bays
      if (r - slot == 0x13) continue;                     // already selected
      pal_queue(0x100-r, DS:11C1 (red), 1); pal_queue(0xED-slot, DS:11AC (white), 1); slot = r - 19;
  }
}
```
Table DS:6C5A (hook 0B01, 24 entries): 1..18 weapon rows x 7..105, y = 14+6*(i-1)..+5; 19 (126,8)-(180,43), 20 (254,8)-(308,43), 21 (254,89)-(308,124), 22 (126,89)-(180,124) (bay boxes);
23 (231,134)-(312,162) = OK/go; 24 (231,165)-(312,193) = back (verified by dispatch).

### 0000:0B01 `arm_hover_hook` void(void) (V)
`h = ui_hit(6C5E, 24)`. If h != 0: if h <= 18 and h != D096: `pal_queue(0x100-h, 0x11BE, 1); arm_show_weapon_info(h)`; elif h >= 23 and h != D096: `pal_queue(0x100-h,0x11BE,1)` (19..22 get no highlight).
If D096 != 0 and D096 != h: if D096 <= 18: `pal_queue(0x100-D096, 0x11C4, 1)` and if h == 0 `arm_show_weapon_info(0x14)` (clear); elif D096 >= 23: `pal_queue(0x100-D096,0x11C4,1)`. `D096 = h`.

### 0000:0BD4 `arm_show_weapon_info` int(int sel) (V)
sel = row 1..18 (0 treated as 20), 20 = clear. Clear: `DS:DC1C = 3; gfx_fill_rect(DC18, 9,136,221,193)` and `DS:DC06 = 3; gfx_fill_rect(DC02, ...)`; returns 0.
Else `sel--` (0..19); page = 3 (sel <= 9) else 4 with `sel -= 10`; fill (9,170)-(221,193) colour 3 on page 1; picture: `blit_rect(page, DS:0232[sel], DS:0246[sel], 1, 63, 136, 160, 40)`
(2 columns x 5 rows of 160x40: sx {0,160}, sy {0,40,80,120,160}); name plate: `sx = (page==3 ? 0xCC : 0x106)`, `sy = 12*sel + 0x50`: `blit_rect(2,sx,sy,1,9,136,58,6)` and `blit_rect(2,sx,sy+6,1,9,144,58,6)`;
`DC18.page = 1; font 4; fg 15; DC1E = 8; 3776(DC18, DS:A587[row-1], 9, 170, 213, 8, 0)` (left-aligned wrapped description, line width 213 px, 8 px line pitch; the description index is `row-1`, i.e. NOT reduced by 10 for page 4); `blit_rect(1,9,136,0,9,136,213,58)`.

### 0000:0D65 `arm_draw_bay` void(int slot, int wid) (V)
`bx = (flight_model*4 + slot)*2; x = DS:01D6[bx]; y = DS:01E6[bx]`.
1. `blit_rect(2, 0xD2 + 0x37*(slot&1), 0x12*(slot&~1), 1, x, y, 55, 36)` (empty bay frame); 2. icon `blit_rect(2, (wid/6)*0x33, (wid%6)*0x20, 1, x+2, y+2, 51, 32)` (icon grid 51x32, 6 per column, 3+ columns, id 0x12 and 0x13 included);
3. `blit_rect(1, x, y, 0, x, y, 55, 36)`.

### 0000:0E23 `arm_roll_unavailable` void(void) (V)
Clears `D878[0..19]`; `n = rand()%5`; repeat until n weapons flagged: `w = DS:01C4[rand()%16]` (`{1,0,12,4,3,9,11,5,15,12,7,6,9,FF,9,8}`), skip FF and already-flagged; `D878[w] = 1`.
Then `D879 = D878 = (byte)pilot.flight_model`.

### 0000:0E96 `arm_draw_fuel` void(void) (V)
`DS:DC1C = 0x42; gfx_fill_rect(DC18, 118,77,161,81)`; `fuel = (D2A2 ? 10000 : 0) + 1900 * #{bays with weapon 0x11}`; font 3, fg 15: `sprintf("%d lbs.", fuel)` at (118,77); `blit_rect(1,118,77,0,118,77,44,5)`.

## 5 Briefing map ("brefscrn" mission planning)

Site table `DS:CBDE`, 16-byte records, count `DS:D33C` (loaded from the mission file by `5625`):
+0 word location-name key (0 -> use byte +0E), +2 world X, +4 world Y (screen = X/0x92 (146), Y/0xC3 (195)), +6 word **type** (0 = airbase/plain; else radar/SAM type, index into the tables below),
+8 word flags: bit 0 (0x01) airbase of interest, bit 3 (0x08) special sprite / pairing rule, bit 5 (0x20) "occasionally off the air", bit 6 (0x40) "believed under repair";
+9 hi byte of that word: bit 0 neutral, bit 1 (0x200) no-hangar start site, bit 2 (0x400) friendly (else hostile), bit 3 (0x800) hidden, **bit 6 (0x4000) range ring shown** (UI toggle);
+A word fighter type (DS:1708 + 20h*t, name string, `" Nato"` text follows after the NUL), +C word squadron count, +E byte name index (location name pointer table DS:DC70, word each).
Type tables: radar DS:15C6 + 0Eh*t: name[8], +8 word max range km, +10 word factor (avg range = |max*factor| >> 4), +12 byte flags bit 0 = Doppler (else Pulse);
SAM/weapon DS:1AA8 + 12h*t: name[8], +8 word missile range km (names None,SA-2,SA-5,SA-8B,SA-10,SA-11,SA-12,SA-13,SA-N-4..7,Hawk,Rapier,Tiger,Seacat,AA-2..,AIM120,AIM-9,HARM,Penguin,Harpoon,AGM-65,LGBOMB,RTBOMB,FFBOMB,AIM-7W,AIM-9W,SA-14,AA-6,AA-7,AA-9,AA-10,Equip.,SA-9; 40 records).
Mission points: `E062` = departure base site, `E060` = target 1 site, `E072` = target 2 site, `E074` = landing base site (objective structs at DS:E05E and DS:E070; `2651(buf, &struct)` builds the "Destroy the X at Y" text).
Button table DS:6D1E (hook 11DB, key 20h space, n = 8 + #site buttons, count byte at DS:6D21): 1 (228,47)-(317,59) radar view, 2 (228,60)-(317,70) SAM view, 3 (228,71)-(317,83) hostile forces, 4 (228,86)-(317,97) show all ranges,
5 (228,98)-(317,110) erase ranges, 6 (228,113)-(317,126) mission description, 7 (228,129)-(317,140) arming, 8 (228,141)-(317,154) new mission; entries 9.. (dynamic, DS:6D62 + 8*i) = one 14x14 box per site/pair.
Site button i: `DS:B286[2i]` = site A index, `DS:B287[2i]` = site B index (0 = none). `DS:B2E6` = number of site buttons.

### 0000:0F4E `brief_setup` void(void) (V)
`D096 = 0; B284 = 0 (average range); B285 = 1; B2E8 = 1 (radar mode); 3088(1); 35FC(2 brefscrn, page 3); 35FC(0x10 + pilot.theatre, page 4)` (map picture, pictures 16..24 libya..korea);
`826C(2)`; `brief_new_mission()`; cursor to (272,119).

### 0000:0FA3 `brief_screen_run` int(void) (V)
```
brief_setup();
loop: r = ui_wait(&tbl_6D1E)
 1,2,3 : B2E8 = r;  goto mode_change
 4     : for i<B2E6: for A,B of button i: if (site.type != 0) site.hi |= 0x40;  brief_redraw(1); goto mode_change
 5     : same with site.hi &= ~0x40
 6     : mission_orders_screen();   // 20D6
 7     : cursor_erase(); return 3;                        // -> arming
 8     : D2A2 = 0; brief_setup();                         // new mission
 63h   : cursor_erase(); return 0;                        // back to ready room
 120h  : if (B2E8 == 1) { h = ui_hit(6D22, [6D21]); if (h > 8) 3588(); B284 ^= 1; brief_redraw(1); if (h > 8) brief_site_info(h-9); }  goto mode_change
 r > 8 && r < [6D21] : toggle ring bit 0x40 on site A and B of button r-9 (only if type != 0); 3588(); brief_redraw(1);
                       pal_queue(r+0xB7, 11AF red, 1); brief_site_info(r-9); goto mode_change
 mode_change: if (B285 == B2E8) continue; if (B285) pal_queue(0x100-B285, 11C4, 1); pal_queue(0x100-B2E8, 11C7, 1); B285 = B2E8; brief_redraw(1);
```
(the toggle in the `r>8` case uses `DS:B274 + 2*r`, i.e. `B286[2(r-9)]` for A and +1 for B.)

### 0000:11DB `brief_hover_hook` void(void) (V)
`h = ui_hit(6D22, [6D21])`; if h == D096 return. `brief_hint_text(h)`. Old button (D096): if > 8: `3588()`; if site A.type or B.type non-zero restore its colour `pal_queue(0xB7+D096, &palbuf[0xB7+D096], 1)` (palbuf = DS:64E3 + 3*idx, the original RGB);
else if `B2E8 != D096`: `pal_queue(0x100-D096, 0x11C4, 1)`. New button h: if > 8: `brief_site_info(h-9)` and if A.type or B.type != 0 `pal_queue(0xB7+h, 0x11AF red, 1)`; else if `B2E8 != h`: `pal_queue(0x100-h, 0x11BE, 1)`. `D096 = h`.

### 0000:1308 `brief_new_mission` void(void) (V)
`blit_rect(3,0,0,0,0,0,320,200)` and to page 1 (brefscrn frame); colours 253,254,255 := DS:11C4 (normal) and colour `256-B2E8` := DS:11C7 (highlight); `3070(1)`; `405F(10)`;
if `D2A2 == 0`: `2B1B("Decoding mission orders.", 160, 100, font 4, center 1, w 160)`; `40F0()` (generate mission); `405F(70)`; `3588()`.
`brief_build_site_buttons()`; copy map rows 0..168: `blit_rect(4,0,0,1,0,0,320,169)` and `blit_rect(4,0,0,0,0,0,320,169)`; `brief_redraw(0)` (animated); `D2A2 = 1`; `brief_hint_text(0)`; `D096 = 0`.

### 0000:141D `brief_redraw` void(char done) (V)
`blit_rect(4,0,0,1,0,0,320,169)` (clean map to page 1); `done = brief_draw_site_icons(done)`; `brief_draw_route(done)`; mode: `B2E8 == 1 or 2` -> `brief_draw_ranges()`, `B2E8 == 3` -> `brief_draw_hostile()`;
`3693(DC18, 229, 50, [D330])` (emblem/legend sprite); `pal_apply_queue()`; `blit_rect(1,0,0,0,0,0,320,169)`. `done = 0` means "animate with waits and sounds" (first display); non-zero = instant.

### 0000:149C `brief_draw_route` char(char done) (V)
Points A = site[E062], B = site[E060], C = site[E072], D = site[E074] (screen coords `X/146, Y/195`). Draws marker sprite DS:D88E at A(-1,-2) on DC02 and DC18; if !done: text
`sprintf("Depart from %s at %s hours.", locname(A), DS:0D88 /*"00:00" departure time buffer*/)` via `2B1B(.., Ax, Ay+24, font 3, center 1, w 120)` + `405F(240)` -> done, `3588()`.
Then `2F4C(Ax,Ay,Bx,By,done)` animated line, marker DS:E09C at B(-1,-1), if !done the objective text (`2651(buf, DS:E05E)`) at (Bx, By+24) etc.; `2F4C(B->C)`, marker E09C at C, objective text from DS:E070; `2F4C(C->D)`, marker D88E at D(-1,-2) and "Land at %s." (DS:030F).
Labels: ctx DC18 fg 1, font 4, `31FA(str, site, nb1, nb2)` places the label near `site`, away from the two neighbour points: if `E074 == E062`: `("T,L" DS:031B, A, B, C)`; else `("T" DS:031F, A, B, C)`, `("L" DS:0321, D, B, C)`; then always `("P" DS:0323, B, A, C)` (primary target) and `("S" DS:0325, C, B, D)` (secondary target) (V; meaning of P/S inferred from the briefing text headings).
Returns done (0 if no key was pressed). The `locname(s)` = `DS:DC70[ s.w0 != 0 ? s.w0 : s.byte0E ]`.

### 0000:183A `brief_draw_ranges` void(void) (V) (modes 1 and 2)
For each site button i: site = A; if A.type == 0 use B; if site.type != 0: radius = (B2E8 == 2) ? `DS:1AB0[12h*type]` (missile range) :
(B284 == 1 ? `DS:15CE[0Eh*type]` : `|15CE*15D0| >> 4`); `radius <<= 6`; doppler = `DS:15D2[0Eh*type] & 1`; colour = 0C0h+i: RGB := DS:11AC (white) if `site.f8 & 0x40` (under repair) else DS:11BB, stored in the palette buffer; if `site.hi & 0x40`:
`2E50(site.x, site.y, radius, colour, doppler)` (dotted ring: Doppler -> step 16, Pulse -> step 8). Finally `pal_queue(0xC0, &palbuf[0xC0], B2E6)`. In mode 2 `doppler` is forced to 1 (dashed ring); in mode 1 it comes from the radar flags. Sites whose chosen record has type 0 are skipped, but the colour index `C0h+i` still advances.

### 0000:196A `brief_draw_site_icons` int(char done) (V)
For i in 0..B2E6-1: if !done: `done = 405F(8); snd_cmd(1Ch)`. A = site `B286[2i]`; `(x,y) = (6D62[4i] + 6, 6D64[4i] + 5)` i.e. centre of button box i. If `B287[2i] == 0` (single): sprite = A.type != 0 ? ( (A.f8 & 8) ? E09A : (B2E8 == 1 ? D08E drawn at x-1 : E082) ) : D29E (airbase); draw with `3693(DC18,x,y,seg)` and, if !done, also on DC02.
If paired (B != 0): mode 1 -> radar sprite D08E at (x-2,y) and airbase sprite D29E at (x+3,y); other modes -> E082 at (x-1,y) and D29E at (x+2,y); each drawn on DC18 and (if !done) DC02. Returns done.
Sprite segment variables: D08E radar-site icon (U), E082 SAM icon (U), E09A special icon (U), D29E airbase icon (U), E09C target marker (U), D88E depart/land marker (U), CAC4/CABA/D330 other icons (U).

### 0000:1AB6 `brief_draw_hostile` void(void) (V, contains an original bug)
`res = 0x64; for (a = 0; a < 16; a++) for (b = 0; b < 16; b++) if (DS:CAD8[a + 16*b] & 0x10) { res = 0x65; b = b*2047/195; a = a*2047/146; 3693(DC18, x=a, y=b, [CAC4]); }`
The scaled values overwrite the loop counters (`[bp-4]`, `[bp-6]`), so after the first hit the loops end almost at once -- reproduce exactly (at most a couple of sprites). Then `brief_hint_text(res)`.
Grid DS:CAD8 = 16x16 bytes "hostile concentration" map, bit 0x10 = concentration.

### 0000:1B37 `brief_hint_text` void(int h) (V)
`DS:DC1C = 0xE2; gfx_fill_rect(DC18, 87,180,234,196)`; font 4, fg 15, DC1E = 8; text by h: 1 DS:0327 radar, 2 DS:035E SAM, 3 DS:03C9 hostile, 4 DS:03FE all ranges, 5 DS:043B erase ranges, 6 DS:0393 description, 7 DS:047A armaments, 8 DS:04B6 different mission,
0x65 DS:04E5 "Hostile forces have concentrated in the indicated areas.", 0x64 DS:051E "No special activity on the part of the enemy has been reported.", other DS:055E "Press Escape button to return to Ready Room.";
`3776(DC18, text, 87, 180, 146, 8, 1)` (centred in a 146 px line); `blit_rect(1,87,180,0,87,180,146,16)`.

### 0000:1C2F `brief_site_info` void(int n) (V)
Builds a text from button n's sites: for k=0,1: s = site `B286[2n+k]`: if `s.type != 0` -> radar record R, if `s.f8 & 1` -> airbase record Ab. Text (strcat):
airbase: `"AIRBASE DATA:\n"`, `"LOC: %s\n"` (locname), status line (`hi&1` "STATUS: Neutral\n", `hi&4` "STATUS: Friendly\n", else "STATUS: Hostile\n"); if `Ab.f0C != 0`: `"FGTR TYPE: %s\n"` (DS:1708 + 20h*Ab.fA) + `"SQDRNS: %d\n"`.
radar: if Ab: DS:05F2 (blank line) ; `"RADAR DATA:\n"`; if no Ab: `"LOC: %s\n"` and neutral line (if `R.hi&1`); `"TYPE: %s, %s\n"` (name DS:15C6+0Eh*t, "Doppler"/"Pulse"); `"RDR RNG: %dkm %s\n"` ((max) when B284 == 1, else (avg)); `"MSL RNG: %dkm (max)\n"`;
`"NOTE: Believed to be\nunder repair"` if `R.f8&0x40`; `"NOTE: Occasionally is\noff the air"` if `R.f8&0x20`. Shown by `2B1B(text, E08A+16, E08C-16, font 3, center 0, w 120)` (box near the cursor). Strings at DS:058B..06AE.

### 0000:1F3F `brief_build_site_buttons` void(void) (V)
Rebuilds the dynamic button entries (DS:6D62, 8 B each) and `B286/B287`: for every site i (type != 0 or f8&1 or hi&2, and not hi&8): clear ring bit if type != 0; find a partner j != i with the same `w0` and the same filter (a pair radar+airbase at one place) unless
(`f8 & 8` and not `hi & 4`: then no partner); box centre = site screen position, or the mid-point of the pair `((Xi+Xj)/292, (Yi+Yj)/390)`; only the lower-indexed site of a pair creates a button: box = (max(cx-7,0), max(cy-7,0)) 14x14,
`B286[2m] = i`, `B287[2m] = (partner < count) ? partner : 0`, m++ . At the end `DS:B2E6 = m`, `DS:6D21 = m + 8`.

## Open items
1. `DS:0030`/comm+30h: START writes `pilot.mission_type > 1` there at exit; RE_GUIDE says SETUP puts the joystick flag in +30h. Check which program reads it (VGAME training flag vs joystick) in S3.6.
2. Palette colours: exact meaning of RGB triples (naming above is from the numbers); page 2 content in the quiz and in message boxes (`requestr.pic` frame art vs save-under buffer) (U).
3. `comm+2Eh` (the fuel estimate in lbs, not a payload; computed by the mission generator at `4F51` as `12 x route score`, (corrected 2026-10-13, see HANDOFFS C10)) is displayed but not computed in this section; `DS:1EC5` is "quantity" by inference.
4. Site flag bits +8/+9 named by use only; 0x08 in `1F3F`/`196A` (special sprite E09A and partner rule) needs a mission-data look (U).
5. `ui_hangar_anim` return value and the exact final calls (see section 2) -- unused by the caller.
6. `ui_ready_room`: which homescrn objects the 5 hotspots are (U-check on screen; mapping by dispatch is verified).
7. `3517` first argument is a DAC index (not "from the top") for all uses seen; `pal_apply_queue` semantics belong to the video reader.
8. In `brief_draw_route` the letters of the labels (DS:031B.. "T,L","T","L","P","S") and which one goes with which point (U).
9. The bug in `brief_draw_hostile` (loop counters overwritten) must be kept for a faithful port; verify visually with a mission that has concentrations (U).

## DGROUP globals seen

| DS offset | type | meaning | readers/writers |
|---|---|---|---|
| 0042 | char[9] | "temp.wld" | ui_main |
| 004B/0050 | char[] | "%s%s" / "%d" | roster_home_card_draw |
| 0054..0068 | byte[] | difficulty weight tables (mission type 54, skill 58, crash 5C, theatre 60) | roster_difficulty_rating |
| 006A | word[6][13+] | hangar sprite-strip records sx,sy,dx,dy,w,h | ui_hangar_hook, ui_hangar_anim |
| 0106 | word[6][4] | hangar upper-strip records | ui_hangar_anim |
| 0136 / 0148 | s8[18] | frame index lists for the two strips | ui_hangar_anim |
| 01C4 | byte[16] | candidate weapon ids for random unavailability (FF = none) | arm_roll_unavailable |
| 01D6 / 01E6 | word[8] | bay box x / y per (flight_model*4+slot) | arm_draw_bay |
| 0232 / 0246 | word[10] | weapon picture sx / sy in weapons1/2 | arm_show_weapon_info |
| 0CBA | word[33] | picture name pointers | ui_show_screen (35FC) |
| 11AC..11E3 | byte[] | 6-bit RGB triples used as UI colours | all hooks |
| 15C6 | struct[14B] | radar type table | brief_* |
| 1708 | struct[32B] | fighter type names | brief_site_info |
| 1AA8 | struct[18B] | SAM/weapon type table | brief_* |
| 1D78 | word[7] | rank string pointers | roster_home_card_draw |
| 1EC5 | byte[20] | bay quantity by weapon row | arm_screen_run |
| 6BC6 | table | quiz buttons (18) | quiz_run |
| 6C5A | table | arming buttons (24) | arm_screen_run |
| 6D1E | table | briefing buttons (8 + dynamic) | brief_* |
| 6F42 | table | ready room hotspots (5) | ui_ready_room |
| 7200, 73A8 | dialog | "need briefing" OK / "ready for your mission?" Yes, Not yet | ui_hangar_launch |
| 936A | word[20] | aircraft names (quiz) | quiz_draw |
| 9ABE, A155, 9522, 92FA, 94D0, 94AC | word[] | pilot-option string tables (theatre, war, mission, skill, crash, flight model) | roster_home_card_draw |
| A587 | word[20] | weapon description strings | arm_show_weapon_info |
| 64A6/64E2 | rec[6B], byte | palette queue and its count | pal_queue (3517), pal_apply_queue |
| 64E3 | byte[768] | palette buffer 6-bit RGB | fades, palette tricks |
| B26F/B270 | byte[18] | quiz: button -> aircraft index | quiz_draw/quiz_run |
| B282 | word | quiz answer | quiz_* |
| B284 | byte | range mode 0 = avg, 1 = max (space toggles) | brief_* |
| B285 | byte | currently highlighted mode button | brief_screen_run |
| B286 | byte[2][n] | site A/B index per site button | brief_* |
| B2E6 | word | number of site buttons | brief_build_site_buttons |
| B2E8 | byte | map mode 1 radar, 2 SAM, 3 hostile | brief_* |
| B2F6/B2F8/B2FC/B302 | word | message box rect (x,y,w,h) | 2B1B, 3588 |
| CAB0 | word[5] | page segment table | 35FC |
| CAC0, CBD8 | far ptr | -> 0000:04F4, 0000:04F2 | 7387, ui_main, ui_ready_room |
| CAC4, CABA, D330, D08E, D29E, E082, E09A, E09C, D88E | word | sprite segments | brief_draw_* |
| CACA/CACC | far ptr | active pilot record = comm+1272h | everywhere |
| CAD8 | byte[256] | hostile concentration grid | brief_draw_hostile |
| CBDE | rec[16B] | site table | brief_* |
| D096 | byte | previously hovered button (all hover hooks) | quiz_hover_hook, arm_hover_hook, brief_hover_hook |
| D2A2 | byte | mission loaded / scheduled flag | many |
| D33C | word | number of sites | 1F3F |
| D878 | byte[20] | weapon unavailable flags (D878/D879 = flight model) | arm_* |
| D88E | word | marker sprite seg (depart/land) | brief_draw_route |
| D894 | rec[50h] | roster table (pilot name at +2) | roster_home_card_draw |
| DC02, DC18 | TextCtx | page 0 / page 1 draw contexts (DC1C fg, DC1E bg, DC24 font) | all |
| DC70 | word[] | location name pointers | brief_draw_route, brief_site_info |
| E060, E062, E072, E074 | word | site idx: target 1, departure, target 2, landing | brief_draw_route, ui_main |
| E08A/E08C, E08E/E090, E092/E093 | word, word, byte | cursor pos, previous cursor pos, cursor sprite w/h | 39F7, 3989 |
| E096/E098 | far ptr | comm block | everywhere |


---
# Part E: orders

## 20D6 `brief_orders_screen` — mission orders (written by the S3.5 merge; verified)

`void brief_orders_screen(void)`; called from the ready-room/briefing flow (spec_a, `ui_main` case 6 = orders).
Let `p` = pilot record (DS:CACA), `site(i)` = 16-byte site table at DS:CBDE-based (`[E062<<4 - 3422h]` = site name
ptr override, `[E062<<4 - 3414h]` byte = default name index), `names` = DS:DC70 (word ptr table, `[idx*2 - 2390h]`),
TextCtx `c = DS:DC5A` (`[c+0Ch]` = font 4 then 3, `[c+4]` = 0 / FFFFh).

1. `ui_load_screen_pic(3, 4)` (FOLDER1.PIC, slot-3 name, page 4). `c.font=4`, `DC60 = FFFF`.
2. `base_name` = site-name pointer of the departure base `E062` (override ptr if non-zero, else names[byte]).
3. Header `ui_text_centered(c, "416th Tactical Fighter Squadron", 0xA8, 0x25)`; title by `p+3Ch` (mission type):
   0 "Air Combat Mission Orders" (DS:6F2), 1 "Ground Strike Mission Orders" (70C), 2 "Air Training Orders" (729),
   3 "Ground Strike Training Orders" (73D), centred at (0xA8, 0x2D); then `c.font=3`.
4. `"Pilot: %s%s."` rank string (`DS:1D78[p+20h]`: "2nd Lt ", "1st Lt ", "Capt ", "Major ") + name `D894+p[0]*50h` at (0x26,0x36).
5. `"%s - "` + base name is formatted into a buffer, `brief_date_string(buf+strlen)` (25D9) appends the date, and `ui_text(c, buf, 0x26, 0x43)` draws it (the `"emp.wld"` annotation in the disassembly is only the immediate 0x43 = y).
6. `mission_brief_text(c, 0)` and `(c, 1)` (24F7: objective text, primary/secondary), then `pal_fade_out(1)`;
   `pal_set_slot(3)` (826C = palette 3 = FOLDER1); full-page blit `slot 42` args (page 4 → screen, 320x200, 0,0,0,0,0, 4)
   [the 8 pushes are the standard 0Fh-arg screen copy; see video.md slot 42]; `pal_fade_in(1)`; `ui_wait_click()`.
   Result 2 (Esc/button 1) skips to step 9.
7. Page 2 of orders: `ui_load_screen_pic(9, 4)` (FOLDER2.PIC); blit rect (0,0,0x26,0xE..) from slot 42 as before
   (clip 0x104 wide, y 0x0E-0xBA), `"FLIGHT PLAN:"` at (0x26,0x12), `"Depart from %s at %s hours."`
   (base name, clock string DS:0D88 "00:00" filled by mission_site_setup) at (0x26,0x1E), wrapped route text
   `ui_text_wrap(c, DS:A361 flight-plan text, 0x26, 0x2A, 0x104, 8, 0)`; then `c.x/[+4]=0, [+6]=FFFF`;
   landing base = site `E074` (same override/default rule); `"Land at %s."` (97) at (0x26,0x46)... as `ui_text`.
   `"RULES OF ENGAGEMENT:"` (0x26,0x56); ROE paragraph = `DS:9781[p+3Ah]` (3 strings: 0 clandestine / 1 "All military
   targets may be attacked freely..." / 2 "A full state of war exists...") wrapped at (0x26,0x5E) width 0x104 line 8.
   `"SUGGESTED ARMAMENT:"` (0x26,0x8C). Weapon lines from `DS:A409[idx*2]` (weapon names, e.g. "4X AIM-9M SIDEWINDER"):
   `CAD2` at (0x40,0x94), `CAD3` at (0xB4,0x94); if `p+42h == 0` (aircraft model 0 = F-117A) also `CAD4` at (0x40,0x9C)
   and `CAD5` at (0xB4,0x9C). `"Est. Fuel required: %d lbs."` with `comm+2Eh` at (0x26,0xA8).
8. `pal_fade_out(1)`; blit page 4; `pal_fade_in(1)`; `ui_wait_click()`.
9. Exit: `pal_fade_out(1)`; `ui_load_screen_pic(p[38h]+0x10, 4)` (theatre map PIC, name index = theatre+16);
   blit; `pal_set_slot(2)` (BREFSCRN palette); copy 3 bytes from DS:11C7 over `DS:(-(B2E8*3) - 67E3h)` (colour 0
   marker patch: writes the 3-byte DAC triple at `64E3 + ...`; (U) it restores one palette entry, index from `[B2E8]`);
   `D096 = 0`; `pal_fade_in(1)`; `ui_message(0x110, 0x86)` (3E84 = print/bearing popup at the map).
10. Returns void. Original bug-free; no globals other than those listed are written.

Open (U): destination of the 3-byte patch at 24C2..24D6 (source DS:11C7, index from `[B2E8]`).


---
# Part B: UI toolkit and briefing text

### S3.5 reader b: START.EXE 0000:24F7 .. 4238 (first function at/after a.txt line 3500 to the end of a.txt)

Source: `work/s35/a.txt` lines 3839-6850. Conventions: (V) verified in disasm, (U) needs visual check / unclear.
Arguments are listed in C order (cdecl, pushed right to left, caller pops). `slot N` = MGRAPHIC/MISC/sound far call
`DGROUP:69B8+5*N`: MGRAPHIC 0-83, MISC 90-95, sound 100-104 (RE_GUIDE 2.3).
Slots used here: 5 text_noclip(TextCtx*,char*) | 22 sprite_scaled | 42 blit_rect(spage,sx,sy,dpage,dx,dy,w,h) |
47 font_char_width(ch,font) | 70 screen_on | 90 kbd_empty (0 = key waiting) | 91 kbd_get (AH scan, AL ascii) |
93 joy_button(n) | 101 snd_cmd(n). Page numbers: 0 = screen A000, 1 = back buffer (clean copy of the screen),
2 = scratch page (popup box + saved background + 2x2 corner tiles), 3 = clean background for the animation (V).

Shared structures inferred here
* `TextCtx` = video.md section 3 (22 bytes): +0 page, +2 flags, +4 fg, +6 bg/shadow colour, +8 x, +A y, +C font.
  Three live statics at DS:DC02 (page 0), DC18 (page 1), DC2E (page 2 / text scratch, font set per call). They are
  also used as `Win` for `gfx_fill_rect` (+0 page, +4 colour) and as `SpriteCtx` for slot 22 (V; page values (U)
  because they are filled in code outside this range).
* `Rect` = 4 words {x0,y0,x1,y1} (8 bytes, inclusive) for hit testing.
* `Dialog`/`MenuDesc` descriptor (data in DGROUP 70AA..73A8; V):
  `+0 msg*, +2 button1*, +4 button2* (0 = single button), +6 w (out), +8 h (out), +0A cb (always 3DA2),
  +0C hotkey byte (0), +0D button count (1 or 2), +0E..+1C two Rects (out: button1 at +0E, button2 at +16)`.
  `ui_dialog_run` copies 84h bytes from +0Ah to DS:B308, so B308 = cb, B30A = hotkey, B30B = count, B30C.. = rects
  (V). `ui_menu_loop` is passed a pointer to this copy (field offsets cb+0, hotkey+2, count+3, rects+4).
* `ObjRec` (mission objective, 12h bytes at DS:E05E + 12h*n, n = 0 primary, 1 secondary; V from 24F7/2651/40F0):
  +2 target index into the site table (primary = [E060], secondary = [E072]), +8 objective id (index into the
  11DE table and the 1F24 text table), +0A char[8] ONC grid string (e.g. "XY00").
* Site record 16 bytes at DS:CBDE (count [D33C]): +0 word name index (non-zero = has a place name), +6 word
  (0 = site gone), +8 word flags (0100h, 0400h tested; 40h/80h set by `mission_site_setup`), +0E byte noun name
  index. Names come from the pointer table DS:DC70 (filled at run time, `[DC70+2*idx]`) (V).
* `comm` = far ptr DS:E096 (off 0 / seg from 0000:04F0, set at 7387). `pilot` = far ptr DS:CACA (= comm+1272h).
  DS:CBD8 is NOT comm: it is a far ptr to 0000:04F2 (the "one-time setup" word) and DS:CAC0 -> 0000:04F4 (V, d.txt 7387).

---
## 1  Briefing text (24F7, 25D9, 2651)

### 24F7 `brief_draw_target_summary(TextCtx *c, int which)` (V)
Called from the mission orders function 20D6 (two call sites, which = 0 / 1).
1. `rec = E05E + 12h*which`; `c->font = 3`, `c->colour = 0`.
2. Heading with `ui_text(c, s, 0x26, y)`: which 0: "PRIMARY TARGET INTELLIGENCE SUMMARY:" at y = 4Eh; which 1:
   "SECONDARY TARGET INTELLIGENCE SUMMARY:" at y = 8Ch.
3. Intro paragraph: `ui_text_wrap(c, objective_text_tbl[rec->objective_id], 0x26, y+8, 0x104, 7, 0)` (word wrapped, left aligned,
   line height 7).
4. `y = c->y + 2`; `c->colour = 4`; draw "OBJECTIVE:" at (0x26, y); `c->colour = 0`; build objective sentence in a stack
   buffer with `brief_format_objective(rec, buf)`; `ui_text_wrap(c, buf, 0x26, y+8, 0x104, 6, 0)` (line height 6).
Strings: "PRIMARY TARGET INTELLIGENCE SUMMARY:", "SECONDARY TARGET INTELLIGENCE SUMMARY:", "OBJECTIVE:".

### 25D9 `char *brief_format_date(char *buf)` (V)
Campaign date for the pilot's current theatre. `t = pilot+38h` (theatre), `month = date_start_month[t]` (DS:882, byte),
`year = date_year[t]` (DS:896, words), `day = date_start_day[t] (DS:88C) + pilot+36h` (days elapsed). Normalise:
`while (days_in_month[month] < day) { day -= days_in_month[month]; month++; }` (table DS:8C2, index 1..12; Feb = 28);
then once: `if (month > 12) { month -= 12; year++; }`. `sprintf(buf, "%s %d, %d", month_names[month], day, year)`
(month names DS:8A8 words, index 1 = "Jan"). Returns buf. Data: start months 4,9,6,8,2,11,1,10,6,0; start days
15,23,15,14,13,22,15,10,7,0; years 1986,1984,1985,1986,1994,1989,1991,1995,1997,2119 (V from the image).

### 2651 `void brief_format_objective(ObjRec *rec, char *out)` (V)
Builds the single sentence shown under OBJECTIVE:. `tgt = CBDE + 16*rec->target`; `place = tgt->name_idx ? names[tgt->name_idx] : 0`
(names = DS:DC70 pointer table). `kind = byte at DS:11DE + 12*rec->objective_id` (11DE table = 12-byte entries:
byte 0 kind, word +4 flags, word +6 extra). Switch on `kind - 1` (jump table at cs:284E, 8 entries; V):

| kind | targets | output |
|---|---|---|
| 1, 2 | 2696 | verb = kind==1 ? "Photograph" : "Destroy". With a place: `"%s the %s at %s"` (verb, names[tgt->noun], place), else `"%s the %s"`. Then tail by flags word (+4): `&0x20` -> `" before %s hours, while the cargo is still being unloaded."`; else `&0x02` -> `" before %s hours."` (both with the time string DS:D7C); else: strip a trailing ',' from `out` and append "." |
| 3 | 2760 | `"Reach the beacon at ONC %s and drop the supplies before %s hours."` (rec+0A grid, D7C) + `" The beacon will be on ONLY during this period."` |
| 4 | 277D | `"Reach the secret airstrip at ONC %s before %s hours."` + `" The airstrip will be lighted ONLY during this period."` |
| 5 | 279A | `"Destroy the AN-72 Coaler leaving %s airbase at %s hours and arriving at %s airbase at %s hours."` args: name of site `[CBDC]` (its +0 name idx), D88, `place`, D7C |
| 7 | 27CA | `"Intercept and destroy the AN-72 Coaler leaving %s airbase, at %s hours.  Destination unknown."` args: name of site `[CBDC]`... (code pushes D88 and `place`; V) |
| 6 | 27E3 | `"Intercept and destroy the Tu-95, last seen at ONC %s at %s hours. It is probably heading for %s airbase, and will arrive at %s hours."` args: D8E (grid buffer "XY00"), D88, `place`, D7C |
| 8 | 27F7 | `tmp = (word at 11DE+12*id+6 == -2) ? " fighter patrol" : ""`; `sprintf(out, "Intercept and destroy the %s%s at ONC %s.", aircraft_name[D3D4], tmp, rec+0A)`; aircraft names at DS:1708 + 32*idx (MIG-23, MIG-25, MIG-29, MIG-31, Su-27, IL-76 ...) |

The jump table maps kind 1,2 -> 2696; 3 -> 2760; 4 -> 277D; 5 -> 279A; 6 -> 27E3; 7 -> 27CA; 8 -> 27F7 (V; the
disassembler shows the table as garbage). D7C, D88, D8E are runtime text buffers ("00:00", "00:00", "XY00" in the image),
filled by the schedule code (149C, outside this range) (U which is departure / arrival: D88 is the departure time in kind 5, D7C arrival).
Caller 149C also uses it (route page). Max output about 0x58 bytes.

---
## 2  File + palette helpers

### 2864 `unsigned ui_load_file_far(char *name, unsigned off, unsigned seg)` (V)
`fd = dos_open(name, 0)`; `n = dos_read_far(fd, 0xFFFF, off, seg)`; `dos_close(fd)`; returns n. Callers: 35FC (PIC files into the
file buffer D338:D336), 73CE (palettes.pal). 289A `ui_load_file_near(name, buf)` same with `dos_read(fd, 0xFFFF, buf)` (caller 7611, near).

### 35FC `ui_load_screen_pic(int name_idx, int page_slot)` (V)
`sprintf(tmp, "%s.pic", tbl_pic_names[name_idx])` (names DS:CBA: air_id armsscrn brefscrn folder1 hangscrn homescrn ordrscrn rostscrn ...);
`ui_load_file_far(tmp, D336, D338)`; `pic_to_seg_b(E086, page_seg_tbl[page_slot])` (CAB0 words; D338 == E086 == buffer segment, offset 0). Does NOT touch the palette.

### 3641 `void ui_show_screen(int name_idx, void (*overlay)(void))` (V)
`pal_fade_out(1)`; `ui_load_screen_pic(name_idx, 1)`; `pal_select(name_idx)` (826C: palette #n from palettes.pal, n clamped to 8, into
pal_cur 64E3, 768 B each); `blit_rect(1,0,0,0,0,0,320,200)` (page 1 -> screen); `if (overlay) overlay()`; `pal_fade_in(1)`;
slot 70 `screen_on`. Callers: 020F 038C 0703 0963 5BF0 623E (all the shell screens). The overlay draws text on the screen (page 0) while the
picture is displayed and the palette is still black.

### 3517 `pal_queue_push(int first, void *rgb, int count)` (V)
`e = 64A6 + 6*[64E2]; e[0]=first; e[1]=rgb; e[2]=count; [64E2]++` (no bounds check; room for 10 entries up to 64E2). `pal_apply_queue` (8427) waits one timer tick (sets [ADFC]=0, spins until the ISR sets it), then
for every entry `INT 10h AX=1012h BX=first CX=count ES:DX=rgb` and decrements [64E2] (V, rawdis). 3548 = 3517 + `snd_cmd(0x1A)` (no callers).
Because the DAC is only written in the tick wait, always call `pal_apply_queue` after a push.

### 3070 / 3088 / 30A0 `pal_fade_in(speed)`, `pal_fade_out(speed)`, `pal_fade(pal, from, to, speed)` (V)
`pal_fade_in(s) = pal_fade(64E3, 0, 0x100, s)`, `pal_fade_out(s) = pal_fade(64E3, 0x100, 0, s)`. Callers: 3641, 1308, 20D6, 0448, 0F4E, 7611.
Algorithm of `pal_fade`:
1. Wait until the palette queue is empty (`while [64E2] pal_apply_queue()`).
2. If the timer ISR is installed ([ADFB]): `while (snd_cmd(2) != 0);` then `timer_remove()` (restores INT 8 / 18.2 Hz so the BIOS tick at 0040:006C is real). `[ADFB]` is not cleared; at the end `timer_install()` (8CA8).
3. `steps` pending counter = 0. If `speed == 0` jump straight to the final value; else `cur = from`.
4. Loop: build a 768-byte scratch palette `out[i] = (src[i] * cur * cur) >> 16` (src bytes signed-extended, long multiplies, quadratic ramp, cur 0..0x100 -> v*(cur/256)^2); `cur = 0x100` gives the source unchanged.
   If `[B304] == 0` (first fade ever): `pal_fade_calibrate(out)` sets `[B304]` = number of `pal_set(out)` calls that fit in 4 BIOS ticks (the loop `while (tick+4 > now) { pal_set(buf); [B304]++; }`) and the loop continues with the first computed palette.
   Else `pal_set(out)` (8364) is the write; if `cur == to` stop. On the first iteration set `n = [B304]*speed` (if 0 then divisor 0xFF) and `delta = (to - from)/n`; each iteration `n--; if n > 0: cur += delta`, else `cur = to` (final frame).
   Duration about `speed * 4` BIOS ticks (about 0.22 s per speed unit at 18.2 Hz) (U on real hardware; the port can use a fixed time per speed unit).
5. Port note: the fade only needs `pal_set` per frame; the timer juggling is a DOS artefact (drop it).

---
## 3  UI toolkit (the shell screens are built from these)

### 3567 `ui_text(TextCtx *c, char *s, int x, int y)` (V)
`c->x = x; c->y = y; slot 5 (text_noclip)(c, s)`. Callers: very many.
### 373C `int ui_text_width(TextCtx *c, char *s)` (V)
Sum of slot 47 `font_char_width(ch, c->font)` over the string.
### 36C4 `ui_text_shadow(TextCtx *c, char *s, int x, int y)` (V)
If `c->bg != -1` (field +6): copy the ctx, set its fg (+4) to `c->bg`, draw s at (x+1,y), (x,y+1), (x+1,y+1) (three offset copies), then draw s normally. If bg == -1 only the normal draw.
### 3776 `int ui_text_wrap(TextCtx *c, char *s, int x, int y, int width, int line_h, int centre)` (V)
Word wrapping into `width` pixels. Per line: scan chars until the accumulated slot-47 width exceeds `width` or NUL/'\n' (0Ah); if over width back up to the previous space (or after a '-': a hyphen is a break point and is kept on the line); copy the line (<=0x100 bytes) to a stack buffer, measure it with `ui_text_width`, track the maximum; unless `c->page == -1`, draw it with `ui_text_shadow` at `x` (centre != 0: `x + (width - lw)/2`, y = c->y). Then `c->y += line_h` (also for a '\n' on an empty line), skip the separator space, continue until NUL. Returns the widest line (pixels). `c->page = -1` therefore gives a measure-only pass (this is how `ui_popup_text` and `ui_dialog_draw` measure) and c->y ends at the y of the next line. First thing it does: `c->y = y`.
### 2A84 `ui_text_centered(TextCtx *c, char *s, int cx, int y)` (V)
`c->x = cx - ui_text_width(c,s)/2; c->y = y; text_noclip`. Callers 20D6, 615C, 64E5.
### 3693 `ui_sprite_draw(SpriteCtx *c, int x, int y, unsigned seg)` (V)
slot 22 `sprite_scaled(c, x, y, dw = word at seg:2?, ...)`: pushes `es:[2]` as the 5th and `es:[0]` as the 4th argument, off = 0: i.e. the sprite header words (w at +0, h at +2) are the destination size = natural size. Callers 39F7 (cursor), 141D 149C 196A 1AB6 5A8A 5BF0 6DB8 (map icons).
### 2B06 `ui_wait_ticks(int n)` (V): `while (n--) pal_apply_queue();` (one timer tick each).

### 2CB4 `ui_draw_frame(TextCtx *c, int x, int y, int w, int h)` (V, look (U))
Bevelled panel on `c->page`. Four 2x2 corner tiles copied with slot 42 from page 2: TL (0,0), TR (2,0), BL (0,2), BR (2,2) to the four corners; then with `gfx_fill_rect(c,...)` (8A32; `c->colour` at +4) the four edges and the interior: colour 0xE0 top edge (x+2..x+w-3, y..y+1), 0xE6 bottom, 0xE1 left, 0xE5 right, 0xE2 interior (the code sets c+4 before each call). The E0..E6 range is a palette ramp (U colours). Callers 2B1B, 2DF2, 332E.
### 2DF2 `int ui_draw_button(char *s, int cx, int y, int colour)` (V)
`DC3A = 1` (font 1 in win_text); `w = ui_text_width(&win_text, s) + 8`; `ui_draw_frame(&win_text, cx - w/2, y, w, 16)`; `DC32 = colour` (fg); `ui_text_shadow(&win_text, s, cx - w/2 + 4, y + 4)`; returns w. (Button colour 0xE7 / 0xE8 from the dialog code; `win_text.page` is page 2 (U) because 332E blits it from page 2.)

### 2B1B `ui_popup_text(char *s, int x, int y, int font, int centre, int wrap_w)` (V)
Framed message box (used by 0703 0963 1308 149C 1C2F for help/status text).
1. `win_text.font = font`; local ctx copy with page = -1; `w = ui_text_wrap(local, s, 0, 0, wrap_w, 8, 0)`; `ui_popup_w = w + 12`; `ui_popup_h = local.y + 12`.
2. `ui_popup_x = centre ? x - ui_popup_w/2 : x`, clamped to 0..320-w; `ui_popup_y` likewise with 200 (y).
3. Save background: slot 42 `blit_rect(1, px, py, 2, sx, sy, w, h)` where `(sx,sy) = (w,4)` if h > 0x62 else `(0, h+4)` (page 2).
4. `ui_draw_frame(&win_text, 0, 4, w, h)` (box built in page 2 at (0,4)); `win_text.fg = 15, bg = 8`; `ui_text_wrap(&win_text, s, 6, 10 .. i.e. y=0xA, w-12, 8, centre)`.
5. `blit_rect(2,0,4,1,px,py,w,h)` and `blit_rect(2,0,4,0,px,py,w,h)` (page 2 box -> back buffer and screen).
`ui_popup_restore()` (3588) reverses it: `(sx,sy)` as in step 3, `blit_rect(2,sx,sy,1,px,py,w,h)` then to page 0. `ui_popup_restore_box(Box *b, int x, int y)` (2AB4, caller 3CEA) is the same for the dialog (b->w at +6, b->h at +8, source (0, b->h+4) only; both pages).

### 332E `ui_dialog_draw(Dialog *d, int x, int y)` (V)
Same scheme as 2B1B but for descriptors: font 4; measure `d->msg` with width `d->w`(in) - 12... (input +6 is overwritten: it measures with `[d+6]-0xC` taken before; the caller zero-initialises w) -> `d->w = textw + 12`, `d->h = text_h + 0x1A`; frame at (0,4); fg 15 / bg 8; message drawn centred at (6,8) width w-12, line height 8; buttons: if `d->btn2`: centres at `(w-4)/3 + 2` and `2*(w-4)/3 + 2` (colour 0xE8 for btn1, 0xE7 for btn2), else one centre `(w-4)/2 + 2`; each `ui_draw_button(label, cx, text_h+10, colour)`; button hit rectangles stored in the descriptor (screen coords): btn1 `+0E x0 = x + cx - w/2`, `+10 y0 = y + h' + 6`, `+12 x1 = x + cx + w/2`, `+14 y1 = y + h' + 0x16`; btn2 at `+16..+1C` (only if btn2). Finally `blit_rect(2,...)` to pages 1 and 0 at (x,y) with the saved background kept in page 2 at (0, h+4).
Strings: see the Dialog table below.

### 3CEA `int ui_dialog_run(Dialog *d, int x, int y)` (V)
Callers 038C 5BF0 623E 7272 (+ many via the dialog data). Saves `[D096]` and the cursor pos (E08A,E08C). Sets the two colour entries E7/E8 (pal_cur 6798/679B) to the normal colour 3Bh,3Ch,39h ((57,60,59) bytes "9<;" at DS:11C4) and queues `pal_queue_push(0xE7, 6798, 2)`; `pal_apply_queue`; `ui_dialog_draw`; copies 84h bytes `d+0Ah` -> B308; `ui_cursor_set_pos(B310-3, B312-3)` (the bottom-right of button 1); `do r = ui_menu_loop(B308) while (r == 0x63)` (Esc is ignored); `ui_wait_ticks(6)`; `ui_popup_restore_box(d,x,y)`; restores `[D096]` and the cursor position. Returns r: 1 = button 1 (OK / Yes), 2 = button 2, 100h+key if the hotkey byte matched (never set: byte is 0).
### 3DA2 `ui_dialog_hover()` (V) -- the `cb` of every dialog
`i = ui_hit_test(B30C, [B30B])`; if i != 0: highlight colour 3F,32,2B at pal[0xE7 + (2 - i)]... exactly: bytes `[679E - 3*i]` := (63,50,43) "?2+" (DS:11BE) and `[6795 + 3*i]` := normal; else both entries normal. `if ([D096] != i) { [D096] = i; pal_queue_push(0xE7, 6798, 2); }` Button 1 uses DAC 0xE8 (679B), button 2 DAC 0xE7 (6798). No code callers: the address is in data (9 descriptors).

### 38AC `int ui_menu_loop(MenuDesc *m)` (V)
Callers 020F 0703 0963 0FA3 3CEA 5BF0 623E 6DB8. Loop: `if (comm->flags72 & 1) joy_poll();` `pal_apply_queue();` `if (m->cb) m->cb();` `k = ui_poll_input(m);` switch k:
* 0: nothing;
* 1 (Enter / button 0 / mouse button 0): `i = ui_hit_test(m->rects, m->count)`; if i: `snd_cmd(0x1A)` and result = i; else `snd_cmd(0x22)` (click on nothing);
* 2 (Esc / button 1): result = 0x63, `snd_cmd(0x23)`;
* other: if `k == m->hotkey` result = 0x100 + k.
Until result != 0; returns it. (A 0 hotkey never matches because k == 0 is the "nothing" case.)

### 393D `int ui_hit_test(Rect *r, int n)` (V)
For i in 0..n-1: `if (r->x0 <= cx && cx <= r->x1 && r->y0 <= cy && cy <= r->y1) return i+1;` with (cx,cy) = (E08A,E08C), rects 8 bytes {x0,y0,x1,y1} (unsigned compares). Else 0. Callers 0786 0B01 0FA3 11DB 38AC 3DA2 5E6F 623E 63FB 71AD.

### 39F7 `int ui_poll_input(MenuDesc *m)` (V) -- the input + cursor engine
1. `a = b = 0`. If `comm->flags72 & 1` (joystick): `a = joy_button(0)`, `b = joy_button(1)` (slot 93).
2. If `comm->flags72 & 2` (mouse): `mouse_poll()` (8545, updates E08A/E08C), remember old = (E08A,E08C), `a |= mouse_button(0); b |= mouse_button(1)` (8530).
3. `key = 0`; `if (kbd_empty() == 0) key = kbd_get();` (slot 90 returns 0 when a key waits; 91 returns scan<<8|ascii) else `if (a) key = 0x0D; else if (b) key = 0x1B;`.
4. Switch on key: 0 and joystick: move the cursor `E08A += (jx - 0x80)/0x1E; E08C += (jy - 0x80)/0x1E` (axis bytes DS:69B2 / 69B3, 0..255 centred on 0x80); 0x1000 (Alt-Q): `quit_prompt()` (7611); arrows 0x4800 up, 0x4B00 left, 0x4D00 right, 0x5000 down: **jump to the nearest rect centre in that direction**: for every rect of `m->rects` (count m->count, 8-byte rects at m+4): centre `(cx,cy) = ((x0+x1)/2, (y0+y1)/2)`, candidate if it is strictly on the correct side of the cursor and `|dy| >= |dx|` (up/down) or `|dx| >= |dy|` (left/right); choose smallest `dist_approx(E08A,E08C,cx,cy)` below 1000; if found cursor = that centre. Other keys: no cursor change.
5. Clamp the cursor to `[DC2A..DC2C]` x and `[DC26..DC28]` y (words xmin, xmax; ymin, ymax in that memory order DC26 ymin, DC28 ymax, DC2A xmin, DC2C xmax).
6. If mouse present and the position changed since step 2: `mouse_set_pos()` (85A7, writes E08A/E08C back to the driver).
7. Redraw: `blit_rect(1, E08E, E090, 0, E08E, E090, E092, E093)` (erase: clean page 1 -> screen) then `ui_sprite_draw(&win_page0, E08A, E08C, E088)`; `E08E,E090 = E08A,E08C`; `ui_check_break()`.
8. Result: key == 0x5300 (Del) or low byte 8 (BS) -> 0x62; low byte (high byte cleared) == 0x0D and previous key (B306) != 0x0D -> 1; == 0x1B and previous != 0x1B -> 2; `B306 = key`; if result still 0 result = key (low byte unless unmodified word: the function stores the full key word in B306 but returns the AL-masked value because `[bp-0x15]` is zeroed) (V for the 0x62/1/2 rules; (U) whether returned plain keys are ascii only: the high byte is cleared at 3CA3 only when key != 0x5300).
So held buttons / Enter produce only one "1": repeated Enter returns the raw key 0x0D afterwards (which never equals a hotkey).
Cursor visual: sprite drawn only on page 0, never on page 1.

### 3989 `ui_cursor_erase()` (V): the erase blit of step 7 alone. Callers 020F 038C 0703 0963 0FA3 5BF0 623E 6AB3 6DB8 (before changing the screen).
### 3E20 `ui_cursor_init(unsigned seg)` (V): `E088 = seg; E092 = byte[seg:0]; E093 = byte[seg:2]; E094 = E095 = 2; E08A = E08C = E08E = E090 = 0`. Only caller 74B1. (E094/E095 never read in this range (U)).
### 3E84 `ui_cursor_set_pos(x,y)` (V): `E08A=x; E08C=y; if (comm->flags72 & 2) mouse_set_pos();`
### 3E5E `ui_check_break()` (V): `if ([73F2]) fatal_exit(0)` (76D1; [73F2] = Ctrl-Break hook installed flag).
### 3E6D `ui_kbd_flush()` (V): `while (kbd_empty() == 0) kbd_get();` (no callers).
### 39B3 / 39D5 (V): `ui_order_desc(unsigned *p)`: if `p[2] > p[0]` swap p[0], p[2]; `ui_order_asc`: if `p[2] < p[0]` swap. Callers 07EE and 5A8A only (rect normalisation, probably (U)).

### 3EA5 `int ui_wait_click(void)` (V)
Loop: `pal_apply_queue()`; joystick (`comm->flags72&1`): button 0 -> 1, button 1 -> 2; mouse (`flags72&2`, `mouse_poll()` then mouse_button 0/1) -> 1 / 2; else key: Alt-Q (1000h) -> `quit_prompt()` (7611) then keep waiting; 0x0D -> 1; 0x1B -> 2; other keys ignored. Only caller 20D6.
### 3F33 `int ui_wait_input(int ticks)` (V)
`ticks == 0` means 65536 (the counter is decremented before the test). Phase 1 (only if ticks != 0): wait for all buttons (joystick 0/1, mouse 0/1) to be released, spending ticks. Phase 2: each tick `pal_apply_queue()`, `ticks--`, poll joystick (button 0 -> 1, button 1 -> 2), mouse (same), keyboard (Alt-Q -> quit prompt; Enter -> 1, Esc -> 2, Space (0x20) -> 3); returns as soon as the result != 0 or ticks hit 0; result 0 = timeout. Callers 0448 (twice), 405F.
### 405F `int ui_wait_skip(int ticks)` (V)
`r = ui_wait_input(ticks)`; switch: 0 -> 0; 1 -> `snd_cmd(0x1A)`, 0; 2 -> `snd_cmd(0x23)`, 0; 3 -> `snd_cmd(0x1A)`, **1**. So only Space reports "skip". Callers 0703 0963 1308 149C 196A 2F4C (pauses / animation skip).

---
## 4  Button-highlight animation (28CE, 2901, 2A7E)

State: `B2FA src page`, `B300 rect table`, `B2FE sequence pointer`, `B2F4 steps left`, `B2EA tick counter`, `B2EC,B2EE,B2F0,B2F2` last drawn bounding box.
* `ui_anim_start(page, tbl, seq, steps)` (28CE; callers 5BF0, 6045) (V): if `[B2F4] != 0` return 0 (busy, request dropped); else `B300=tbl; B2FE=seq; B2FA=page; B2F4=steps; B2EA=0; B2F0=0`.
* `ui_anim_active()` (2A7E; callers 5E6F, 5FFA) returns `B2F4`.
* `ui_anim_step()` (2901; callers 5BF0, 5E6F) (V): returns `B2F4` immediately unless `(B2EA++ & 7) == 0` (runs on every 8th call, first call immediately) and `B2F4 != 0`.
  1. If `B2F0 != 0`: restore the previous box: `blit_rect(3, B2EC, B2EE, 1, B2EC, B2EE, B2F0, B2F2)` (clean copy in page 3 -> page 1).
  2. Sequence entry at `*B2FE` = `{first, count}` bytes; rects at `B300 + 12*first`, 12 bytes each = 6 words `{sx, sy, dx, dy, w, h}`. For each of `count` rects: `blit_rect(B2FA, sx, sy, 1, dx, dy, w, h)` (highlight frame from the source page onto page 1) and accumulate the union box (min dx,dy; max dx+w, dy+h).
  3. Union the new box with the previous box (x/y min, max edges) and `blit_rect(1, ux, uy, 0, ux, uy, uw, uh)` (page 1 -> screen).
  4. Save the new box in B2EC..B2F2 (w = max-min stored as `-(min-max)`); `B2FE += 2`; `B2F4--`; return `B2F4`.
Use: the shell buttons flash through highlight cels (page 3 holds the clean background). Mapping of source pages (page 3 is allocated by the shell) is (U).

---
## 5  Map drawing helpers (ONC map screen, callers in 149C/183A, reader a)

### 2E50 `map_draw_range_ring(int wx, int wy, int radius, int colour, int solid)` (V)
Draws on win_page1 (DC18; `DC1C = colour`). Pixel centre `cx = wx / 146 (0x92)`, `cy = wy / 195 (0xC3)` (the world is 146 x 195 units per pixel on the map). `r = radius >> 7`. Angle step (in 1/256 turn): `solid ? 8 : (radius >= 7000 ? 4 : radius >= 3000 ? 8 : 16)`. For `a = 0; a <= 256; a += step`: `ang = (a & 0xFF) << 8`; `px = ui_sin_mul(ang, r)*128 / 146 + cx`, `py = ui_cos_mul(ang, r)*128 / -195 + cy` (north = up). `solid == 0`: plot the point as a 1-pixel `gfx_line(&win_page1, px, py, px, py, colour)` only if px < 320 and py < 200; `solid != 0`: after the first point draw `gfx_line(&win_page1, px, py, prev_x, prev_y, colour)` (not clipped here). Only caller 183A (radar range display, "Press Selector to view ranges of selected radar sites").
`ui_sin_mul(ang, r)` (3301) = `ui_fixmul_q14(ui_sin16(ang), r)` = `r * sin(ang)/16384` (8C7C: sine of a 16 bit angle, 256-entry interpolated table at DS:ABF8, Q14; 8C15: `(a*b*4)>>16`, rounded); `ui_cos_mul` (3318) adds 0x4000.

### 2F4C `int map_draw_route_line(int x1, int y1, int x2, int y2, int skip)` (V)
Animated route line. `dx = |x2-x1|, dy = |y2-y1|`; both 0 -> return 0. `n = max(dx,dy)`; steps `n-1`; in 6-bit fixed point the minor axis step is `(delta << 6) / n` (sign by direction), major axis `+-0x40`. Start `x = x1*64 + 32`, `y = y1*64 + 32`. For each step `i`: `if (skip == 0) skip = ui_wait_skip(1);` (one tick pause, Space = skip rest); `px = x>>6, py = y>>6`; `if (i > 1) { gfx_line(&win_page1, px,py,px,py, 0); if (skip == 0) gfx_line(&win_page0 (DC02), px,py,px,py, 0); }` colour argument is literally 0 (palette index 0, black on the chart: the route is pen-drawn as black dots; (U) confirm index 0 is black in that palette). `x += stepx; y += stepy`. Returns `skip` (1 if Space was pressed). Caller 149C (three calls, for the legs of the route).

### 31FA `map_place_label(char *s, Pt *p1, Pt *p2, Pt *p3)` (V)
Points `{?, x(+2), y(+4)}` in world units; pixel `(x/146, y/195)`. Pixel coords: p1 = (X,Y), p2 = (x2,y2), p3 = (x3,y3). Label origin offsets (dx,dy) relative to p1, first match wins (V): (1) X <= x2 and X <= x3 (p1 leftmost): (-5,-3); (2) x2 <= X and x3 <= X (rightmost): (+3,-3); (3) y2 >= Y and y3 >= Y (p1 highest on screen): (-2,-8); (4) y2 <= Y and y3 <= Y (lowest): (-2,+3); (5) otherwise `(y3-y2)*(x3-x2) > 0` ? (-4,+2) : (+3,-7). Draws `ui_text(&win_page1, s, X+dx, Y+dy)`. Caller 149C only (airfield/waypoint names on the route map).

---
## 6  Mission preparation (40F0) and memory wrappers

### 40F0 `void mission_site_setup(void)` (V; caller 1308)
1. `[CABE] = pilot+3E; [B38E] = pilot+38 (theatre); [B38C] = pilot+3A`; `theatre_load(theatre_prefix_ptrs[theatre])` (5466, reader c: loads `<suffix>.wld/.3dG/.3dT`); then `4239()` (the 3.8 KB generator: reader c/d section b.txt); `[CAD2..CAD5] = comm+38, +3A, +3C, +3E` (bytes; option/difficulty bytes).
2. `[D33A] = 1` if any byte of the 16x16 map `CAD8[row*16+col]` has bit 10h (all 256 cells scanned), else 0.
3. For `i = 0..[D33C]` (inclusive, an off-by-one in the original): `site[i].flags &= 0x3F` (clear 40h/80h).
4. `k = rand() % 3`; while `k != 0`: `j = rand() % [D33C]`; if `j != [E060]` and `j != [E072]` and `site[j].w6 != 0` and `!(site[j].flags & 0x0400)` and `!(site[j].flags & 0x0100)`: `site[j].flags |= 0x40; k--`.
5. For `i = 0..[D33C]` (inclusive): if `site[i].flags & 0x40` and `rand() % 3 == 0`: `site[i].flags |= 0x80; site[i].w6 = 0`.
Meaning of 0x40/0x80 (random event sites, destroyed) is (U); `rand()%n` is 76C0 (`rand` 96CE then modulo). Port: keep the exact order of rand() calls if the seed is deterministic (it is: srand called elsewhere), otherwise irrelevant.
40A4 `mem_alloc_chk` / 40CF `mem_free_chk` (already in platform_symbols: dos_allocmem wrappers printing "Insufficient system memory - AllocBuffer$" / "Buffer dealloc error$" then `fatal_exit(0)` 76D1).

---
## 7  Dialogs defined in DGROUP (all with cb = ui_dialog_hover) (V)

| Descriptor | msg | button1 | button2 |
|---|---|---|---|
| 70AA | Changing pilot orders will cancel your scheduled mission. | OK | No Way! |
| 710D | Changing pilots will cancel your scheduled mission. | OK | No Way! |
| 715F | U.S. Air Force regulations prohibit deceased officers from flying multi-million dollar aircraft.  Please choose again or erase this entry. | OK | - |
| 7200 | You need to receive a mission briefing first. | OK | - |
| 7244 | This officer has retired. Please choose again or erase this entry. | OK | - |
| 729D | Original disk in drive. Roster file will not be saved. | OK | - |
| 72EA | Disk is write protected. Roster file will not be saved. | OK | - |
| 7338 | Your security clearance does not authorize you to conduct air operations in this theatre. | OK | - |
| 73A8 | Are you ready for your mission? | Yes | Not yet |
Button strings shared: "OK" at 7102, "No Way!" 7105, "Yes" 73E6, "Not yet" 73EA.

---
## Open items
* Page of the three contexts: the first word of the ctx is the page index into CAB0 (gfx_line 829A reads `es = CAB0[ctx[0]]`, V): DC02 = page 0 and DC18 = page 1 (V from their use as screen / back-buffer targets), DC2E = page 2 (U, implied by the popup code).
* (U) `map_draw_route_line` pixels use colour index 0 (gfx_line 6th arg is the colour byte, V from 829A): check it is black in the map palette.
* (U) `ui_draw_frame` exact edge geometry and the E0..E6 ramp colours (visual).
* (U) Meaning of site flags 0x40 / 0x80 and the map-cell bit 0x10 (consumer in 4239 and later).
* (U) Fade duration `[B304]*speed` BIOS ticks: choose a fixed duration in the port.
* (U) `ui_poll_input` return value for ordinary keys (ascii low byte vs scan:ascii word) - port by re-reading 3C95-3CE2.
* (U) D88 vs D7C time strings (departure vs arrival) in 2651 kinds 5-7.
* No callers found for 3548, 3E6D, 39B3/39D5 only from 07EE/5A8A.
* The sprite page used by the cursor (E088) and bounds DC26..DC2C are set by code outside this range (74B1, 6DCF area).

## DGROUP globals seen
| DS offset | type | meaning | readers/writers |
|---|---|---|---|
| 0882 | u8[10] | start month per theatre | 25D9 |
| 088C | u8[10] | start day per theatre | 25D9 |
| 0896 | u16[10] | start year per theatre | 25D9 |
| 08A8 | char*[13] | month names (Jan..Dec) | 25D9 |
| 08C2 | u8[14] | days in month (index 1..12) | 25D9 |
| 07F0 / 0815 / 083C | str | heading / OBJECTIVE strings | 24F7 |
| 11DE | 12-byte records | objective type table (kind, flags +4, extra +6) | 2651 |
| 1708 | 32-byte records | aircraft names | 2651 |
| 1F24 | char*[] | objective intro paragraphs | 24F7 |
| 0CB2 / 0CBA | str / char*[] | "%s.pic" / screen names | 35FC |
| 0D6A | char*[] | theatre file suffixes lb pg nc ce vn me | 40F0 |
| 11BE / 11C4 | 3 bytes | DAC highlight (3F,32,2B) / normal (39,3C,3B) colours | 3CEA, 3DA2 |
| 64A6 | 6*10 B | DAC update queue | 3517, 8427 |
| 64E2 | u8 | queue length | 3517, 8427, 30A0 |
| 64E3 | u8[768] | working palette (E7/E8 at 6798/679B) | 30A0, 3641, 3CEA, 3DA2 |
| 69B2 / 69B3 | u8 | joystick X / Y (centre 80h) | 39F7 |
| 73F2 | u8 | break hook installed | 3E5E |
| ADFB | u8 | timer ISR installed | 30A0 |
| B2EA..B302 | mixed | animation and popup state (see syms) | 28CE 2901 2B1B 3588 |
| B304 | u8 | fade calibration | 30A0, 31AD |
| B306 | u16 | previous key | 39F7 |
| B308.. | 84h B | active menu descriptor copy (cb, hotkey, count, rects) | 3CEA, 38AC, 3DA2 |
| B38C / B38E / CABE | u16 | pilot+3A / theatre (pilot+38) / pilot+3E | 40F0 |
| CAB0 | u16[] | page segment table | 35FC |
| CAC0 / CBD8 | far ptr | -> 0000:04F4 / 0000:04F2 | 7387 |
| CACA | far ptr | pilot record (comm+1272h) | 25D9, 40F0 |
| CAD2..CAD5 | u8 | option bytes copied from comm+38..3E | 40F0 |
| CAD8 | u8[256] | map cell flags | 40F0 |
| CBDC | u16 | current site index | 2651 |
| CBDE | 16 B x [D33C] | site records | 2651, 40F0 |
| D096 | u8 | hovered dialog button | 3CEA, 3DA2 |
| D092 | far ptr | palettes.pal data | 826C |
| D336 / D338 / E086 | u16 | file buffer off / seg / seg | 35FC, 2864 callers |
| D33A | u8 | map has a bit-10h cell | 40F0 |
| D33C | u16 | site count | 40F0 |
| D3D4 | u16 | enemy aircraft type (index into 1708) | 2651 |
| D7C / D88 / D8E | char[] | time / time / ONC grid text buffers | 2651 |
| DC02 / DC18 / DC2E | TextCtx x3 | page 0 / page 1 / scratch text contexts | many |
| DC26..DC2C | u16 x4 | cursor ymin, ymax, xmin, xmax | 39F7 |
| DC70 | char*[] | place / noun name pointers (runtime) | 2651 |
| E05E | 12h x2 | objective records | 24F7, 2651; E060/E072 in 40F0 |
| E088..E095 | mixed | cursor sprite seg, x, y, old x, old y, w, h, 2, 2 | 39F7 3989 3E20 3E84 |
| E096 | far ptr | comm | 39F7 38AC 3EA5 3F33 40F0 |


---
# Part C: mission generator and orders helpers

### S3.5 reader c: START 0000:4239 (mission generator) and 0000:512E..6200 (WLD helpers, orders screen)

Source: work/s35/b.txt (4239..512D, one function) and work/s35/c.txt (512E..623D). Everything below was read in the
disassembly unless marked (U). Addresses are START code offsets (seg 0000), DS = DGROUP 0A95. Notation: `site[i]` =
16-byte site record at DS:CBDE+16*i, `unit[u]` = 36-byte record at DS:D3BE+36*u, `pilot` = far ptr DS:CACA
(= comm+1272h, the active pilot record), `comm` = far ptr DS:E096 (comm+0). `rand(n)` = 0000:76C0 (`rand() % n`),
`abs` = 96AE, `strcpy` = 94AC, `movedata` = 95F4. `blit_rect(spage,sx,sy,dpage,dx,dy,w,h)` = driver slot 42 (lcall 0A95:6A8A);
sound effect = slot 101 (lcall 0A95:6BB1, arg = effect id); `text_noclip` = slot 5 (0A95:69D1); `sprite_scaled` = slot 22 (0A95:6A26);
`screen_on` slot 70 (0A95:6B16).

## 0. Theatre numbering and what the pilot record fields mean (verified from the tables)

Theatre index (pilot+38h, also DS:B38E) 0..8, file prefix table DS:0D6A -> `lb pg nc ce vn me ku cu ko`:
0 Libya, 1 Persian Gulf, 2 North Cape, 3 Central Europe, 4 Vietnam, 5 Middle East, 6 Desert Storm, 7 Cuba, 8 Korea
(names DS:9ABE table; long descriptions DS:9B29 table). Files `<prefix>.wld/.3dG/.3dT`.

| pilot+ | meaning | evidence |
|---|---|---|
| +00h | pilot slot number 0..9 (used as `10*n+0x2F` cursor row, `3*n` palette entry) | 615C |
| +38h | theatre 0..8 (copied to DS:B38E by 40F0) | 5D44, 4239 |
| +3Ah | war level 0..2 (copied to DS:B38C): 0 Cold War, 1 Limited War, 2 Conventional War (tooltips for hotspots 10..12, DS:A187/A214/A2C4) | 5DA7, 4239 |
| +3Ch | mission type 0..3 = hotspot-13: bit0 = ground strike, bit1 = training. 0 Air-to-Air, 1 Ground Strike, 2 Air-to-Air Training, 3 Ground Strike Training (tooltips DS:956C/95F7/9676/96FF) | 5DC9, 4239 |
| +3Eh | enemy skill / opponent level **0..3** (Green, Regular, Veteran, Elite; copied to DS:CABE). The only writer is the pilot options screen: `6FEB` stores `button - 1` for the four skill buttons 1..4 ((corrected 2026-10-13, see HANDOFFS C8)); 4239 uses `cabe-5`, `4-cabe`, `rand(5)+cabe` as its own expressions | 40F0, 4239 |
| +42h | non-zero = pilot restricted (hotspots 13/14 = real missions disabled, weapon search starts at row 2, site flag 0x200 excluded) (U exact meaning: rookie/training-only?) | 5A8A, 5282, 4239 |

comm fields written/read here: +22h (clearance: SETUP sets 1 and only the dead quiz clears it, so it is always 1 in a real run; 0 would force `pilot+38h = 0` and `pilot+3Ch \|= 2` at `6328` / `6D91`: (corrected 2026-10-13, see HANDOFFS G3)),
+33h (night flag 0/1), +2Eh (route cost = 12*route score), +38h/+3Ah/+3Ch/+3Eh (4 loadout/weapon ids), +40h..+46h (4 words, quantity per id),
+72h (bit0 joystick, bit1 mouse; test in 38AC/3E84), +7Ah (world export buffer, see 5625).

Colour convention of the orders screen: the map picture uses DAC colours F7h..FFh for the 9 theatres (colour 256-hotspot, hotspot 1..9)
and F0h..F6h for the 10..16 buttons. Highlighting = rewriting that DAC entry via the palette queue (pal_queue, 3517):
white DS:11AC (3F,3F,3F), orange DS:11C7 (3E,29,1B) = selected, red DS:11C1 (3F,00,00) = hover on theatre, DS:11D0 (1F,29,24) = idle button,
grey DS:11CA (2A,2A,2A) = disabled. (verified)

## 1. mission_generate  0000:4239  (3829 bytes, bp-0xD0 frame)  `void mission_generate(void)`

Called once per mission from 0000:40F0 right after `wld_load_theatre(theatre_prefix[pilot.theatre])` (0000:411A, 411E); afterwards
40F0 copies comm+38h/3Ah to DS:CAD2/CAD3 (verified). Fills the "mission slots" E05E/E070, unit table, a few
waypoints, comm loadout fields and the time strings. Randomness only through rand(). Four helper facts first.

Site record (16 B, from .WLD; verified use):
`+0 w name index (into DS:DC70 name pointer table; 0 = unnamed)`, `+2 w x`, `+4 w y` (WLD units, 1/32 world; y runs south),
`+6 w threat level 0..16`, `+8 w flags`, `+A w unit type (assigned by generator, see P6/P8)`, `+C w count`, `+E w object id (bank<<8|index; low 7 bits = object type)`.
Flag bits seen: 0x001 valid target; 0x002/0x004? (U); 0x008 "threat may be removed" (4E5E); 0x100 defence-site class A (adds cost rand(100)+50 *64
when also low-byte test `+9&1`); 0x200 favoured/airfield (cost bonus 0xC80, unit z +0x80, D326 offset 0x708); 0x400 defence-site class B; 0x800 (U).
Sites 0..2 are scratch slots (slot 1/2 are overwritten by wld_site_of_type / wld_site_at_pos). DS:E084 = first site index of
the "non-target" block; targets are picked from 3..E084-1. DS:CACE = number of leading sites that carry a threat level.

Mission slot (18 B, two of them: slot0 at DS:E05E, slot1 at DS:E070, "swap" copy is 9 words):
`+0 kind (0 none, 1..8)`, `+2 target site`, `+4 anchor/defence site (best defended site near the target)`, `+6 flags (from template +8 plus w5<<8)`,
`+8 template index (0..81)`, `+0A char[5] grid-reference string "AB12"`, `+10h (slot0 only, DS:E06E) = D876>>4`.
Mission template table: 82 entries x 12 B at DS:11DA (6 words): `w0 mask of theatres (bit = 1<<theatre)`, `w1 mask of war levels (1<<pilot+3A)`,
`w2 kind (stored in slot+0)`, `w3 required target category (== DS:D2A6[site type & 7F])`, `w4 flags (slot+6)`, `w5 signed: >0 -> hi byte
added to slot flags (<<8); <0 -> -w5 becomes unit[0].type (D3D4) (48C8)`. (verified)
Kinds (U, names guessed from branches): 1 = photo/first-weapon 0x10, 2 = strike with weapon by target class, 3/4 = strike (weapon 0x12; kind 4 forces
night), 5..8 = air-to-air kinds that need a moving unit 0 (5 convoy-like target placed by distance, 6/7 = two-ended route, 8 = static special).
Ground-strike missions (pilot+3C bit0) must have both kinds < 5.

Phases (all verified, addresses are the disasm labels):

P0 4242..4255: `D876 = 0`; `min_route = 450 (bp-34h)`; `base = 900 (bp-24h)`; `attempts = 900` (forces the first re-roll below).
P1 4256..42A7 (re-roll entry): `attempts++`; if `attempts > 50` { collect all object types 0..127 with `D2A6[t] != 0` into a list, pick random
until `D2A6[pick] != 0x15` -> `target_type` (bp-32h); `attempts = 0` }. Every retry in the function jumps back here.
P2 42A8..4331: choose the primary target site `E060`.
 - air mission (pilot+3C bit0 clear): `r = rand(E084-3)+3` until `(site[r].flags & 0xD01) == 1`; `E060 = r`.
 - ground mission (bit0 set): loop `E060 = wld_site_of_type(1, target_type)`; 0xFFFF -> `attempts = 900`, re-roll; accept when
   `(site.flags & 0x801) != 1` and `(site.objid & 7F) == target_type` (bp-2Ah), else try again.
P3 4331..444C: choose a second site `E072` (slot-2 scratch site) within the 6400-unit box around E060: up to 100 tries of
 `x = rand(0x3200)+site.x-0x1900`, `y = rand(0x3200)+site.y-0x1900`, clamped to 0x840..0x77BF; the 16x16 grid byte
 `DS:CAD8[(y>>11)*16 + (x>>11)]` must satisfy `&3 == 0` (land); `E072 = wld_site_at_pos(x,y,2)`. Reject (-> P2) when -1, when (ground) its
 flags&0x801 == 1, when it is the same position as E060 (accept without distance test) or when `wld_site_dist(E060,E072) & 0xFFC0 > 0x6400`.
 After 100 tries -> P1.
P4 4455..45B2 (two passes, pass = 0 for E060, 1 for E072): for sites `i = E084+1 .. D33C-1` with `(flags & 0x500) != 0 && (flags & 0x201) != 0`
 (and not (theatre 7, name 0x11), not (pilot+42 && site.byte9&2)): `cost = wld_site_dist(target_of_pass, i)`; pass 0 adds
 `(rand(150)-75)<<6`; if `byte9&1` add `(rand(100)+50)<<6`; `cost = clamp_hi(cost,0,0x7FFF)`; skip if 0x7FFF; `cost' = cost + rand(0x500)`;
 accept if `best[pass] + (flags&0x200 ? 0xC80 : 0) > cost'`: then `slot[pass].anchor = i`, `best[pass] = cost` (raw). `best[]` = bp-3Eh,bp-3Ch.
 Scripted training special (45B3..461F, pilot+3C bit1 and theatre 0): ground training (3C bit0): `E060=3,E072=0xF,E062=0x22,E074=0x21`; air training:
 `E060=0x16,E072=8,E062=0x22,E074=0x23`; best[0]=dist(E060,E062), best[1]=dist(E072,E074).
 Route score `bp-4 = dist(E060,E072)>>6 + best[0]>>6 + best[1]>>6`. If `score > 900+attempts` or `score < min_route`:
 `min_route += cabe-5; goto P1`.
P5 465C..4777 template choice, once per slot (slot index bp-44h = 0,1; slot kind pre-zeroed): scan templates 0..81 where
 `(w0 & 1<<theatre) && (w1 & 1<<pilot+3A) && D2A6[type of slot's target site] == w3 && (slot==0 || tmpl != E066)`; two-pass uniform choice
 (first pass counts candidates bp-CAh, second pass takes the rand(count)-th, bp-22h). Result: `slot.kind=w2`, `slot.tmpl`, `slot.flags=w4 (+ (w5&0xFF)<<8 if w5>0)`.
P6 4778..47BF: scripted training (as in P4): `bp-CC = (3C&1) ? 5 : 55`; E05E/E066/E064 copied from template `bp-CC` (kind w2, index, flags). (U: template 5 for
 ground training, template 55 for air training.)
 Validation 47BF..4850: ground -> both kinds must be < 5; air -> both kinds non-zero; else `attempts = 900` re-roll (42F6).
 Reject (retry P1) if `E066 == E078`. If `best[1] > best[0]` and not training: swap the two 18-byte slots and the two costs (slot0 gets the cheaper one).
 Reject if slot1.flags&2; if slot1.kind in 5..8; if slot0.kind==4 && cabe==0; if slot0.flags&8 && slot1.flags&8.
 `if slot0.flags&2: D876 = (wld_site_dist(E060,E062)>>4) + 450`; `if slot1.flags&2: D876 = (dist(E072,E062)>>4)+450` (units: ticks).
 `cabc = -1`; if `tmpl[E066].w5 < 0`: `D3D4 = -w5` (unit 0 type).
P7 48D8..4ABC kind-specific moving unit (unit 0, `cabc = 0`, flag D3D6 |= 4; `D3BE`=site, `CBDC`=special site):
 - kind 5: choose site `i < E084` with flags bit0 set and bit8 clear minimising `|dist(i,E060) - best[0]|`; `unit_init(0, i)`; `D3BE = E060`;
   `strcpy(DS:0D8E, grid_ref(i))`; `D876 = dist(E060,i) / (((unit0.speedfield>>6)&..)*3)` (uses unit[cabc]+1Ah>>6, *3).
 - kind 6/7: choose `i = 3..D33C-1` with `(flags>>8)&5 != 0 && (flags>>8)&0xA == 0`, `i != E062`, minimising `|dist(i,E060)-best[0]|`;
   kind 7: `unit_init(0,E060)`, `D3BE=cbdc`, `CBDC=E060`; kind 6: `unit_init(0,cbdc)`, `D3BE=E060`, `D876 = dist(cbdc,E060)/(( D3D8&0xDF)>>5)`;
   both `strcpy(DS:0D8E, grid_ref(cbdc))`.
 - kind 8: `unit_init(0,E060)`; if `D3D4==2 && theatre==1` then `D3D4=0xC`; `D3D6|=0x40`; `D3BE=CBDC=E060`.
 If `cabc==0`: `D3DA = 0x4E1F` (unit0 "no next leg" sentinel 19999).
P8 4AC9..4B6B: for each slot: find the nearest named site `j` (`flags>>8 & 5 == 0`, `site[j].name != 0`) to the slot target (only when slot target idx < 3,
 i.e. a scratch site) and copy its name index into the target site (`site[target].name = site[j].name`); then
 `strcpy(slot+0Ah, grid_ref(slot.target))`.
 4B6C..4C3C: `E06E = D876>>4`; geometry words: `D29A:D29C (long) = site[E062].x<<5`, `D326:D328 (long) = ((0x8000-site[E062].y)<<5) - (flags&0x200 ? 0 : 0x708)`;
 waypoint words `15B2/15B4 = (site[E062].x>>1)+(site[E060].x>>1)` (and y) midpoint, `15B6/15B8 = site[E060].x/y` (if slot0.flags&0x10: `(v&0xFC00)+0x200`),
 `15BA/15BC = site[E072]`, `15BE/15C0 = site[E074]` (verified; used by the briefing map, U).
P9 4C3D..4E0B place units `u = 0 .. D890-5` (last 4 units are reserved): 8 random candidate sites `rand(E084)+1` (skip flags bit10) pick the one whose bearing
 (`mission_bearing(site.x-U.x, U.y-site.y)`) is closest to the wanted heading `bp-0Eh` -> `U+0 = site`; if `U.flags&0x100` and `cabc != -1`: `unit_init(u, CBDC)` and
 `U+1Ch = 0x4E1F`; for `u != 0`: up to 20 tries to find an empty target site (`flags&0x801==1`, `site.+C == 0`) and give it `site.+A = unit type`, `site.+C = rand(theatre+1)+1`.
 Units whose flags have 0x80 are first re-sited: `limit = (4-cabe)*(|best[0]|>>2)` (or `best[0]*2` if flags&0x40), pick random site (not bit8) within `dist(15B2/B4, site) <= limit+16`
 -> `unit_init(u, site)`, then re-run the heading search. (U: exact intent = scatter patrols/SAMs around the route.)
P10 4E0D..4EC5 threat levels: for sites `i < DS:CACE`: `lvl = site[i].+6`; skip 0 and 0x15; roll `rand(5) + (pilot+3A>=1) + cabe`;
 roll 0,1,3 -> `lvl = DS:1EE6[4*lvl]`; roll 5,7,8 -> `lvl = DS:1EE4[4*lvl]`; roll 2,4,6 or >8 unchanged; store; if site flags&8 and
 `pilot+3A + cabe + 2 < rand(10)` -> `site.+6 = 0`.
P11 4EC6..4F0E: for each of the 16x16 grid cells (DS:CAD8) with bit 0x10: keep with probability cabe/5 (`rand(5) < cabe` keeps) else clear the bit.
P12 4F0F..5060 comm loadout: `comm+38h=1`; `comm+3Ch = rand(2) ? 9 : 5`; `comm+3Eh = 0`; `comm+3Ah = 2`; `comm+2Eh = 12*score`; if that `> 10000` `comm+3Ch = 0x11`.
 For slots i=0,1: id = -1; kind 1 (first time) -> 0x10; kind 3 or 4 -> 0x12; kind 2 -> `arm_pick_weapon(D7F6[type of slot target] & 0xF)`; if id != -1 `comm[38h+2i] = id`.
 Training special (5001..505F): ground training: `comm+38h=5, +3Ch=1`; air training: `+38h=1, +3Ch=5`; `+3Eh=0`, `+3Eh=0x11` if `12*score > 10000`.
 For i<4: `comm[40h+2i] = DS:1EC6[comm[38h+2i]]` (byte quantity table, 17 entries).
P13 508B..512D times: `bp-8 = ((E078 + E066) & 0xF) << 8`; `comm+33h` (night) = 1 if either slot kind is 4; else if pilot+42: `rand(10) != 0`, else `rand(10) > 2`.
 `clock_string(DS:0D88, bp-8)` (launch), `clock_string(DS:0D7C, D876+bp-8)` (time over target), `clock_string(DS:0D82, D876+bp-8+0x1C2)` (+15 min);
 `D876 -= (D876+bp-8) % 150`.

Outputs for later code (briefing/VGAME): E05E..E081 slots, E06E, D876, CABC, CBDC, DS:0D8E grid string, D3BE.. units, D29A/D326, 15B2..15C0, comm+2E/33/38..46.

## 2. WLD / site helpers (0000:512E..5466 and 54BB..5743)

| addr | proposed C | what it does |
|---|---|---|
| 512E | `int wld_site_at_pos(int x, int y, int slot)` | `rec = terrain_lookup(x<<5, (0x8000-y)<<5)` (0000:7744, returns near ptr or 0 -> return 0xFFFF). Snaps to the cell centre: `x = rec.long@+4 >>5`, `y = -((rec.long@+8>>5)-0x8000)`. Scans sites 3..D33C-1 for equal (x,y) and returns that index; otherwise writes `site[slot] = {x,y, objid = rec.w0 + 0x100}` (`+2,+4,+E`) and returns `slot`. (verified) |
| 51E8 | `int wld_site_of_type(int slot, int type)` | same body, but `rec = find_object_of_type(type)` (0000:7904, arg sign-extended char). (verified) |
| 5282 | `int arm_pick_weapon(int cls)` | rows k = (pilot+42 ? 2 : 0)..15 with `DS:D878[k]==0` (excluded flags); value `DS:1D86[k*16+cls]`; keeps the max and all ties (list at bp-12h); returns rand tie. Table 1D86 = 16 weapons x 16 target classes. (verified) |
| 532A | `int wld_site_dist(int a, int b)` | `dist_approx_xy(site[a].x-site[b].x, site[a].y-site[b].y)`. |
| 5356 | `void mission_unit_init(int u, int site)` | `U=unit[u]`: `+0=site`, `+2=site.x+9`, `+4=site.y-0xC`, `+8:A = long(U+2)<<5`, `+C:E = long(U+4)<<5`, `+6 = 0x8C` (+0x80 if site flags bit9 clear), `+10h=0xFC00`, `+12h=+14h=0`, `+18h |= 0x403`, `+1Ah = AC[type].w0` where `AC = DS:171A + 32*type` (type = `U+16h`, set before the call), `+1Ch = (AC.w2<<13) / U+1Ah`. DS:171A is the enemy aircraft table: 32-byte records `w0 w2 w4 w6 w8 wA wC, char name[18]` ("MIG-25"," Foxbat"; MiG-29 Fulcrum, MiG-31 Foxhound, Su-27 Flanker...). (verified) |
| 5413 | `int dist_approx_xy(int dx, int dy)` | `a=abs(dx), b=abs(dy)`; returns `max + min/2`, saturated to 0x7FFF. |
| 5466 | `void wld_load_theatre(const char *prefix)` | builds `"<prefix>.wld"`, calls `wld_read_file`, then `"<prefix>.3dG"` -> 0000:7D71 and `"<prefix>.3dT"` -> 0000:7E88. Strings DS:0D93 ".wld", 0D98 ".3dG", 0D9D ".3dT". |
| 54BB | `void wld_read_file(const char *path)` | `fopen(path,"rb")`(mode DS:0DA2); `fread` in order: DS:D88C 2 B (id), D33C 2 (nSites), CACE 2, E084 2, CBDE 16*nSites, D890 2 (nUnits), D3BE 36*nUnits, D7F6 128, D33E 128, D2A6 128, CAD8 256, DD70 750; fclose. Then builds the name pointer table: `DS:DC70[0]=DD70`; for each NUL at DD70+i (i<750) while k<128: `DC70[k++] = DD71+i`. (verified; matches FORMATS .WLD) |
| 5625 | `void wld_export_to_comm(void)` | `dst = comm+7Ah` (cursor DS:B390 off / B392 seg); with `wld_pack_put(src,size,count)` (588D) appends: D88C, D33C, CACE, E084, CBDE (nSites*16), D890, D3BE (nUnits*36), D7F6 (128), D33E (128), DD70 (750), CAD8 (256), D876 (2), CABC (2), 15B2 (4*4 B), E05E (2*18 B). D2A6 is NOT exported. VGAME unpacks the same layout (585C = `wld_pack_get`, reverse copy from the cursor). (verified) |
| 585C | `void wld_pack_get(void *dst, int size, int count)` | `movedata(far cursor -> DS:dst, size*count)`; cursor += len. |
| 588D | `void wld_pack_put(void *src, int size, int count)` | `movedata(DS:src -> far cursor, size*count)`; cursor += len. |

## 3. Small helpers

| addr | proposed C | notes |
|---|---|---|
| 5743 | `unsigned mission_bearing(int dx, int dy)` | 16-bit bearing, 0x0000 = +dy (north in the caller's convention), 0x4000 = +dx, 0x8000 = -dy, 0xC000 = -dx (0x10000 = 360 deg). Octant atan: `t = (min<<14)/max`; `r = t*(0x2800 - ((|0x1333-t|*0xB00)>>14)) >> 14`; quadrant fix-ups: (dx>0,dy>0): r or 0x4000-r (by |dx|>|dy|); (dx>0,dy<=0): 0x4000+r / 0x8000-r; (dx<=0,dy>0): r-0x4000 / -r; (dx<=0,dy<=0): 0xC000-r / 0x8000+r. dx==0 / dy==0 handled first. Note long-divide helper 96F4 pushes divisor first, dividend second. (verified) |
| 58BE | `char *site_grid_ref(int site)` | `grid_ref(site.x, site.y, pilot.theatre)` (58DF). |
| 58DF | `char *grid_ref(int x, int y, int theatre)` | Returns DS:B394 (5 bytes, e.g. "TD00"). Base string by theatre: 0 TD00, 1 JZ00, 2 WX00, 3 CC00, 4 XV00, 5 ES00, 6 KU00, 7 CU00, 8 KO00 (strings DS:0DA5.. spaced 5); initial offsets (colOff,rowOff) = (6,4) for 0, (3,5) for 3, else (0,0) (jump table cs:5963, 9 words, verified from raw bytes). `col = ((x>>5)*20>>10) + colOff`; while col>9 {col-=10; s[0]++}; `s[2] += col`; `row = ((y>>5)*20>>10)+rowOff`; while row>9 {row-=10; s[1]--}; `s[3] += 9-row`. It is an ONC-style chart reference ("last seen at ONC %s"). (verified) |
| 59D8 | `int clamp_hi(int v, int lo, int hi)` | returns `v` if `lo <= v <= hi`, else `hi` (also for v < lo). Always called (cost,0,0x7FFF). |
| 59FE | `void clock_string(char *buf, int ticks)` | `buf="00:00"` then adds start hour: `buf[0] += comm+33h` (night: 1), `buf[1] += night?8:6` (so day starts 06:xx, night 18:xx), `buf[1] += (ticks/1800)%10` with carry to `buf[0]`; minutes `m=((ticks/30)%60)` rounded down to 5: `buf[3] += m/10`, `buf[4] += m%10`. 30 ticks = 1 minute (U: tick = 2 s). Output buffers DS:0D7C, 0D82, 0D88 ("00:00"). (verified) |

## 4. Orders screen ("ordrscrn"): 0000:5A8A, 5BF0, 5E6F, 5FFA, 6045, 60DC

Pictures names DS:0CBA table (index -> file): 0 air_id,1 armsscrn,2 brefscrn,3 folder1,4 hangscrn,5 homescrn,6 ordrscrn,7 rostscrn,8 breficon,9 folder2,
10 ordrsprt,11 requestr,12 rostsprt,13 weapons1..15 weapons3,16 libya,17 persian,18 n_cape,19 c_europe,20 vietnam,21 mid_east,22 kuwait,23 cuba (verified).
Helper roles (readers a/d own the bodies; names are the call-site meaning): 3641 `ui_show_screen(pic, callback)` (load pic into page 1, pal, blit to page 0, call `callback`,
screen_on); 35FC `ui_load_pic(pic, seg_slot)` (page seg slots DS:CAB0+2*n); 38AC `ui_menu_poll(Menu*)` (idle callback [menu+0], returns hotspot 1..N,
0x63 = right click, 0x100+key); 393D `ui_hit_test(entries, n)` (cursor DS:E08A/E08C; entry = x0,y0,x1,y1; returns idx+1 or 0); 3989 cursor restore (draw
saved cursor rect); 3517 `pal_queue(first, rgb*, count)` (queue DS:64A6, 6 B entries, count DS:64E2, flushed by 8427); 3693 `ui_draw_sprite(SpriteCtx*, x, y, seg=DS:CABA)`
(slot 22); 3567 `ui_text_at(ctx, str, x, y)`; 3776 `ui_text_wrap(ctx, str, colour, x, y, line_h, 0)`; 2A84 `ui_text_center(ctx, str, cx, y)`; 3E84 `ui_cursor_set(x,y)`;
3CEA `ui_dialog(Dialog*, y, x)` returns 1/2; 28CE `ui_pulse_start(colour, base_tbl, step_tbl, n)`, 2901 `ui_pulse_tick()`, 2A7E `ui_pulse_active()`; 2B06 `wait_frames(n)`;
7272 `roster_save(0)` ("Roster.Fil").

### 5A8A `void ui_orders_draw(void)` (callback of 3641(6,...))
`ui_load_pic(10 /*ordrsprt*/, 4)`; `blit_rect(1,0,0, 3,0,0,320,200)` (page 3 = clean background); `D096=0; B39A=0`.
Palette: `pal[DD8[pilot.theatre]] := orange` in the working palette array DS:64E3 (3 B per colour); if pilot+42: colours F3h and F1h := grey (`pal[F1]=pal[F3]=11CA`).
Hotspots 13,14 (entries at 6F72+8*(k-1): 6FD2, 6FDA): `39D5` (normalise x0<=x1 = enabled) when pilot+42==0, else `39B3` (swap -> x0>x1 = disabled).
If comm+22==0: colours F2h,F3h := grey and hotspots 13,14 disabled (39B3). Then draw the two marker sprites DC02 and DC18 at
`(DEC[3A], DFA[3A])` and `(DF2[3C], E00[3C])` (x/y tables DS:0DEC / 0DFA, 7 entries: war-level markers 0..2 then mission-type markers 3..6 via 3C). Ends with `ui_theatre_name_draw` (60DC).

### 5BF0 `int ui_orders_screen(void)` (menu loop; always returns 0)
`ui_show_screen(6, ui_orders_draw)`; then loop `sel = ui_menu_poll(DS:6F6E)` (hotspots: header w callback=5E6F, b=0, b count=0x11; entries DS:6F72..; hotspot 1..9 theatres,
10..12 war level, 13..16 mission type, 17 = "Fly"; coordinates dumped from DS:6F72, 8 B each).
 - If `D2A2 != 0 && sel < 0x11`: `ui_dialog(DS:70AA, 0x56, 0x46)` ("Changing pilot orders will cancel ..."); result 2 -> `sel = 0` (ignored), else `D2A2 = 0`.
 - sel 1..3,5..9 (theatre hotspots except 4): if `comm+22 != 0` handle as 4, else `ui_dialog(DS:7338, 0x3C, 0x46)` ("Your security clearance does not authorize ...") and continue.
 - sel 4 (and unlocked others): `th = DE2[sel]` (DS:0DE2: hotspot 1..9 -> theatre 7,2,3,0,5,6,1,4,8); if `th != pilot.theatre`: old theatre colour `DD8[old]` := white (3517), `pilot.theatre = th`, new colour `DD8[th]` := orange, `ui_theatre_name_draw()`.
 - sel 10..12: `pilot+3A = sel-10` (remember old marker index in bp-0Ah); sel 13..16: `pilot+3C = sel-13` (old marker = old+3). Then for sel>9: erase the old marker
   (`blit_rect(3,x,y,1,x,y,12,8)` then to page 0), draw new marker sprites DC18/DC02 at (DEC[sel-10], DFA[sel-10]) and sound effect 0x1B.
 - sel 0x63 (right click/cancel): `3989()` and return 0 (no change saved).
 - sel 17 (Fly): `3989()`; `roster_save(0)`; if `B39A`: `ui_pulse_start(4, 0x10E0, 0x10D2, 7)`; 62 frames (i=0..0x3D): `pal_apply_queue()`;
   `blit_rect(2, 0xF1+i, 0xA7, 0, 0x102+i, 0xBC, 1, 12)` (1-px column reveal of a bar), `ui_pulse_tick()`; then `wait_frames(30)`; return 0.
   The caller (state machine, reader a/d) then proceeds to the briefing. (U) whether sel 17 vs 0x63 differ for the caller: both return 0, only the roster save and the animation differ.

### 5E6F `void ui_orders_hover(void)` (idle callback of the menu)
`hover = ui_hit_test(DS:6F72, DS:6F71)`; if `D096 != 0 && D096 != hover`: for previous hotspot p<=9 (theatre) and `DE2[p] != pilot.theatre`: colour `256-p` := white (11AC);
p>9: colour `256-p` := DS:11D0 (idle); if `hover==0 || hover==17` `ui_tooltip_clear()`. If `hover != 0 && D096 != hover`: for theatres: red highlight (11C1) only if not the current
theatre, tooltip text = theatre description `DS:9B29[DE2[hover]]`; hotspots 10..12: tooltip `DS:A16D+2*hover`; 13..16: `DS:954A+2*hover`; 17: red highlight only.
`D096 = hover`. Finally if `ui_pulse_active()` and `ui_pulse_tick()==0`: `B39A = 1` and re-show the tooltip of the current hotspot. (verified; B39A = tooltip may animate/frozen (U))

### 5FFA `void ui_tooltip_clear(void)`
If not pulsing: `blit_rect(4,0xBA,0x68,1,0,0x68,0x86,0x60)` and the same to page 0 (restore tooltip panel from the sprite sheet).

### 6045 `void ui_tooltip_show(const char *text)`
If `B39A==0`: `ui_pulse_start(4, 0x10E0, 0x10C0, 9)` (animated panel) and return. Else: `blit_rect(4,0xBA,0x68,1,0,0x68,0x86,0x60)`; ctx DC18: `DC24=4, DC1E=-1, DC1C=0`;
`ui_text_wrap(DC18, text, 15, 0x78, 0x6E, 8, 0)`; `blit_rect(1,0,0x68,0,0,0x68,0x86,0x61)`.

### 60DC `void ui_theatre_name_draw(void)`
`name = DS:9ABE[pilot.theatre]`; `blit_rect(3,0xD4,0x79,1,0xD4,0x79,0x46,0xB)`; `DC24=4, DC1C=8`; `ui_text_at(DC18, name, 0xD4, 0x7C)`; `blit_rect(1,0xD4,0x79,0,0xD4,0x79,0x46,0xB)`.

## 5. Roster screen callback: 0000:615C `void ui_roster_draw(void)`
`ui_load_pic(12 /*rostsprt*/, 4)`; `blit_rect(1,0,0,3,0,0,320,200)`; `DC24=1; DC1C=0xFF`; for `i=0..9`: `roster_draw_line(i, 0x2C+10*i)` (0000:6861, reader d) and `DC1C--`;
`DC24=3; DC1C=0x1A`; `ui_text_center(DC18, "Press 'Delete' key to erase a pilot", 0xA0, 0x90)`; palette colour `0xFF - pilot.slot` := orange (`DS:67E0-3*slot`);
`blit_rect(1,0x1E,0x2C,0,0x1E,0x2C,0x10E,0x6E)`; `ui_cursor_set(0xA0, 10*slot+0x2F)`. String DS:0E1B (verified).

## 6. Open items
1. Exact meaning of pilot+3Eh (DS:CABE) and pilot+42h; comm+22h; tooltip/pulse flag DS:B39A and D2A2 writer (who sets D2A2 = 1 after a mission is issued?).
2. Mission kind names 1..8 (match with briefing text in reader d: "Intercept and destroy the %s%s at ONC %s.", "...last seen at ONC %s at %s hours...", photo recon) and units' flag bits (0x40, 0x80, 0x100).
3. Site flag bit meanings (0x2, 0x4, 0x800) and DS:D33E table role (loaded, never read in this range); DS:D7F6 low nibble = weapon target class (verified use), high nibble (U).
4. Slot 2 `bp-CC` training template indices in P6 (computed 5 / 55) and the precise swap rule ordering (verified in code, intent U).
5. P9 unit placement: intent (scatter patrols/SAMs) is inferred; port it verbatim from 4C3D..4E0B rather than from this summary.
6. `unit_init` field +10h = 0xFC00, +1Ch ratio semantics (U); fields consumed by VGAME.
7. 0000:3517 palette queue max length (>6 entries?) (reader a).
8. DS:11DA template data dump (82 x 12 B) not decoded per entry; extract with a script when porting.

## 7. DGROUP globals seen
| DS offset | type | meaning | readers / writers |
|---|---|---|---|
| CACA | far ptr | pilot record (comm+1272h) | everywhere / startup |
| E096 | far ptr | comm base | 4239, 5625, 59FE, 5BF0 |
| B38E / B38C / CABE | w | copy of pilot theatre / war level / +3E | 40F0 writes; 4239 reads |
| D33C | w | number of sites | 54BB; 4239, 512E |
| CACE | w | leading sites with threat level | 54BB; 4239 P10 |
| E084 | w | first non-target site index | 54BB; 4239 |
| D88C | w | WLD id 'BN' | 54BB; 5625 |
| CBDE | 16*n | site table | 54BB, 512E, 51E8, 4239 |
| D890 | w | number of units | 54BB; 4239 |
| D3BE | 36*n | unit table | 54BB, 5356, 4239 |
| D7F6, D33E, D2A6 | 128 B each | type tables: D7F6 low nibble weapon class; D2A6 target category (0x15 excluded); D33E (U) | 54BB; 4239/5282 |
| CAD8 | 256 B | 16x16 land/flag grid | 54BB; 4239 P3/P11; 5625 |
| DD70 / DC70 | 750 B / 128 w | names / pointer table | 54BB |
| B390 / B392 | w / w | export cursor offset / segment | 5625, 585C, 588D |
| B394 | char[5] | grid reference result | 58DF |
| E05E, E070 | 2 x 18 B | mission slots (E060 target, E062 anchor, E064 flags, E066 tmpl, E068 grid str, E06E time); slot1 E072 E074 E076 E078 | 4239; 5625 |
| D876 | w | flight time estimate (ticks) | 4239; 5625 |
| CABC / CBDC | w / w | special unit index (-1 none) / its site | 4239; 5625 |
| D29A,D29C,D326,D328 | 2 x long | world position of anchor E062 | 4239 |
| 15B2..15C0 | 8 w | waypoints (mid, target, E072, E074) | 4239; 5625 |
| 0D7C, 0D82, 0D88 | char[6] | "HH:MM" strings (TOT, TOT+15, launch) | 4239 |
| 0D8E | char[5] | grid ref of special site | 4239 |
| 171A | 32 B * n | enemy aircraft type table (name at +0Eh) | 5356 |
| 1D86 | 16x16 B | weapon suitability [weapon][target class] | 5282 |
| D878 | 16 B | weapon excluded flags | 5282 |
| 1EC6 | 17 B | quantity by loadout id | 4239 |
| 1EE4 | 17 x 2 w | threat level remap tables A (+0) and B (+2) | 4239 P10 |
| 11DA | 82 x 12 B | mission templates | 4239 |
| 0D6A | 9 w | theatre prefix string ptrs (lb pg nc ce vn me ku cu ko) | 40F0, 5466 |
| 0DD8 | 9 b | theatre -> DAC colour index (FC F9 FE FD F8 FB FA FF F7) | 5A8A, 5BF0 |
| 0DE2 | 10 b | hotspot -> theatre (idx 1..9: 7 2 3 0 5 6 1 4 8) | 5BF0, 5E6F |
| 0DEC / 0DFA | 7 w each | marker x / y tables | 5A8A, 5BF0 |
| 6F6E | menu | orders hotspot table (17 entries) | 5BF0, 5E6F, 5A8A |
| 64E3 | 768 B | working palette (colour n at 64E3+3n); 64A6 queue, 64E2 count | 5A8A, 615C, 3517 |
| 11AC..11D2 | RGB | white, (3F..), grey, orange, red palette triples | 5A8A..5E6F |
| D096 | b | previous hover hotspot | 5E6F |
| B39A | b | tooltip frozen flag | 5BF0, 5E6F, 6045 |
| D2A2 | b | orders-issued flag (warning dialog) | 5BF0 |
| DC02, DC18 | 22 B each | sprite/text contexts (DC18 = text ctx: +4 colour DC1C, +6 DC1E, +C DC24) | 5A8A.. |
| CABA | w | draw/page segment for sprites | 3693 callers |
| E08A / E08C | w / w | cursor x / y | 393D, 3E84 |


---
# Part D: roster, pilot options, shell init, terrain

### S3.5 reader d: START.EXE 0000:6200..8010 (roster, pilot options, shell init, quit prompt, terrain tile lookup)

Source: `work/s35/d.txt` read in full (3130 lines), plus DGROUP bytes (image `0x0A95*16+off`), plus the call sites in
`a.txt`/`c.txt` where needed to fix argument meaning. "(V)" = verified in disassembly/data, "(U)" = unclear / needs a visual or
runtime check. Offsets `rec+N` / `pilot+N` are inside the 0x50-byte pilot record.

Range summary (24 functions): roster screen (623e..6db8, 71ad, 7218, 7272), pilot options screen (6db8, 71ad), shell start-up
(7312, 7387, 73ce, 74b1), quit prompt (7611), exit (76d1), rand helper (76c0), terrain tile map (7744, 7904, 7c05, 7c56, 7d71,
7e88). `800a` (dos_allocmem) is already in platform_symbols.csv. The tail of `615c` (6200..623d, rows/cursor of the roster screen
draw callback) belongs to reader c; its meaning is summarised in 1.2.

---------------------------------------------------------------------------------------------------------------------------
## 0. Shared conventions and helper glossary (all verified from the bodies in a.txt)

Near helpers called from this range (owned by readers a/b; names here are the ones I use in the spec, the csv of the owner wins):

| addr | proposed name | signature (cdecl, args pushed right to left) | what it does (V) |
|---|---|---|---|
| 3517 | pal_queue_push | `void(int first_dac, const uint8_t *rgb6, int count)` | appends `{first,ptr,count}` to a 6-byte-entry queue at DS:64A6 (counter DS:64E2); `pal_apply_queue` (8427, called from every `menu_pick` pass) writes the DAC. Colours are VGA 6-bit RGB. |
| 3567 | text_at | `void(TextCtx *c, const char *s, int x, int y)` | `c->x=x; c->y=y; slot 5 text_noclip(c,s)` |
| 2a84 | text_center_at | `void(TextCtx *c, const char *s, int cx, int y)` | `c->x = cx - text_width(c,s)/2; c->y=y;` draw |
| 373c | text_width | `int(TextCtx *c, const char *s)` | sum of MGRAPHIC slot 47 (`font_char_width(ch, c->font)`) |
| 3693 | sprite_draw | `void(TextCtx *w, int x, int y, unsigned seg)` | slot 22 `sprite_scaled(w,x,y, dw=seg:0.w0, dh=seg:0.w2, 0, seg)`: natural size draw of a pool sprite (header words = width, height) |
| 38ac | menu_pick | `int(Menu *m)` | one blocking pass until an event: `m->hover()` (if non-NULL) each pass, `input_poll()` (39f7); click(1) -> `hit_test(m->rects,m->count)` index 1..count (click on nothing: sound 0x22, returns 0 -> loop); right click/Esc (2) -> returns 0x63 (sound 0x23); other key == `m->key` -> returns `0x100+key`. Sound 0x1a on a valid click. |
| 393d | hit_test | `int(Rect *r, int n)` | 1-based index of the first rect with `x1<=mx<=x2 && y1<=my<=y2` using cursor DS:E08A/E08C, else 0 |
| 3989 | cursor_erase | `void(void)` | `blit_rect(1,E08E,E090 -> 0,E08E,E090, E092 x E093)` restores the screen under the software cursor from page 1 |
| 3cea | ui_msgbox | `int(MsgBox *b, int x /*0x56*/, int y /*0x46*/)` | modal two-button box (3.3), returns 1 (first button) / 2 (second) / 0x63. Used here only for "OK" (1) vs "No Way!" (2). |
| 3641 | ui_show_screen | `void(int pic_idx, void (*draw_cb)(void))` | fade_out(1); `pic_load(pic_idx,page1)` (35fc); `pal_load_slot(pic_idx)` (826c, PALETTES.PAL slot = idx); copy page1 -> screen; `draw_cb()`; fade_in(1); slot 70 screen_on |
| 35fc | pic_load | `void(int pic_idx, int page)` | `sprintf(n,"%s.pic",DS:CBA[pic_idx])`, read file into far buffer D338:D336, `pic_to_seg_b(page_seg[page], E086)` |
| 3088/3070 | fade_out(n)/fade_in(n) | `void(int steps)` | both tail-call 30a0(shadow DAC DS:64E3, 0x100/0 or 0/0x100, n) |
| 2b06 | wait_ticks | `void(int n)` | n x `pal_apply_queue` pass (one timer tick each) |
| 2864 | read_file_far | `unsigned(char *name, unsigned off, unsigned seg)` | open + `dos_read_far` whole file + close |
| 289a | read_file | `unsigned(char *name, void *buf)` | open + `dos_read` + close |
| 3e20 | cursor_set_sprite | `void(unsigned seg)` | `E088=seg; E092=byte[seg:0]; E093=byte[seg:2]; E094=E095=2; E08A=E08C=E08E=E090=0` |
| 3e84 | cursor_set_pos | `void(int x,int y)` | moves the cursor (E08A/E08C, and the mouse via 85A7 when present) |
| 39f7 | input_poll | `int(Menu *)` | joystick/mouse/keyboard poll + cursor draw; returns 0 none, 1 = left click or Enter, 2 = right click or Esc, **0x62 = Delete (scan 5300h) or Backspace**, else raw key; **0x1000 (Alt-Q) calls `ui_quit_prompt` (7611)** |

`Menu` layout (V, 38ac): `{ void (*hover)(void); uint8_t key; uint8_t count; Rect r[count]; }` with `Rect = {x1,y1,x2,y2}` words (inclusive).
`MsgBox` (V, 3cea/332e use): `{ char *text; char *btn1; char *btn2; int w,h; Menu menu(hover=3DA2,key=0,count=2, rect space) ; char text_buf[]}`
laid out at DS:710D etc.; the first word points at the text stored 0x1E bytes after the struct. Boxes seen in this range:

| DS addr | text (V, verbatim) | buttons | w,h |
|---|---|---|---|
| 710D | `Changing pilots will cancel your scheduled mission.` | `OK` / `No Way!` | 148,59 |
| 7244 | `This officer has retired. Please choose again or erase this entry.` | one button (count 1) | 120,46 |
| 715F | `U.S. Air Force regulations prohibit deceased officers from flying multi-million dollar aircraft.  Please choose again or erase this entry.` (two spaces after the full stop) | one button | 160,46 |
| 72EA | `Disk is write protected. Roster file will not be saved.` | one | 120,46 |
| 729D | `Original disk in drive. Roster file will not be saved.` | one | 120,46 |

Comm / globals used by this range (details in the final table): `comm = DS:E096` (far, off 0, seg DS:E098), active pilot record
`DS:CACA/CACC = comm+0x1272` (far), `DS:CBD8` = **far ptr to 0000:04F2, the F117.COM "first run" word**, `DS:CAC0` = far ptr to
0000:04F4 (graphics already initialised flag). NB the brief assumed CBD8 was the comm block; it is not.

### Pilot record (0x50 bytes, comm+0x1272 and DS:D892+0x50*i, ROSTER.FIL = word `DS:B39C` + 10 records; file is 802 bytes) (V unless noted)

| off | size | meaning |
|---|---|---|
| +00 | w | roster slot index 0..9 (set to `slot` when a name is entered; roster_save stores the active index into B39C) |
| +02 | char[30] | name (typed part only, max 29 chars + NUL; the rank prefix is not stored) |
| +20 | w | rank index 0..6 = `2nd Lt `,`1st Lt `,`Capt `,`Major `,`Lt Col `,`Col `,`B Gen ` (table DS:1D78, trailing space is part of the string) |
| +22 | w | Purple Heart count (PH) |
| +24 | w | Air Medal count (AM) |
| +26 | w | Distinguished Flying Cross count (DFC) |
| +28 | w | Silver Star count (SS) |
| +2A | w | Air Force Cross count (AFC) |
| +2C | w | Congressional Medal of Honor count (CMH) |
| +2E | w | best mission score ("Best Mission: %d") |
| +30 | w | last mission score ("Last Mission: %d") |
| +32 | dw | career score (ltoa, shown right aligned at x=224 in the roster row) |
| +36 | w | missions flown (itoa, right aligned at x=246) |
| +38 | w | theatre index (DS:9ABE strings Libya, Persian Gulf, North Cape, Central Europe, Vietnam, Middle East, ...; seen in a.txt 008d) |
| +3A | w | war scenario index: Cold War / Limited War / Conventional War (table DS:A155) |
| +3C | w | mission type: bit0 = Strike (vs Air), bit1 = Training: 0 Air Missions, 1 Strike Missions, 2 Air Training, 3 Strike Training (table DS:9522). New pilot = 3. |
| +3E | w | opponent level 0..3 = Green/Regular/Veteran/Elite Opponents (DS:931E) |
| +40 | w | landing realism 0..2 = No Crashes / Easy Landings / Realistic Landings (DS:94F0) |
| +42 | w | aircraft model 0 = `MicroProse F117A`, 1 = `Lockheed F117A` (DS:94AC). Selecting 1 forces the mission type to a Strike type (bit0 set) |
| +4E | w | status 0 Active / 1 Retired / 2 KIA (table DS:0E40 -> `Active`,`Retired`,`KIA`) |
| others | | +34 is the high word of the score, +44..+4D unseen here (U) |

---------------------------------------------------------------------------------------------------------------------------
## 1. Roster screen (ROSTSCRN)

### 1.1 `623e roster_screen_run` -> `int roster_screen_run(void)` (V)
Entry from the main shell function at 029D (a.txt). Sequence:
1. `*(uint16*)DS:CBD8 = 0` (clear the first-run word at 0000:04F2).
2. `ui_show_screen(7 /*rostscrn*/, roster_draw_cb /*615C*/)`.
3. Loop `r = menu_pick(&roster_menu /*DS:704E*/)`:
   * `r<1` -> loop. `r==11` (rect 11 = Done button 269,157,302,187) or `r==0x63` (right click/Esc) -> **finish** (below).
   * `r==0x162` (Delete/Backspace key, `key=0x62`): `i = hit_test(roster rects, 11)` at the *current cursor*; if `i` is 0 or >10 ignore;
     else if `DS:D2A2` (mission scheduled flag) is set and `ui_msgbox(0x710D)` returns 2 ("No Way!") ignore; else
     `roster_edit_name(i-1)` (see 1.5) then `D2A2 = 0`.
   * `1<=r<=10` (click on a pilot row, slot=r-1): `st = roster[slot].status` (+4E):
       - 0 Active: if `D2A2` and `ui_msgbox(0x710D)==2` ignore. `D2A2=0`. Old active row colour back to normal
         (`pal_queue_push(0xFF-active.idx, rgb 11CD, 1)`), copy active record -> `roster[active.idx]` (0x28 words), copy `roster[slot]` ->
         active, `active.idx = slot`; if `comm+0x22 == 0` { `active+0x38 = 0; active+0x3C |= 2` }; highlight the chosen row
         (`pal_queue_push(0x100-r, rgb 11C7, 1)`); then `roster_options_screen()` (6DB8).
       - 1 Retired: `ui_msgbox(0x7244,0x56,0x46)`.
       - 2 KIA: `ui_msgbox(0x715F,0x56,0x46)`.
4. **finish** (63CF): `cursor_erase()`; if `active.status != 0` show the retired / KIA box (same dispatch at 6294) and stay in the
   loop; else `D878 = D879 = (uint8)active+0x42` (aircraft model; write-only in the code I saw (U)), `roster_save(1)` (7272), return 0.

`roster_menu` (DS:704E, V): `hover=63FB, key=0x62, count=0x0B`, rects (x1,y1,x2,y2): rows `i=1..10`: `(28, 43+10(i-1), 292, 52+10(i-1))`,
rect 11 `(269,157,302,187)` = Done.

DAC convention: pilot row *i* (1-based) uses DAC entry `0x100-i` (0xFF..0xF6) as its text colour (set by `615C` with `ctx.fg=0xFF-i`);
colours (6-bit RGB, DS:11C1..): `11C1 (3F,00,00)` red = hover, `11C4 (39,3C,3B)` light grey, `11C7 (3E,29,1B)` orange = selected pilot,
`11CA (2A,2A,2A)`, `11CD (0A,12,0F)` dark = normal row, `11D0 (1F,29,24)` = normal option button.

### 1.2 `615C roster_draw_cb` (reader c; summarised) (V)
`pic_load(12 /*rostsprt*/, page 4)` (medal icons + options panel art); copy page1 -> page3 (clean background); draw the 10 rows with
`roster_draw_row(i, 44+10 i)` using font 1, fg = 0xFF-i; prints `Press 'Delete' key to erase a pilot` centred at (160,144) font 3
fg 0x1A; sets the shadow palette entry `0xFF-active.idx` := 11C7; blits rows page1 -> screen (x=30,y=44,w=270,h=110); places the cursor at
(160, 10*active.idx+47).

### 1.3 `63FB roster_hover_cb` -> `void roster_hover_cb(void)` (V)
`cur = (uint8)hit_test(DS:7052, DS:7051)`, `prev = DS:D096` (byte, 0 = none).
* `if (prev && cur != prev)`: unless `prev-1 == active.idx` -> `pal_queue_push(0x100-prev, 11CD, 1)`; if `prev<=10`: erase the info panel
  (`blit_rect(3,36,159 -> 1,36,159,224,41)` then `blit_rect(3,36,159 -> 0,36,159,224,41)`).
* `if (cur && cur != prev)`: unless `cur-1 == active.idx` -> `pal_queue_push(0x100-cur, 11C1 red, 1)`; if `cur<=10`: `roster_draw_info(cur-1)`.
* `D096 = cur`. (Bug-compatible: the "skip active row" test also protects row `idx+1` only.)

### 1.4 `64E5 roster_draw_info` -> `void roster_draw_info(int slot)` (V) - bottom info panel for the hovered pilot
All text via TextCtx `DS:DC18` (page 1), then page1 -> screen `blit_rect(1,36,159 -> 0,36,159,224,41)` at the end.
* `rec = D892+0x50*slot`. `ctx.font=4; ctx.fg=1`. `name = ui_str_last_word(rec+2)`; `sprintf(buf,"%s%s",rank[rec+0x20],name)`; `buf[24]=0`;
  `text_at(c,buf,50,159)`. Then `ctx.fg=0x1C` (28) for the rest.
* `sprintf("Best Mission: %d", rec+0x2E)` at (50,168); `"Last Mission: %d"` (rec+0x30) at (50,176).
* Option strings at x=162: y=160 `DS:931E[rec+0x3E]`, y=168 `DS:94F0[rec+0x40]`, y=176 `DS:94AC[rec+0x42]`.
* Medals row: `x=36`; for each of the six in this order, only if the counter is non-zero: `blit_rect(4, 0, sy -> 1, x, 191, 34, 8)`
  (icon from the rostsprt page 4) and `text_center_at(c, "XX:%d", x+17, 184)`, then `x += 38`:

| order | label (fmt) | counter | icon sy |
|---|---|---|---|
| 1 | `CMH:%d` | +2C | 0x98 |
| 2 | `DFC:%d` | +26 | 0xA0 |
| 3 | `AFC:%d` | +2A | 0xA8 |
| 4 | `SS:%d` | +28 | 0xB0 |
| 5 | `AM:%d` | +24 | 0xB8 |
| 6 | `PH:%d` | +22 | 0xC0 |

(font stays 4, fg 0x1C for labels.)

### 1.5 `6A7C ui_str_last_word` and `6AB3 roster_edit_name`
`6A7C char *ui_str_last_word(char *s)` (V): `i=strlen(s)-1; while (s[i]!=' ' && i>0) i--; if (i>0) i++; return s+i;` (surname, or the whole string when no space).

`6AB3 int roster_edit_name(int slot)` (V) - used by the Delete key on a row: "erase this entry and enter a new pilot".
`cursor_erase()`; `rec=D892+0x50*slot`; `y=10*slot+44`; `ctx(DC18).fg = 0xFF-slot; ctx.font = 1`.
Restore row background `blit_rect(3,30,y -> 1,30,y,158,8)`, `buf = rank[0] ("2nd Lt ")`, draw it at (30,y), draw `"_"` (DS:0E9E) with slot 5 at the end of the prefix,
`len0=strlen(buf)`, show page1 -> screen (158 x 8). Loop:
* key = Enter (0x0D) when `typed==29` or `text_width(ctx,buf) > 150`; else wait `kbd_empty()==0` (slot 90) and `kbd_get()` low byte (slot 91).
* Esc (0x1B): leave unchanged (jump to redraw). BS (8): drop the last typed char (not below `len0`). Enter (0x0D): commit: normal colour for the old
  active row (`pal_queue_push(0xFF-active.idx,11CD,1)`), selected colour for this row (`0xFF-slot` <- 11C7), `mem_zero(rec,0x50)`, `strcpy(rec+2, buf+len0)`,
  `rec.idx=slot; rec+0x3A=2 (Conventional War); rec+0x3C=3 (Strike Training); rec+0x4E=0`, copy `rec` -> active pilot (0x28 words) (rank 0, scores 0, options 0).
  Any other key: if `isprint(key)` (9694) append; else ignore.
* After each key: restore bg (158 wide), `text_at(buf,30,y)`, draw `"_"` (slot 5), blit to screen.
* Exit path (Esc and Enter): `roster_draw_row(slot,y)` (rebuilds the row from the roster array), then
  `blit_rect(1,30,y -> 0,30,y,270,8)`; returns 0. Note the done flag `[bp-0x34]` is never set; there is no other exit.
For Enter the row shows the new data immediately: `rec` is the roster array entry itself (`D892+0x50*slot`), which `mem_zero`/`strcpy` modify in place; the active record gets a copy.

### 1.6 `6861 roster_draw_row` -> `void roster_draw_row(int slot, int y)` (V)
Draws the roster row into page 1 (ctx DC18), no screen blit: `blit_rect(3,30,y -> 1,30,y,258,8)`; `rec=D892+0x50*slot`;
`sprintf("%s%s", rank[rec+0x20], rec+2)` at x=30; `ltoa(rec+0x32 lo,hi, 10)` right-aligned to x=224 (`x=224-text_width`);
`itoa(rec+0x36,10)` right-aligned to x=246; `strcpy(statusname[rec+0x4E])` centred: `x = 253 + (35-w)/2` (signed /2). The compiled code
contains the whole sequence **twice** (second copy identical, same ctx DC18) - harmless duplicate, a port can draw once.
Callers: 615C (rows 0..9), 6AB3.

### 1.7 `6D07 roster_startup_sync` -> `void roster_startup_sync(void)` (V)
Called once at program start from the main function (a.txt 0020), after `shell_init`. `roster_load()` (7218); then for the 10 records
`and word [DS:D8D4],1` (**always the same address = pilot 0 +0x42**: original bug, only pilot 0 is masked) and, for each pilot with `+0x42 != 0`
and `+0x3C ∈ {0,2}`, `+0x3C++` (Lockheed => Strike mission type).
Then: `if (*(word*)CBD8 != 0)` (F117.COM first run) copy `roster[ (uint8)B39C ]` -> active pilot (0x28 words); else (coming back from VGAME/END) copy active -> `roster[active.idx]`.
Then `if (comm+0x22 == 0) { active+0x38 = 0; active+0x3C |= 2; }`; `roster_save(0)` (silent).

### 1.8 `7218 roster_load` / `7272 roster_save` (V)
* `7218 void roster_load(void)`: `fp=fopen("Roster.Fil","rb")` (**not checked for NULL**), `fread(&DS:B39C,2,1,fp)` then `fread(DS:D892+0x50*i,0x50,1,fp)` i=0..9, `fclose`.
  Port: treat a missing/short file as zeros (the shipped `Game/ROSTER.FIL` exists, 802 bytes).
* `7272 void roster_save(int show_err)`: `B39C = (uint8)active.idx` (first byte of the active record); `if (fcb_find() != 0)` { `fp=fopen("Roster.Fil","wb")`;
  if ok: `fwrite(&B39C,2,1,fp)` + 10 x `fwrite(rec,0x50,1,fp)`, `fclose`; else if `show_err` `ui_msgbox(0x72EA)` (write protected) } else (`fcb_find()==0`) if `show_err`
  `ui_msgbox(0x729D)` (original disk). **`fcb_find` (804E) = INT 21h/11h FCB search for the volume label (extended FCB at DS:6187, attribute 08h) compared with
  `"F117A-SF"` (DS:617E); returns 0 when the original floppy is in the drive.** Port: always "not original disk" (write allowed), keep the messages for write failures.
  Callers of `roster_save(1)`: 623E finish, 6DB8 exit, c.txt 5DFE; `roster_save(0)`: 6D07.

---------------------------------------------------------------------------------------------------------------------------
## 2. Pilot options screen (drawn on top of the roster screen)

### 2.1 `6DB8 roster_options_screen` -> `void roster_options_screen(void)` (V)
Opened when an Active pilot row is clicked. Three option groups: (a) opponent level `+3E` (2x2 buttons), (b) landing realism `+40` (3 buttons),
(c) aircraft model `+42` (2 buttons) + a Done button.
1. `cursor_erase()`; `saved_d096 = D096; D096 = 0`; save cursor `(E08A,E08C)`; set cursor to (259,180) (inside Done).
2. `pal_queue_push(0xEB, DS:67A4, 10)` (DS:67A4 = shadow palette 64E3 + 3*0xEB: re-applies the default colours of DAC 0xEB..0xF4 = option buttons).
3. Save the panel: `blit_rect(1,192,40 -> 4,111,0,111,152)` (page 4 receives a copy at x=111; page 4 x=0..110 holds the rostsprt button art);
   `blit_rect(4,0,0 -> 1,192,40,111,152)` (draw the panel art onto page 1).
4. Draw the three current markers: sprite `DS:CABA` (12x8 marker) with `sprite_draw(DC18, 192+EA8[id], 40+EB2[id], CABA)` for ids
   `id1 = pilot+3E + 1`, `id2 = pilot+40 + 5`, `id3 = pilot+42 + 8` (DS:EA8 / DS:EB2 are 10-byte tables indexed by id 1..9; see table below);
   `blit_rect(1,192,40 -> 0,192,40,111,152)`.
5. Title: `buf = sprintf("%s%s", rank[active.+20], ui_str_last_word(D894+0x50*active.idx))`; TextCtx DC02 and DC18 `font=3, fg=0x1C`; typewriter: for `i=0..49`
   while `buf[i]`: `wait_ticks(rand()%6+6)`; `tmp[0..i]=buf[0..i]`; stop if `text_width(DC02,tmp)>82`; `snd_cmd(0x21)`; `text_at(DC02 and DC18, tmp, 217, 55)`.
6. Loop `r = menu_pick(&opt_menu /*DS:6FFA*/)` until done:
   * `r==0x63` or `r==10` -> done. `r==11` is unreachable (see menu table). `r==0`: no change.
   * `1..4`: `old = pilot+3E + 1; pilot+3E = r-1`. `5..7`: `old = pilot+40 + 5; pilot+40 = r-5`. `8..9`: `old = pilot+42 + 8; pilot+42 = r-8` and if
     `pilot+42 != 0 && (pilot+3C==0 || pilot+3C==2) pilot+3C++`.
   * For `r<10`: erase the old marker: `blit_rect(4, EA8[old], EB2[old] -> 1, EA8[old]+192, EB2[old]+40, 12, 8)` and the same to page 0; `snd_cmd(0x1B)`;
     draw the new marker `sprite_draw(DC18, EA8[r]+192, EB2[r]+40, CABA)` and `sprite_draw(DC02, ...)` (page 1 and screen).
7. Exit: `blit_rect(4,111,0 -> 1,192,40,111,152)`, `blit_rect(4,111,0 -> 0,192,40,111,152)` (restore the roster picture under the panel);
   `D096 = saved_d096`. If `r==0x63` (cancel): copy `roster[active.idx]` -> active (discard edits), cursor back to the saved position. Else (Done / id 10): copy active ->
   `roster[active.idx]`, cursor to (297,182) (over the roster's Done button). No `roster_save` here (the file is written when the roster screen is left, 623E finish).
   (The `call 0x7272` at 6DAE belongs to `6D07`; the options screen ends at 71AC.)

`opt_menu` (DS:6FFA, V): `hover=71AD, key=0, count=0x0B` but only 10 rects are real; rect 11 overlaps the first 8 bytes of `roster_menu` (`x1=0x63FB`),
so it can never be hit (original off-by-one, harmless; port with count 10):

| id | rect (x1,y1,x2,y2) | meaning |
|---|---|---|
| 1,2 | (195,73,247,82), (195,83,247,93) | opponent level 0 Green, 1 Regular |
| 3,4 | (248,73,300,82), (248,83,300,93) | opponent level 2 Veteran, 3 Elite |
| 5,6,7 | (195,106,300,115), (195,116,300,125), (195,126,300,136) | landing 0 No Crashes, 1 Easy, 2 Realistic |
| 8,9 | (195,149,300,159), (195,160,300,172) | aircraft 0 MicroProse F117A, 1 Lockheed F117A |
| 10 | (195,175,300,187) | Done |

Marker positions `x = 192+EA8[id]`, `y = 40+EB2[id]` (V from DS tables EA8 = `00 28 28 5B 5B 52 52 52 52 52`, EB2 = `00 21 2B 21 2B 43 4C 55 6E 78`):
id1 (232,73), id2 (232,83), id3 (283,73), id4 (283,83), id5 (274,107), id6 (274,116), id7 (274,125), id8 (274,150), id9 (274,160).

### 2.2 `71AD roster_options_hover_cb` (V)
`cur = (uint8)hit_test(DS:6FFE, DS:6FFD)`, `prev=D096`. If `prev && cur!=prev`: `pal_queue_push(0xF5-prev, 11D0, 1)`; if `cur && cur!=prev`: `pal_queue_push(0xF5-cur, 11C1 red, 1)`; `D096=cur`.
So button *i* is drawn in DAC `0xF5-i` (0xF4..0xEB).

---------------------------------------------------------------------------------------------------------------------------
## 3. Shell start-up, exit, quit prompt

### 3.1 `7312 shell_init` -> `void shell_init(void)` (V) - first call of the program (a.txt 0010)
1. `shell_comm_setup()` (7387).
2. `ovl_bind(comm+0x1E)` (MISC), `ovl_bind(comm+0x1A)` (MGRAPHIC), `ovl_bind(comm+0x1C)` (sound) (RE_GUIDE 2.3: START binds `[1A],[1C],[1E]`; order here is 1E,1A,1C).
3. `E086 = D338 = mem_alloc_chk(0x5DC0)` (24000 bytes), `D336 = 0` - **then 73CE does the same allocation again** (the first block is leaked, never freed; port: allocate once).
4. `shell_gfx_init()` (73CE); `shell_sprites_init()` (74B1); `timer_install()` (8CA8).
5. `if (comm+0x72 & 1) joy_set_calib(far comm+0x48)`; `if (comm+0x72 & 2) mouse_init()`; `srand(bios_timer())`; `D2A2 = 0`.

### 3.2 `7387 shell_comm_setup` (V)
`CBD8 = 0000:04F2`, `CAC0 = 0000:04F4`, `seg = *(uint16 far *)0000:04F0`; `E098 = seg; E096 = 0` (comm = seg:0); `CACC = seg; CACA = 0x1272` (active pilot far pointer).

### 3.3 `73CE shell_gfx_init` (V)
* `E086 = D338 = mem_alloc_chk(0x5DC0); D336 = 0` (scratch/file buffer, also pic work area).
* if `*(word far*)CAC0 == 0` (first program of the session): slot 69 `screen_off`, slot 60 `mode13_set(comm+0x24 /*joystick calibration byte*/)`, `*CAC0 = 1`.
* slot 14 `page_select(0)`. For `i=0..4`: `page_seg[i] = DS:CAB0[i] = gfx_alloc(i)` (slot 0; i=0 -> A000h); if 0 -> `shell_exit(1)`; for `i!=0`: slot 75 `page_seg_set_b(page_seg[i], i)`.
  `TextCtx DS:DC02+0x16*i` initialised: `page=i, ymin=0, ymax=199, xmin=0, xmax=319`. (DC02 = screen/page 0, DC18 = page 1, DC2E.. pages 2-4.)
* `D094:D092 = (seg of mem_alloc_chk(0x1B00), 0)` (6912 bytes = 9 palettes); failure -> `shell_exit(1)`; `read_file_far("palettes.pal", 0, D094)`.
Pages (V from use): 0 screen (A000), 1 off-screen compose page, 2 sprite-source page for the init grabs, 3 clean copy of the current screen background, 4 rostsprt/button art.

### 3.4 `74B1 shell_sprites_init` (V)
`pic_load(11 /*requestr*/, page 2)`; slot 76 `pool_init` (0 -> `shell_exit(1)`); ten `sprite_grab(page=2, x, y, w, h)` (slot 77) into DS words:

| DS | x,y | w,h | use |
|---|---|---|---|
| D29E | 312,161 | 3x5 | small 3x5 sprite (U: HUD/map marker) |
| D330 | 232,0 | 88x101 | big sprite (U: requester/help image) |
| CABA | 308,176 | 12x8 | **option marker** (used in 2.1) |
| D32E | 309,184 | 11x16 | **mouse cursor sprite** (`cursor_set_sprite(D32E)`) |
| D08E | 315,161 | 5x5 | small sprite (U) |
| D88E | 317,156 | 3x5 | small sprite (U) |
| E082 | 306,161 | 3x5 | small sprite (U) |
| E09A | 309,161 | 3x5 | small sprite (U) |
| E09C | 314,158 | 3x3 | small sprite (U) |
| CAC4 | 306,166 | 14x10 | sprite (U) |

then slot 78 `pool_free` (0 -> `shell_exit(1)`), `cursor_set_sprite(D32E)`.

### 3.5 `76D1 shell_exit` -> `void shell_exit(int code)` (V)
`snd_cmd(0)` (slot 101); `if (E086) mem_free_chk(E086)`; `if (DS:73F2) break_hook_remove()`; `if (DS:ADFB) timer_remove()`; `if (code <= 1)` { `int86(0x10, AX=0003h)` (text mode) };
slot 94 `kbd_locks_clear`; `if (code == 1) { printf("Not enough memory!"); code = 0; }`; `exit(code)`. Other call sites use codes 0x0C/0x0D (a.txt 0088:
chosen by `test byte [bx-0x3419],2` on a theatre/climate table) => process chaining codes read by F117.COM, no text mode/no message (U: meaning of 12/13 is for the reader of a.txt).

### 3.6 `7611 ui_quit_prompt` -> `void ui_quit_prompt(void)` (V) - Alt-Q
`snd_cmd(0)`; `mem_copy(DS:64E3 -> local, 0x300)` (save shadow DAC); `fade_out(1)`; `pic_load(0x1F /*adv*/, page 0)` (straight to the screen); `read_file("adv.pal", DS:64E3)`;
`fade_in(1)`; wait for a key (`kbd_empty()` slot 90 until 0, `kbd_get()` slot 91); `c = ax & 0x5F`; if `c == 'Y'` -> `shell_exit(0)`; else `fade_out(1)`; `blit_rect(1,0,0 -> 0,0,0,320,200)`; restore the shadow
palette from the local copy; `fade_in(1)`. The question text is part of `adv.pic` (no strings in the code). Callers: `input_poll` (3AE4) and a.txt 3F19, 4028.

### 3.7 `76C0 ui_rand_mod` -> `unsigned ui_rand_mod(unsigned n)` (V): `rand() % n` (unsigned div). Many callers in a/b.

---------------------------------------------------------------------------------------------------------------------------
## 4. Terrain tile map and object records (theatre `.3DG` / `.3DT`, same format as VGAME; FORMATS.md "Terrain")

Loaded by `5466` (c.txt) with the 2-letter theatre prefix: `7d71("XX.3dG")` then `7e88("XX.3dT")`. Both always return 0 (callers ignore the value).
Coordinates: **world units** = level-1 units; the theatre is 100000h x 100000h; a level-L cell is 1000h level units; `level units = world / 1, 4, 16, 64` for
L = 1, 2, 3, 4 and `world*2` for L = 0. WLD site coordinates -> world: `x_world = wld_x << 5`, `t = (0x8000 - wld_y) << 5` (terrain y runs north) (512E does exactly this).

### 4.1 `7D71 map_load_3dg` -> `int map_load_3dg(char *name)` (V)
`fopen(name,"rb")`. On failure: `DS:BAA0[i]=i (i=0..15)`; `memset` zero: `B9A0` (256), `B7A0` (512), `B5A0` (512), `B3A0` (512); return 0.
On success: `fread(&DS:104C,2,1)` must be `0x3232` ("22"), else fclose, return 0. Then reads: 16 bytes -> `BAA0` (L4 4x4 table), 256 -> `B9A0` (L3 16x16 map),
512 -> `B7A0` (L2 children), 512 -> `B5A0` (L1 children), 512 -> `B3A0` (L0 children); fclose.

### 4.2 `7E88 map_load_3dt` -> `int map_load_3dt(char *name)` (V)
`fopen(name,"rb")` (fail -> 0); magic `0x3131` ("11") at `DS:104E` else 0. `fread(DS:0F00, 2, 5)` = number of tile types per level (each must be `<= 0x20`, else abort).
Per level `i=0..4`: `fread(DS:0F0A + 64*i, 2, type_count[i])` = number of records per (level,type). Then records, level-major then type-major, pool pointer `p=0`:
for each `(i,j)`: `DS:C85C[i*32+j] = 0xBAB0 + p`; for each of `F0A[i*32+j]` records: `fread` x (2 bytes -> `BAB0+p`), y (`BAB2+p`), z (`BAB4+p`), obj word whose **low byte** goes to
`BAB6+p`; `p += 7`; abort (return 0, state partially built) if `p > 0xDAC` (pool = 500 records, ends where the pointer table `C85C` starts). `fclose`.
Record (7 bytes): `int16 x, y, z; uint8 obj` - x,y relative to the cell centre (+800h), obj = object id (bank/index), see FORMATS.

### 4.3 `7C56 map_tile_type` -> `int map_tile_type(int level, int x, int y)` (V)
Returns the tile type 0..31 at cell `(x,y)` of level `level`, or -1 if `x<0 || y<0 || x>=size[level] || y>=size[level]` (`size = DS:107E = {1024,256,64,16,4}`):
`L4: BAA0[y*4+x]` (`BAA0` byte table (also read as `DS:BAA0`)); `L3: B9A0[y*16+x]`;
`L2: B7A0[ (map_tile_type(3, x>>2, y>>2) << 4) + ((y&3)<<2) + (x&3) ]`; `L1: B5A0[ (tile(2,x>>2,y>>2)<<4) + (y&3)*4 + (x&3) ]`; `L0: B3A0[ (tile(1,x>>2,y>>2)<<4) + ... ]`.
(Parent level of L is L+1; the table pointer `-0x4C60/-0x4A60/-0x4860` = B3A0/B5A0/B7A0.) Callers: 7744, 7904.

### 4.4 `7C05 map_level_scale` -> `long map_level_scale(int level, long v)` (V)
`L0: v<<1; L1: v; L2: v>>2; L3: v>>4; L4: v>>6` (logical shifts via `rtl_aFulshr`, 97F4): world units -> level units.

### 4.5 `7744 map_nearest_object` -> `struct ObjHit *map_nearest_object(long wx, long wy)` (V; returns NULL (0) if nothing)
`wx = wld_x*32`, `wy = (0x8000 - wld_y)*32` (arguments at bp+4 = wx lo/hi, bp+8 = wy lo/hi).
`best = 0x7FFF`. For `L = 1,2` and for the 9 neighbour offsets `k` in order `(dx,dy) = (-1,1) (1,1) (1,-1) (-1,-1) (0,1) (1,0) (0,-1) (-1,0) (0,0)` (tables DS:105A / DS:106C):
`lx = map_level_scale(L,wx)`, `ly = map_level_scale(L,wy)`, `cx = (lx>>12)+dx`, `cy = (ly>>12)+dy`; `off_x = dx*0x1000 - (lx&0xFFF) + 0x800`, `off_y = dy*0x1000 - (ly&0xFFF) + 0x800`
(centre of the neighbour cell relative to the query point); `t = map_tile_type(L,cx,cy)`; skip if -1. For every record `j` of `(L,t)` (`ptr = C85C[L*32+t]`, count `F0A[L*32+t]`)
whose class `DS:D2A6[rec.obj] != 0` (class table loaded from the WLD, 128 bytes): `px = rec.x+off_x`, `py = rec.y+off_y`, `d = |px|+|py|`; **L1: `d >>= 2`; L2: `px,py <<= 2`** (so `d` is in
L2 units, positions in world units). If `d < best`: store the hit: `CA9C=L, CA9D=j, CA9E=(uint8)cx, CA9F=(uint8)cy, CA9A=rec ptr, CA8E=rec.obj, CA90=d, CA92(long)=px+wx, CA96(long)=py+wy`.
Returns `&CA8E` (struct below) if `CA90 != 0x7FFF`, else 0.

```c
struct ObjHit {          /* DS:CA8E (V) */
  uint16 obj;            /* CA8E  object id (low byte of the record) */
  uint16 dist;           /* CA90  best distance (L2 units), 0x7FFF = none */
  int32  x;              /* CA92  absolute world x */
  int32  y;              /* CA96  absolute terrain y (north), WLD y = 0x8000 - (y>>5) */
  uint16 rec;            /* CA9A  near ptr to the 7-byte record */
  uint8  level, index, cx, cy;   /* CA9C..CA9F */
};
```
Caller 512E (c.txt): `hit = 7744(wx,wy)`; `wld_x = hit->x >> 5`, `wld_y = 0x8000 - (hit->y >> 5)`; stores id `hit->obj + 0x100`; table at DS:CBE0 (16-byte entries, count DS:D33C).

### 4.6 `7904 map_pick_object` -> `struct ObjHit *map_pick_object(int obj_id)` (V; returns NULL if none)
Collects all records whose `obj == obj_id` into the candidate list at far `D338:D336` (8-byte entries: `far ptr rec (off,seg=DS)`, `+4 x byte`, `+5 y byte`, `+6 level`) and returns a **random** one.
* If `DS:D2A6[obj_id] == 0x10` (large, level-3 objects): for `x=0..15`, `y=0..15` skip cells where `DS:CBC8[x - 16*y] & 3` (i.e. WLD 16x16 grid `CAD8[(15-y)*16+x]`, bits 0|1 = excluded) and
  `t = map_tile_type(3,x,y)`; scan the `(3,t)` record list; entries `{rec, x<<4, y<<4, level 3}`.
* Else: for `L=1` (step 1) then `L=2` (step 4): for `a = step, 2*step, ... < 0x100` (x) and `e = step, 2*step ... < 0x100` (y) (**index 0 is skipped, quirk**) skip cells
  whose level-3 cell is excluded (`CBC8[(a>>4) - (e&0xF0)] & 3`), `t = map_tile_type(L, a, e)` for L1 or `map_tile_type(2, a>>2, e>>2)` for L2; entries `{rec, a, e, L}`.
* count 0 -> return 0. Pick `entry = list[rand() % count]` (`ui_rand_mod`, none when count==1). Fill `ObjHit`: `obj=rec.obj`, `x = (a<<12)`, `y = (e<<12)` (32 bit) then add by level:
  L1: `+ (rec.x+0x800)` / `(rec.y+0x800)`; L2: `+ ((rec.x+0x800)<<2)` / same y; L3: `+ ((rec.x<<4)+0x8000)` / same y (16-bit shift wrap as in the code). Returns `&CA8E` (only `obj,x,y` are filled; `dist`,`rec`,... keep old values).
Callers: 51E8 (c.txt) (nearest-type search wrapper like 512E).

---------------------------------------------------------------------------------------------------------------------------
## 5. Driver slots called from this range (V)

| slot | name (video_symbols / RE_GUIDE) | where |
|---|---|---|
| 0 | gfx_alloc | 73CE |
| 5 | text_noclip (via text_at) | 6AB3, 6DB8, 3567 |
| 14 | page_select | 73CE |
| 22 | sprite_scaled (via sprite_draw 3693) | 6DB8 |
| 42 | blit_rect | 63FB, 64E5, 6861, 6AB3, 6DB8, 7611, 3989 |
| 47 | font_char_width (text_width 373C) | 6861, 6AB3, 6DB8 |
| 60 | mode13_set | 73CE |
| 69 | screen_off | 73CE |
| 75 | page_seg_set_b | 73CE |
| 76 / 77 / 78 | pool_init / sprite_grab / pool_free | 74B1 |
| 90 / 91 | kbd_empty / kbd_get (MISC) | 6AB3, 6DB8 indirectly, 7611 |
| 94 | kbd_locks_clear (MISC) | 76D1 |
| 101 | snd_cmd | 6DB8 (0x21 type tick, 0x1B click), 7611 / 76D1 (0 = stop) |

Sound commands seen: 0x1B (option click), 0x21 (name type-in tick), 0x1A (menu click, in 38ac), 0x22 (click on nothing), 0x23 (right click), 0 (stop).

---------------------------------------------------------------------------------------------------------------------------
## 6. Open items
1. Meaning of `comm+0x22` (SETUP sets 1): when 0, START forces `pilot+0x38=0` (Libya) and `pilot+0x3C|=2` (training). Probably "full campaign unlocked / not the demo" (U).
2. `D878/D879` (copies of aircraft model `pilot+0x42`) are written in 623E and a.txt 07FE/0E8C but I saw no reader (U; may be read in a/b/c).
3. Layout of the `MsgBox` rect area and the button text of the one-button boxes (owner: reader a, 3cea/332e); I did not trace 332E.
4. Small sprites D29E, D08E, D88E, E082, E09A, E09C, CAC4, D330 (requestr page): purpose (map/HUD markers, requester art) (U); needs the callers in a/b/c.
5. The ROSTER row draw in 6861 is duplicated; original bug in 6D07 (`and [D8D4],1` always pilot 0); `opt_menu.count` 11 vs 10; `shell_init` allocates the 24000-byte buffer twice (leak). Port: reproduce behaviour not bugs unless noted.
6. Whether the `0x1000` Alt-Q key can occur while a modal `ui_msgbox` is open (calls `ui_quit_prompt` from input_poll anywhere) - port: handle in `input_poll`.
7. `shell_exit` codes 0x0C/0x0D (a.txt 0088) - chaining protocol with F117.COM; check RE_GUIDE 2.1 (U).
8. U-check suggestions: roster screen: click a row -> options screen (markers on current options, typed title "Rank Surname" at (217,55)); Delete on a row -> name entry with "2nd Lt " prefix and `_` cursor; Alt-Q -> adv.pic prompt.

---------------------------------------------------------------------------------------------------------------------------
## 7. DGROUP globals seen (DS offset | type | meaning | readers/writers)

| DS off | type | meaning | readers/writers |
|---|---|---|---|
| CBD8 | far ptr | -> 0000:04F2, F117.COM "first run" word | 7387 (init), 623E (clear), 6D07 (read), a.txt 0013 |
| CAC0 | far ptr | -> 0000:04F4, "graphics initialised" word | 7387, 73CE |
| E096/E098 | far ptr | comm block (seg from 0000:04F0, off 0) | 7387; everywhere |
| CACA/CACC | far ptr | active pilot record = comm+0x1272 | 7387; 623E, 6D07, 6DB8, 63FB, 6AB3, 7272 |
| B39C | word | selected roster index (ROSTER.FIL header) | 7218 read, 7272 write, 6D07 |
| D892 | struct[10] x 0x50 | roster array | 7218, 623E, 64E5, 6861, 6AB3, 6D07, 6DB8 |
| D8D4 | word | pilot0 +0x42 (masked by the 6D07 quirk) | 6D07 |
| D2A2 | byte | "mission scheduled" flag (message 710D guards pilot change) | 623E, 6AB3 path, 7312 (clear), a.txt |
| D096 | byte | previous hover item index (shared by all menus) | 63FB, 71AD, 6DB8 (save/restore), a.txt/c.txt |
| D878/D879 | bytes | copies of `pilot+0x42` | 623E (write) |
| DC02 | TextCtx[5] x 0x16 | per-page text/draw context (DC02 page 0, DC18 page 1, ...) | 73CE init; all drawing |
| CAB0 | word[5] | page segments (gfx_alloc) | 73CE; 35fc |
| CABA, D32E, D29E, D330, D08E, D88E, E082, E09A, E09C, CAC4 | words | sprite segments from the pool (3.4) | 74B1; 6DB8 uses CABA |
| E088 | word | cursor sprite segment | 3E20 |
| E08A/E08C | words | cursor x, y | 6DB8, 3E84, 3989.. |
| E08E/E090, E092/E093 | words/bytes | last drawn cursor pos, cursor w,h | 3E20, 3989 |
| E086 | word | segment of the 24000-byte scratch/file buffer | 7312, 73CE, 76D1 (free), 35FC |
| D336/D338 | far ptr | same buffer (off 0, seg) | 7312, 73CE, 35FC, 7904 (candidate list) |
| D092/D094 | far ptr | PALETTES.PAL buffer (6912 bytes) | 73CE, 826C |
| 64A6 / 64E2 / 64E3 | 6B x10 / byte / 768 B | pal queue / its count / shadow DAC | 3517, 8427, 30A0, 7611 |
| 67A4 | 30 B | shadow DAC entries 0xEB..0xF4 (= 64E3+3*0xEB) | 6DB8 |
| 67E0 | 3 B | shadow DAC entry 0xFF | 615C |
| 73F2, ADFB | bytes | break hook installed / timer installed | 76D1 |
| 11C1..11D2 | 6 x RGB6 | UI colours (see 1.1) | all |
| 0E40, 0E08.. | ptr[3], strings | status names | 6861 |
| 1D78 | ptr[7] | rank strings | 64E5, 6861, 6AB3, 6DB8 |
| 931E / 94F0 / 94AC | ptr tables | opponent level / landing / aircraft strings | 64E5 |
| EA8 / EB2 | byte[10] | marker x/y offsets per option id | 6DB8 |
| 6FFA / 704E | Menu | options menu / roster menu | 6DB8 / 623E |
| 710D, 7244, 715F, 72EA, 729D | MsgBox | boxes | 623E, 7272 |
| CBA | ptr[32] | pic names (`air_id` ... `adv`, idx 0x1F) | 35FC |
| 104C/104E | words | magic buffers "22"/"11" | 7D71, 7E88 |
| F00 / F0A / FCA | w[5] / w[5][32] / (U) | tile-type count per level / record count per (level,type) / (unused here) | 7E88, 7744, 7904 |
| C85C | near ptr[5][32] | first record of (level,type) | 7E88, 7744, 7904 |
| BAB0 | 3500 B | record pool (7-byte records) | 7E88, 7744, 7904 |
| B3A0, B5A0, B7A0, B9A0, BAA0 | 512,512,512,256,16 B | tile tables L0..L4 | 7D71, 7C56 |
| 107E | word[5] | grid sizes 1024,256,64,16,4 | 7C56 |
| 1050..1058 | int[5] | -2000h,-1000h,0,1000h,2000h (T[d]=d*0x1000 at 1054+2d) | 7744 |
| 105A / 106C | int[9] | neighbour dx / dy tables | 7744 |
| D2A6 | byte[128] | object class table (from the WLD) | 7744, 7904 |
| CAD8 | byte[256] | WLD 16x16 level-3 cell flags (bits 0|1 exclude) | 7904 (as CBC8 + x - 16y) |
| CA8E..CA9F | struct | ObjHit result | 7744, 7904 |
| D33C | word | count of cached object entries (c.txt 512E table at CBE0) | c.txt |



# Part E: closing pass (S3.10b, 2026-10-13): the last 6 uncovered functions of START

`shell_symbols.csv` now has a row for every indexed START function (267 of 267). The six below are C runtime / library pieces.
Method and the dead-code test: `frontend.md` section 8. Corrections to the S3.10 twin rows of START (`885E`, `A768`) are in
`frontend.md` 8.5.

| Addr | Name | Class | Description |
|---|---|---|---|
| `8BD2` | `mem_copy_far_w` | dead | `void mem_copy_far_w(void far *src, void far *dst, unsigned nwords)`: `rep movsw`. No call, no address-taken reference anywhere in START |
| `932E` | `fwrite` | live (RTL) | `unsigned fwrite(void *buf, unsigned size, unsigned n, FILE *f)`, the MSC stdio routine (near twin of MPS_LOGO `0146:049A`; the two differ only in the large-model call/return forms). The one caller is `roster_save` (`7272`, writes `ROSTER.FIL`). Behaviour: `total = size * n`; copies into the stream buffer while it has room; once a whole buffer (or more) remains it flushes the stream and writes the largest multiple of the buffer size straight to the handle (`rtl_write`, `A31A`); the remainder goes through `rtl_flsbuf` (`9992`) byte by byte; a short write sets the error bit `20h`. Returns the number of complete items written (`n` when everything was written). Port: C `fwrite` |
| `963E` | `memset` | live (RTL) | `void *memset(void *d, int c, unsigned n)`: fills with `rep stosw` after a leading `stosb` for an odd start address and a trailing `stosb` for an odd count; returns `d`; `n = 0` returns at once. One caller (`7D71`). Port: C `memset` |
| `9694` | `isprint` | live (RTL) | `int isprint(int c)`: `AL = 57h` (`_UPPER\|_LOWER\|_DIGIT\|_PUNCT\|_BLANK`), then the shared tail `96A1`. Caller: `roster_edit_name` (`6AB3`, which characters a typed pilot name accepts). Port: C `isprint` for ASCII (the table holds only ASCII classes; characters above 7Fh are 0) |
| `96A1` | `ctype_test` | live (RTL) | shared tail of the MSC `isXXX` stubs at `9680..969E` (masks `80h` xdigit, `08h` space, `10h` punct, `07h` alnum, `57h` print, `17h` graph, `20h` cntrl; the other stubs are not indexed and unused by START): `return mask & _ctype[c + 1]` with the table at `DS:B050` (indexed by `c` at `B051`). The indexer decodes the bytes as `and al,[bx-4FAFh]`, which is the same instruction |
| `A3CB` | `rtl_write_flush` | live (RTL) | the line-buffer flush of `rtl_write` (= VGAME `FD55`): writes `[DX, DI)` to the handle `[bp+4]` with `INT 21h/40h`, adds the count to `[bp-2]`; on a short or failed write unwinds the frame and returns the count or sets the DOS error (`ah = 9` / `1C00h` on a full disk unless the text-mode Ctrl-Z rule applies). The block also contains the unsplit `ltoa` core `A457..A4B6` (same routine as PLAYER `1F70`) |
