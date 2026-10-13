# S3.9 working notes (oracle for Phase 5)

Executable references for `port/spec/debrief.md` (END.EXE). No game data and no disassembly is stored here; the harness reads
`work/unpacked/END.EXE` at run time (needs `pip install unicorn capstone`).

* `s39_ref.py`: Python transcription of the END arithmetic: `3021 score`, `2AFA promotion`, `2476 awards`, `0EF3 fmt_time`, `00A1 landing_class`,
  `05CA place_icons` (state = DGROUP as a 64 KB bytearray + the comm block).
* `s39_ref_ui.py`: the screens as lists of UI operations (`blit`, `text`, `prompt`, `screen`, ...) with every text built the way the original does
  (`2BEC`, `0A98`, `0DA3`, `0714`, `2265` and its parts, the ending scenes, `39D0`, `2092`, `04D2`, `046E`, `3BF4` name).
* `s39_emu.py`: Unicorn harness (END image, DGROUP at 05FA, comm block at 3000:0, driver call stubs, near stubs that log their arguments).
* `s39_test.py [N] [time landing score promo awards icons]`: real code vs reference, byte for byte over DGROUP and the comm block.
* `s39_test_ui.py [N] [waitin marks hover init event status replay summary awards quips scenes cbs main mapend]`: same for the screens (drawing, file, wait and sound calls are
  stubbed and compared as an ordered operation list).
* `cmpfn.py END_OFFSET START_OFFSET`: instruction diff of an END toolkit function against its START twin.

Result in S3.9: 6 x 1000 numeric cases + 14 x 300 screen cases, 0 mismatches (about 30 s).

Phase 5: port each function from `debrief.md`, then port these scenarios as unit tests (same random worlds, the Python reference as expected result) or
run the C code against the reference through a small ctypes shim.
