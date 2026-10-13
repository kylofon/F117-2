# S3.6 working notes (kept for S3.7 / S3.8)

Copied from `work/s36/` (gitignored) because later subtasks build on them. Everything else of the S3.6
run is merged into `port/spec/simflow.md` / `simflow_symbols.csv`.

* `spec_R11a.md`, `spec_R11b.md`, `syms_R11a.csv`, `syms_R11b.csv`: the per-function render3d passes
  (terrain/scenery 04DA family, 1001 edge tables, 11EB/12EF/1359/1433). Input for **S3.7**; only their
  interface is in `simflow.md` Part 12.
* `r2_*.py`: the Python reference of `1e9e` (per-frame input + player flight model), differential-tested
  against the real code under Unicorn. Input for **S3.8** (`simflow.md` Part 2).
  `python port/notes/s36/r2_test_fm.py 600` (needs `pip install unicorn` and `work/unpacked/VGAME.EXE`,
  read at run time; no game data is stored here). `r2_keychain.py` also reads `work/s36/all.txt`.
* `BRIEF.md`: the brief the twelve readers worked from (scopes, conventions).
