# F-117A Nighthawk Stealth Fighter 2.0 — SDL3 port

A faithful native port (C, SDL3) of MicroProse's *F-117A Nighthawk Stealth Fighter 2.0* (1991), in the
making. Not an emulator. The original game files are not included: the port reads them from a `Game`
folder at run time.

Status and plan: [PLAN.md](PLAN.md). Agent rules: [CLAUDE.md](CLAUDE.md).

## Layout

```text
Game\        junction to the installed DOS game (not in git)
_tools\      junction to Ghidra + JDK (shared with the other ports, not in git)
tools\       reverse-engineering scripts (Python, Ghidra)
port\        RE guide, function indexes, symbols, specs (port\spec), formats (port\formats)
docs\        manual PDFs/text (not in git)
work\        generated output and DOSBox reference captures (not in git)
f117port\    the port (from phase 4)
```

## Build

MSYS2 mingw64 (gcc, ninja, cmake, `mingw-w64-x86_64-sdl3`; the launcher also `mingw-w64-x86_64-wxwidgets3.2-msw`):

```bash
cmake -S f117port -B f117port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DF117_LAUNCHER=ON
cmake --build f117port/build && ctest --test-dir f117port/build
f117port/build/f117 --game-dir Game
```

`-DF117_LAUNCHER=OFF` (the default) skips the launcher. Details: [f117port/PORTING.md](f117port/PORTING.md).

## License

MIT for the code in this repository (see [LICENSE](LICENSE)). F-117A is © MicroProse.
