# F-117A Nighthawk Stealth Fighter 2.0 — SDL3 port

A faithful native port of MicroProse's *F-117A Nighthawk Stealth Fighter 2.0* (1991) in C with SDL3. It is not an
emulator: the game's logic is re-implemented function by function, and the port reads the original game files at
run time. The release does not contain the game. You need your own copy.

Version 0.1.0 (first release, Windows x64). What changed and what is still open: [CHANGELOG.md](CHANGELOG.md).

## How to play

1. **You need the original game.** Its files, copied from your own installation: `F117.COM`, `SETUP.EXE`,
   `MPS_LOGO.EXE`, `PLAYER.EXE`, `START.EXE`, `VGAME.EXE`, `END.EXE`, `DSWAP.EXE`, `MGRAPHIC.EXE`, `MISC.EXE`, and
   the data files that come with them. The launcher checks the list above and tells you which one is missing.
2. **Unzip** `F117-SDL3-v0.1.0-win64.zip` into a folder of your choice.
3. **Copy the game files into `Game`**, the folder beside `F-117A Nighthawk.exe` (it is in the zip, with a
   `PUT_GAME_FILES_HERE.txt` note in it). The folder must be writable: the game saves your pilot roster there.
4. **Run `F-117A Nighthawk.exe`** and press **Play**. The launcher stays open; the game starts in its own window.
5. **SETUP** (the first program): answer **N** for the joystick and **2** for AdLib (the sound card the port
   plays). Then the intro, the title, START: create a pilot, pick a mission and launch.

The keyboard is the main input. A joystick is supported; the mouse and the joystick in the shell and the debrief
are not yet tested (see the CHANGELOG).

### The launcher

`F-117A Nighthawk.exe` sets the options and starts `f117.exe`. Everything is remembered in
`%APPDATA%\F-117A Nighthawk\settings.ini`; delete the file to go back to the defaults.

* **Game files**: **Folder** (default `Game` beside the launcher) and **Program** (`f117.exe` beside it). The line
  under them names what is missing. **Play** stays greyed out until both are there. The (i) icon next to the
  folder's Browse button is a reminder that the folder must be writable.
* **Aspect**: 4:3, as on a VGA monitor (the default), or 1:1 square pixels.
* **Window size** and **Start in full screen**. Alt+Enter switches full screen in the game.
* **Sound**: off starts the game without sound.
* **Game speed** (default **7**): the original paces itself to about 17 frames a second on any fast PC, which makes
  radar dishes spin far too fast. The value is the number of timer ticks (70 a second) that each frame takes at
  least: 7 gives about 10 frames a second. The flight model is scaled to the measured rate, so the aircraft still
  flies at the same speed. **0** is the original pacing, the fast spin of the original on today's PCs.
* **Key Bindings...**: every action of the game with its key, on four tabs (Flying, Weapons and countermeasures,
  Views, Displays and game). Click a key's button, press the new key (Shift, Ctrl and Alt can be held with it).
  **Reset to defaults**, **OK**, **Cancel**. Changed keys are shown in bold and saved in `settings.ini`. The keys that cannot be changed are listed under
  **Static keys**.
* Hover over an option for a short explanation.
* **About**: version, author and links.

### Main keys (defaults)

| Action | Key |
|---|---|
| Stick (pitch and roll) | keypad arrows (the grey cursor keys) |
| Throttle up / down; full / idle | `=` / `-`; Shift+`=` / Shift+`-` |
| Gear / flaps / bay doors / autopilot | `6` / `9` / `8` / `7` |
| Wheel brakes / eject | `0` / Shift+F10 |
| Select weapon / gun / release | Space / Backspace / Enter |
| Flare / chaff / IR jammer / ECM jammer / decoy | `1` / `2` / `3` / `4` / `5` |
| Drop targets / master mode | `B` / F2 |
| Views: front / chase / zoom in and out | F1 / Shift+F1 / `Z` / `X` |
| Look left / right / rear / ahead | `M` / `,` / `.` / `/` |
| MFDs: left / weapons / FLIR / nav / page | F3 / F5 / F6 / F7 / F10 |
| HUD mode / HUD style | `V` / `C` |
| Pause / sound on and off | Alt+`P` / Alt+`V` |

Alt+Q, Alt+B, Alt+S and Alt+Enter keep their jobs in the game. The launcher's Key Bindings dialog lists the rest
(56 actions in all) and is the place to change them.

### Saves, logs and errors

* **Pilot roster**: `Game\ROSTER.FIL`, written by the game like the original.
* **Errors**: `f117.log` beside `f117.exe` gets one dated line for each error the game reports. Attach it to a bug
  report.

### Command line

The launcher runs `f117.exe`; you can also run it directly. The usual options:

```text
f117 [--game-dir DIR] [--scale N] [--aspect 4:3|1:1] [--fullscreen] [--headless]
     [--trace] [--check] [--chain-test] [--tick-test [MS]] [--keys NAME=CODE,...]
     [--frame-ticks N] [--keymap-test] [--version]
```

* `--game-dir DIR`: the folder with the original files (default `Game`).
* `--scale N`: the window is 320x240 times N (default 3). `--aspect 4:3|1:1` and `--fullscreen` as in the launcher.
* `--frame-ticks N`: the game speed (default 7; 0 = the original pacing).
* `--keys NAME=CODE,...`: key bindings for the actions not on their defaults (the launcher passes its changes this way).
* `--check`: checks that the game folder holds the files the game needs and exits.
* `--trace`: prints the program switches and their exit codes (also `F117_TRACE=1`).
* `--headless`: no window and no sound, for tests. `--version` prints the version and exits.

The remaining test options are listed at the top of `f117port/src/main.c`.

## Building

MSYS2 `mingw64` (gcc, ninja, cmake, and the packages `mingw-w64-x86_64-sdl3`,
`mingw-w64-x86_64-wxwidgets3.2-msw` for the launcher). The release is made by one script:

```bash
python tools/make_release.py     # clean Release build with the launcher, ctest, the zip in release/
```

or by hand:

```bash
cmake -S f117port -B f117port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DF117_LAUNCHER=ON
cmake --build f117port/build && ctest --test-dir f117port/build
f117port/build/f117 --game-dir Game
```

`-DF117_LAUNCHER=OFF` (the default) skips the launcher. The tests that need the game files skip themselves when
`Game` is not there. Details: [f117port/PORTING.md](f117port/PORTING.md) and [f117port/launcher/README.md](f117port/launcher/README.md).

## For developers: how the port was made

The port is a reverse-engineering project, not a translation of source code (MicroProse never published any).
The original DOS programs are disassembled and traced; their behaviour is written down as specifications, and the
C code is checked against the original under emulation, function by function.

```text
Game\        junction to the installed DOS game (not in git)
_tools\      junction to Ghidra + JDK (shared with the other ports, not in git)
tools\       reverse-engineering scripts (Python, Ghidra) and tools\make_release.py
port\        RE guide, function indexes, symbols, specs (port\spec), formats (port\formats)
f117port\    the port: src\ (C, SDL3), launcher\ (wxWidgets), tests (ctest), PORTING.md
licenses\    third-party licence texts for the DLLs in the zip
docs\        manual PDFs/text (not in git)
work\        generated output and DOSBox reference captures (not in git)
release\     the zip and its staged folder (not in git)
```

* `PLAN.md`: the state of the port, the subtasks and the open checks by the user. The rules for the agents that
  worked on it are in `CLAUDE.md`; `FORMATS.md` describes the file formats.
* `port/spec/`: the specifications the port follows.
* Game data is never committed or zipped: `Game\` is ignored by git, and `make_release.py` refuses to stage any
  original game file.

## Licence

The code in this repository is MIT (see [LICENSE](LICENSE)). The third-party libraries in the zip (SDL3, Nuked-OPL3,
wxWidgets and the DLLs they need) have their own licences; the texts are in `licenses\`, and the list is in
[licenses/THIRD-PARTY.md](licenses/THIRD-PARTY.md). F-117A Nighthawk Stealth Fighter 2.0 is © MicroProse; this
project is not affiliated with it and contains none of its files.
