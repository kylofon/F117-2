# F-117A Nighthawk (the launcher)

`F-117A Nighthawk.exe` starts `f117`, the SDL3 port of F-117A Nighthawk Stealth Fighter 2.0, with the options
chosen in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own
controls, like the Test Drive III launcher (`TestDrive3/td3port/launcher`) it is modelled on.

## The window

* **Game files**
  * **Folder** (`--game-dir`): the folder with the original game's files (default `Game` beside the launcher; in
    the source tree the repository's `Game`). The game writes its pilot roster and preferences there, so it must
    be writable (the (i) icon next to its Browse button says so when you hover over it).
  * **Program**: `f117.exe` (default: beside the launcher).
  * The line below says what is missing (`F117.COM`, `SETUP.EXE`, `MPS_LOGO.EXE`, `PLAYER.EXE`, `START.EXE`,
    `VGAME.EXE`, `END.EXE`, `DSWAP.EXE`, `MGRAPHIC.EXE`, `MISC.EXE`, or the program). **Play** stays greyed out
    until the folder and the program are there.
* **Options**
  * **Aspect** (`--aspect`): 4:3 as on a VGA monitor, or 1:1 square pixels.
  * **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
  * **Sound**: off starts the game with `SDL_AUDIO_DRIVER=dummy`.
  * **Game speed** (`--frame-ticks`, default 7): the retrace ticks (70.09 a second) each frame takes at least in
    flight. The original paces itself to about 17 frames a second on any fast PC (its frame-rate estimator stops at
    15), far above the 4 to 15 it was built for, so radar dishes spin wildly. 7 gives 10 frames a second, the middle of
    that range; the flight model scales with the measured rate, so the aircraft still flies at the same speed.
    0 = the original pacing only.
* **Key Bindings...** opens a dialog with a button per action showing its key, on four tabs (Flying, Weapons and
  countermeasures, Views, Displays and game): click it, then press the new key (Shift, Ctrl or Alt can be held with it;
  a key already used asks before it is taken over; **No key** frees the action). **Reset to defaults**, **OK**,
  **Cancel**. Changed keys are shown in bold. The keys that keep their job (keypad stick, Esc, Alt+Q, Alt+B, Alt+S,
  Alt+Enter) are listed under the tabs as **Static keys**. Play passes only the changed keys, as `--keys name=code,...`
  (`f117port/PORTING.md` "Key bindings").
* **Play** starts the game; the launcher stays open. **About**: version, author and links.

Everything is remembered in `%APPDATA%\F-117A Nighthawk\settings.ini` (`~/.config/f117a-nighthawk` on Linux). A
folder or program left at its default is stored empty, so it follows the launcher if the whole folder moves.
The key bindings are in the `[Keys]` section, one line per action. Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). It builds on its own:

```bash
cmake -S f117port/launcher -B f117port/launcher/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build f117port/launcher/build
```

Then choose the program in the window or copy the launcher next to it. From the main project (`f117port/CMakeLists.txt`),
`-DF117_LAUNCHER=ON` (`add_subdirectory(launcher)`) builds it beside `f117.exe`.

On Windows the build copies every DLL the launcher needs beside it (`copy_dlls.cmake`): the two wxWidgets DLLs
and the MSYS2 libraries they load. Keep them with the `.exe` in a release. The C++ runtime of the launcher
itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window and the About box.
* `keys.h`, `keys.cpp`: the rebindable actions (the same names and defaults as `src/keymap.c`), key names, load and save, the
  `--keys` argument and the "Press new key" prompt. `keysdialog.h`, `keysdialog.cpp`: the Key Bindings dialog.
* `game.h`, `game.cpp`: the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, the F-117A over a night sky drawn in code. `make_icon.py` (Pillow) writes
  the same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy.
* `CMakeLists.txt`: the build, standalone or from the main project.
