# Rules for agents (and humans) working in this repo

This is one repository: a fork of Omnispeak (`src/`, `data/`, `web/`, `tools/`) plus the DOS oracle it is
verified against (`oracle/`). Until 2026-11-08 these were two repos (`keen456-engine` as a submodule of
`keen456-decomp`); the old history of the oracle part is in the archived `github.com/awkto/keen456-decomp`.
Pick up one tracker issue at a time: <https://gitlab.dnsif.ca/github/keen456-omnispeak-fork>.
Read [`docs/PLAN.md`](docs/PLAN.md) first (historical for the two-repo layout), then
[`README.md`](README.md) and [`oracle/README.md`](oracle/README.md).
Keen 4 + 5 first; Keen 6 traces exist but CI has no Keen 5/6 data.
Every issue is self-contained (Context / Task / Acceptance / Depends on). Labels say who it
suits: `tier::opus` needs judgement, `tier::deepseek` is mechanical with an objective check,
`tier::human` needs altanc. Do not start an issue whose *blocked by* issues are open.

## Layout

- `src/`, `data/`, `web/`, `CMakeLists.txt`: the engine (Omnispeak fork). `tools/build-wasm.sh`, `tools/vanilla-guard.sh`, `tools/framecmp.py`.
- `tools/`: packaging and deploy (`package-*.sh`, `deploy-keen456x.sh`), `droplet.sh`, `fetch-k4-shareware.sh`, `check-no-data.sh`, `motiontrace.py`, `tileanim.py`.
- `oracle/`: `dos/` (reconstructed DOS source), `harness/`, `tools/` (verify-dos, keendump, tracecmp, tracediff, ...), `docs/`. Run its Makefile with `make -C oracle`.
- `docs/`: engine knowledge base (INTERP, ACTORS, ID_RF, TIMING, JITTER, DATA, PLAN, ROADMAP).
- `tests/dos-golden/`: DOS golden traces for Keen 4/5/6 (`.trc.zst`).

## Hard rules

1. **Never commit game data, original executables, ripped static data, or Borland files.**
   `.gitignore` blocks them; do not work around it. `tools/check-no-data.sh` must pass (the only
   allowed binaries are `tests/dos-golden/*.trc.zst` and the Omnispeak tables in `data/keen*/`).
   Data is read from `$KEEN_DATA` (use `~/keen-data-decomp`; never modify `~/keen-data`);
   Borland C++ from `~/bcc`. Tools print `SKIP` and exit 0 when data is absent.
2. **`oracle/dos/` must keep building byte-identical.** Any change to it is wrapped in an
   `#ifdef` that is off by default, and `make -C oracle verify-k4` (etc.) must still pass.
3. **Vanilla is sacred.** The default/`vanilla` profile is bug-for-bug with DOS, proven
   by golden demo traces. Interpolation is presentation-only and must leave the trace unchanged. Fixes and enhancements are separate toggles. If you find an engine
   bug, document it; do not "fix" it in the vanilla path.
4. **Keep the fork rebasable on upstream Omnispeak** (remote `upstream` = sulix/omnispeak). Small, toggle-guarded patches in
   upstream's style; no drive-by refactors. Fidelity fixes found via the oracle are upstream candidates (#94).
5. **Prove it.** "Looks right" is not acceptance. Use `oracle/tools/tracediff.py`, `tools/framecmp.py`, hashes,
   motion-trace plots. Put the evidence in the issue when closing it.
6. **16-bit semantics are deliberate.** `int` was 16 bits. When a trace divergence comes from
   width or wrap-around, fix it as defined behaviour and record it in `docs/ENGINE_DIVERGENCES.md`.
7. GPL-2.0-or-later headers on new source files. Omnispeak code is GPL-2 (attribute it).
   **KEEN70HZ has no licence: read it, never copy from it.**

## Practicalities

- The dev host (pro.dnsif.ca) is headless: DOSBox runs under Xvfb or the dummy SDL driver,
  always with output redirected to a file and a watchdog timeout. Heavy oracle work goes on a droplet (`tools/droplet.sh`).
- Driving Keen's menus with synthetic keys is flaky; prefer `/TEDLEVEL n`, `/NOWAIT`, `DEMO`
  and file-based assertions.
- Timing facts already measured (do not re-investigate): the game loop is PIT-timed at
  70 Hz tics, `MINTICS=2` → 35 fps, not vblank-synced; emulated CPU speed does not affect it.
- When you learn something about the engine, it goes in `docs/`, not only in the issue.
