# F-117A 2.0 Reverse Engineering Guide

## §1 Executable Map (from tools/survey.py)

**Total code size:** 285,672 bytes across 15 files (87 relocations total).

**Compiler:** Borland C++ (7 main EXE files: START, VGAME, END, MPS_LOGO, PLAYER, SETUP, DSWAP).

**Entry points:** Main programs at 000E:xxxx (Borland C++ initialization pattern); drivers at 0000:0000 (loader/interrupt handler pattern).

**Packers:** All files unpacked (original DOS release, no compression).

**Drivers/modules:** 
- Audio: ASOUND/ISOUND/RSOUND/NSOUND.117 (AdLib/Roland/IBM-Tandy/speaker drivers, 10-15 KB each, heavy INT 1Ah/21h use)
- Graphics: MGRAPHIC.EXE (VGA driver, 12 KB, INT 10h/72h for video mode setup)
- Sound data: SPEECH.117 (26 KB, raw binary, no MZ header)
- Miscellaneous: MISC.EXE, DSWAP.EXE (small utilities)

**Chain loader:** F117.COM (1.7 KB, overlay loader, INT 21h for program loading). Loads sequence: SETUP → MPS_LOGO → PLAYER → START → VGAME → END, with MGRAPHIC + sound drivers as TSRs.

**INT usage:** Dominated by INT 21h (DOS services) and INT 10h (BIOS video), with sound/graphics drivers using INT 1Ah (timer) and device-specific interrupts.

**I/O ports:** Graphics driver uses standard VGA ports (03C4h-03DAh); audio drivers use device-specific ports (AdLib: 0388h-0389h; Roland: 0330h-0331h).

Next: S1.2 (overlay chain analysis) will detail loader protocol, memory layout, and exit codes.
