# F-117A Nighthawk (the launcher)

`F-117A Nighthawk.exe` starts `f117`, the SDL3 port of F-117A Nighthawk Stealth Fighter 2.0, with the options
chosen in its window, or the original 1991 game in DOSBox for comparison. It is built with
[wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls, like the launchers of the other
ports (copied from `AcesOfThePacific/aotpport/launcher`).

## The window

* **Game files**
  * **Folder** (`--game-dir`): the folder with the installed game's files (default `Game` beside the launcher; in
    the source tree the repository's `Game`). The game writes its roster and preferences there, so it must be
    writable.
  * **Program**: `f117.exe` (default: beside the launcher). The port does not exist yet (S4.1), so **Play** stays
    greyed out until it is built.
  * **DOSBox**: `DOSBox.exe` (default: beside the launcher or in a `DOSBOX` folder up to four levels above it).
  * The line below says which game file is missing (`F117.COM`, `SETUP.EXE`, `MPS_LOGO.EXE`, `PLAYER.EXE`,
    `START.EXE`, `VGAME.EXE`, `END.EXE`, `DSWAP.EXE`, `MGRAPHIC.EXE`, `MISC.EXE`).
* **Options**: **Aspect** (`--aspect`, 4:3 as on a VGA monitor or 1:1 square pixels), **Window size**
  (`--scale`), **Start in full screen** (`--fullscreen`; Alt+Enter switches) and **Sound** (off starts the game
  with `SDL_AUDIO_DRIVER=dummy`).
* **Play** starts the port; **Play original (DOSBox)** mounts the game folder as `C:` and runs `F117` in DOSBox
  (the checks marked U in `PLAN.md` compare the port against it). The launcher stays open.

Menus: **File** (Preferences: Always on top; Exit) and **About**. Everything is remembered in
`%APPDATA%\F-117A Nighthawk\settings.ini` (`~/.config/f117a-nighthawk` on Linux). A folder, program or DOSBox
left at its default is stored empty. There is no Key Bindings page yet (the port's keys are decided in S5).

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`).

```bash
cmake -S f117port/launcher -B f117port/launcher/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build f117port/launcher/build
```

The build copies the DLLs the launcher needs beside it (`copy_dlls.cmake`); keep them with the `.exe`. Once the
port's own CMake project exists (S4.1), `-DF117_LAUNCHER=ON` builds the launcher next to `f117.exe`
(`add_subdirectory(launcher)`).

## Files

`app.cpp` (application), `launcher.*` (window, menus, Preferences, About), `game.*` (file checks, starting the
port or DOSBox), `settings.*` (`settings.ini`), `icon.*` + `make_icon.py` (the F-117A over a night sky, drawn in
code; `make_icon.py` writes `app.ico`), `app.rc`, `app.manifest`, `version.h`, `copy_dlls.cmake`, `CMakeLists.txt`.
