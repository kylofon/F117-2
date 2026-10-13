# S3.7 working notes (kept for Phase 5 / S3.8)

Copied from `work/s37/` (gitignored). The specification itself is merged into `port/spec/render3d.md` (Parts 1-6 = readers
R7d, R7f, R7e, R7a, R7b, R7c) and `port/spec/render3d_symbols.csv`; disassembly dumps were not copied (regenerate with
`tools/dsann.py`).

* `R7x_ref.py`: Python references of the renderer leaf routines, written from the disassembly. They are executable specs and
  the oracle for the port's unit tests (render3d.md section 8 lists which routine is in which file).
* `R7x_emu.py` (+ `R7a_emu2.py`): Unicorn harnesses that run the real VGAME code and compare it with the reference.
  `pip install unicorn`; they read `work/unpacked/VGAME.EXE` at run time (no game data is stored here). Example:
  `python port/notes/s37/R7b_emu.py 2000`. `R7x_test*.txt` = the outputs of the full runs (all 0 mismatches).
* `R7c_tables.py`, `R7c_fit2.py`: formula fits of the 1433 tables (segment 14B2); `R7d_dump.py`, `R7e_data.py`: table dumps /
  data checks; `R7a_stats.py`: part counts of the shipped 3DX banks.
* `BRIEF.md`: the brief the six readers worked from.
