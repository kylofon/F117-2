# S3.8 working notes (oracle for Phase 5)

Executable references for `port/spec/flight.md`. No game data and no disassembly is stored here; the harness reads
`work/unpacked/VGAME.EXE` at run time (needs `pip install unicorn`) and the S3.6 helpers in `port/notes/s36/`.

* `s38_ref.py`: Python transcription of `5460 43ee 5588 559a 516d 62d0 65d4 5730 4512 461c 465b 711f 74f6 671a 753f`
  (+ `d707 d7bb 8bab bcac bbed`). State = DGROUP as a 64 KB bytearray, calls to drawing/sound/text code are logged.
* `s38_test.py`: Unicorn differential test, real code vs `s38_ref`, byte for byte over DGROUP + pilot/comm blocks + call log.
  `python port/notes/s38/s38_test.py 1000` (all 16 scenarios, about 15 min) or `... 200 ai proj` (selected).
  Result in S3.8: 16 x 1000 cases, 0 mismatches.
* `cov.py`: which reference lines a set of scenarios executes (`python port/notes/s38/cov.py 300 ai aifocus`).
* `asm_fields.py`: names the unit/site/projectile/decoy/tracer fields in `tools/dsann.py` output.

Phase 5: port each function from `flight.md`, then port these scenarios as unit tests (same random worlds, the Python reference
as the expected result) or run the C code against the reference through a small ctypes shim.
