# F-117A 2.0 port â€” agent rules (keep this file short: it is loaded every session)

Faithful C/SDL3 port of MicroProse *F-117A Nighthawk Stealth Fighter 2.0* (1991), same method as
`../AcesOfThePacific` (RE â†’ formats â†’ specs â†’ port). State and subtasks: `PLAN.md`.

Token budget rules:
- Work on ONE subtask per session (PLAN.md id, e.g. `S1.2`). Read only `PLAN.md` "Status" + that
  subtask, not the whole file. Update its status line, run `python tools/plan_site.py`, commit.
- Never read large files whole. `Grep`/`rg` first, then `Read` with offset/limit. Specs, decomps and
  disassembly are always searched, never dumped.
- Tools write to `work/` (or `port/`); print only a short summary (counts, first lines, errors).
- Do not view images or screenshots unless asked. Visual checks are the user's job (PLAN.md "U" tasks):
  write the PNG / contact sheet, then ask the user what they see.
- Reuse before writing: `../AcesOfThePacific/aotpport/` (host, mem, VGA, loader), `tools/` here.
  Copy files with the shell; do not read them into context unless they must change.
- Wide searches â†’ `Explore` agent on Haiku. Do not re-derive facts already in `port/RE_GUIDE.md`,
  `FORMATS.md` or `port/spec/`.
- Ground truth when a spec is unclear: `python tools/x86dis.py Game/<EXE> dis SSSS:OOOO LEN`.
- Game data is never committed (`Game/` is a junction to the ignored `F-117ANi/`).
- No test HTML pages; implement, then tell the user what to test.
