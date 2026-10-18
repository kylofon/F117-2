# Changelog

## 0.1.0 (2026-10-18)

The first release: a port of the whole game for Windows x64. You need your own copy of the original
game files (see README.md, How to play).

### The game

- The whole original chain runs as it did: SETUP, the MicroProse logo, the intro, the shell (pilot roster,
  options, orders, the briefing map, arming and launch), the missions in all theatres, and the debrief
  (landing, eject or rescue, crash, medals, promotion, the roster).
- AdLib sound: the logo, the intro and shell music, the flight sounds and the debrief music, through the
  Nuked-OPL3 emulator.
- The error log `f117.log` beside the program records each error the game reports, with its date.
- Fixed: Esc no longer quits the whole game when a program ends (a crash into the sea returned to the desktop).

### The launcher (`F-117A Nighthawk.exe`)

- Pick the game folder, the program, the aspect (4:3 or 1:1), the window size and full screen, and whether
  sound is on. Play starts the game; the launcher stays open. All of it is remembered in `settings.ini`.
- **Game speed** (default 7, about 10 frames a second): the original paces itself to about 17 frames a second
  on today's PCs, which spins the radar dishes far too fast. 0 gives back the original pacing.
- **Key Bindings...**: all 56 actions on four tabs, with Reset to defaults. Changed keys are saved and
  passed to the game with `--keys`.

### Command line

- `--frame-ticks N`: the game speed for `f117.exe` itself (the launcher passes its value).
- `--keys NAME=CODE,...`: the key bindings for `f117.exe`.
- `--version` prints the version. `--check` checks the game folder.

### How it was verified

- Every module was compared with the original code under emulation (Unicorn), byte for byte: the
  program loader, the video driver, the sim flow, the flight and weapons code, the 3D renderer, the
  shell, the front end, the debrief and the sound driver.
- 23 automated tests (`ctest`) run the chain of programs and the checks against the original. The tests
  that need the game files skip themselves when they are not there.

### Known issues

- Low over the sea near the left edge of the screen, a possible land strip can show. It is not investigated yet.
- Speech is silent. The Roland, PC speaker and IBM/Tandy sound choices play as AdLib; the Sound Blaster and
  Pro Audio Spectrum digital sound states are not ported.
- The mouse and the joystick are untested in the shell and the debrief.
- The radar dish spin depends on the game speed (0 = the original pacing).
- The ID quiz is not present: it is dead code in this version of the original.

### Open checks (not yet compared side by side with the original)

These parts are not yet compared side by side with the original in DOSBox-X. Please report any difference
you see:

- the cockpit and HUD (tapes, frame, reticle, pitch ladder, colours) in the front view;
- the 3D view: terrain, scenery, distance haze, sky and draw order, and the external and TV views;
- the sound: the music tempo and pitch, the engine, weapons, warnings and explosions;
- the debrief: the landing, eject and crash scenes, the summary and the roster;
- the game speed: how the radar dish and the aircraft feel at 7 compared with 0 and with 5 or 9;
- the launcher's key bindings: the dialog, Reset to defaults, and the keys in the game.
