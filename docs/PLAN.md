# Plan

> **2026-11-08: consolidated into this repo.** The engine fork (formerly `keen456-engine`) and the DOS oracle (formerly `keen456-decomp`, now `oracle/`) are one repository, `keen456-omnispeak-fork`. This plan is **historical** where it describes the old two-repo layout (parent repo + `engine/` submodule); the milestones and decisions still stand. Tracker: <https://gitlab.dnsif.ca/github/keen456-omnispeak-fork>.

*Written 2026-11-01, at the pivot. The tracker-generated issue list is [`ROADMAP.md`](ROADMAP.md).*

## Goal

Commander Keen 4 · 5 · 6 that **looks smooth on modern displays** (60 / 75 / 120 / 144 Hz, VRR)
and has the engine's own motion and animation jitter fixed. All of that sits behind toggles, with a
`vanilla` mode that plays **identically** to the DOS original, tic for tic.

## Where we are

| | State |
|---|---|
| Byte-identical DOS rebuild, Keen 4 v1.4 + Keen 5 v1.4 | ✅ 2026-09-19: `make verify-k4 verify-k5` (Borland C++ 3.0 + BC++ 2.0 LIB, headless DOSBox, ~14 s each) |
| Keen 6 | ⏸ Our copy is v1.0; the reconstruction and Omnispeak both need v1.4/v1.5. The user will supply it later (#90). |
| Interpolation spike (#89) | ✅ Prototype on Omnispeak, ~420 lines, sim untouched. Main jitter source = the 2-px EGA scroll grid (vanilla scrolls 2,4,2,4 px per frame). Scroll-step stddev at 60 Hz: 1.67 vanilla → 0.44 whole-pixel → 0.11 quarter-pixel. The user judged whole-pixel indistinguishable from quarter-pixel and both clearly better than vanilla. |
| keen456 DOSBox-X pacing | ✅ native-v1.1.1, user-confirmed. That path is finished; this project is what goes beyond it. |
| Everything else | Not started. **We are still planning, and no implementation work has begun.** |

## The pivot (2026-11-01)

**Before:** we would rebuild DOS byte-identical, write a ~59k-line engine knowledge base, then port
the engine ourselves to C + SDL, file-for-file with `dos/`, and only then do high refresh and jitter
fixes. That was about 60 issues of work before anything visible changed.

**After:** we **fork Omnispeak** and do the high-refresh and jitter work there. The byte-identical
DOS build becomes the **oracle** that proves the fork's vanilla mode is exact.

Why:

- The spike delivered the visible goal on Omnispeak in ~420 lines. The planned own port would rebuild
  what Omnispeak already is: a GPL-2 reimplementation of Keen 4–6 aiming for bug-for-bug fidelity,
  save-compatible with DOS, and actively maintained (upstream commits 2026-10-26/27).
- Upstream had even done one of our issues already: the Keen 6 copy-protection override (our old #67).
- M1 is not wasted. Omnispeak has never been checked tic-by-tic against a rebuilt DOS binary.
  KEENDUMP traces from `dos/` let us do that, and every divergence we find is a fidelity fix we can
  offer upstream.
- Keen 6's data problem is the same either way (Omnispeak also needs v1.4/v1.5), so the pivot costs
  nothing there.

What we gave up: a codebase that maps 1:1 to `dos/` file-for-file. Omnispeak is structured
differently (for example, actor states load from action files), so mapping between the two is
explicit work (#18). That is an acceptable price.

41 issues were closed as `status::wont-do`, each with a comment giving the reason. The ROADMAP
lists them. They can be reopened if we ever return to a from-scratch port.

## Architecture

```
                 ┌───────────────────────────── keen456-decomp (this repo) ─────────────────────────────┐
 dos/  ──BC++──▶ │ byte-identical EXEs ──KEENDUMP──▶ golden per-tic traces (M2)                         │
                 │                                              │                                       │
                 │                                    tracediff (#59)  ◀── fork trace writer (#93)      │
                 │                                              │                                       │
 engine/ (fork of sulix/omnispeak, #91) ── vanilla mode ────────┘  ← must match, every demo (M3)        │
                 │        └─ enhanced: interpolation (M4) + jitter fixes (M5), per-fix toggles (#83)    │
                 └───────────────────────────────────────────────────────────────────────────────────┘
                                         │ ships through
                                         ▼
                           awkto/keen456 launchers: Linux, WASM, Android (M6)
```

- **`dos/`** stays pristine and byte-identical. KEENDUMP hooks live behind an `#ifdef` that is off by default.
- **The engine fork** follows upstream. Our changes are small and toggle-guarded so rebases stay cheap (#94).
- **Vanilla is sacred.** Interpolation must be presentation-only: the trace stays identical with it on (#60 checks this).

## Milestones

| | Scope | Key issues |
|---|---|---|
| **M1** DOS baseline (K4 + 5) | ✅ core done; polish left | #2 #5 #6 #9 |
| **M2** DOS oracle | KEENDUMP + golden traces for every K4/K5 demo | #18 #56 #57 #58 #59 #27 #28 |
| **M3** Engine fork, vanilla-verified | fork exists, spike rebased, fork writes traces, K4 + K5 trace-identical | #91 #92 #93 #83 #60 #62 #29 |
| **M4** High refresh | productise interpolation; 60/75/120/144 Hz/VRR bench | #74–#79 #14 #19 #30 #82 |
| **M5** Jitter fixes | catalogue + fix behind toggles | #50 #84–#88 #20 #51 |
| **M6** Ship | keen456 Linux / WASM / Android, GPL, upstream sync | #69–#72 #94 |
| **M7** Keen 6 | waits on data | #90 #55 #64 |

**Order of work:** #91 done (see Decisions). Then #92 and the M2 oracle chain
(#18 → #56/#57 → #58) can run in parallel. They meet at #93 → #60/#62.

## Decisions

| Date | Decision | Ref |
|---|---|---|
| 2026-09-19 | Build on K1n9_Duk3's reconstruction rather than decompiling from scratch; repo is GPL-2-or-later | #7 |
| 2026-09-19 | High refresh = interpolate over the untouched 35 Hz sim; whole-pixel default | #89, #73 |
| 2026-11-01 | **Fork Omnispeak; drop the own native port.** `dos/` becomes the oracle | this doc |
| 2026-11-01 | Keen 4 + 5 first; Keen 6 deferred to M7 until v1.4/v1.5 data arrives | #90 |
| 2026-11-01 | 70 Hz simulation rejected | #80 |
| 2026-11-01 | Fork lives in **private `github.com/awkto/keen456-engine`**, submodule at `engine/`. Branches: `main` = our work rebased on upstream; `upstream-master` = untouched mirror of `sulix/omnispeak` master; `spike/interp-89` = the original spike, kept for reference. Patches are small, toggle-guarded and in upstream style so they can be offered back (#94). Can go public later. | #91 |

## Prior art (surveyed 2026-11-01)

| Project | What | Use to us |
|---|---|---|
| [sulix/omnispeak](https://github.com/sulix/omnispeak) | GPL-2 reimplementation of Keen 4–6, active | **The base of our fork** |
| K1n9_Duk3 reconstruction ([mirror](https://github.com/sparky4/keen4-6)) | Byte-identical DOS source | Already in `dos/` (newer shikadi.net copy) |
| [juj/KEEN70HZ](https://github.com/juj/KEEN70HZ) | Keen 4 shareware modded to 70 fps | **No licence: read it for ideas, do not copy code.** It is the evidence that a 70 Hz sim breaks gameplay. |
| [grinoir/keen-worlds](https://github.com/grinoir/keen-worlds) | GPL-2 Omnispeak-based mashup, created 2026-10 | Worth a look when we work on E8; probably nothing to cherry-pick |
| Omnispeak platform forks ([Xbox](https://github.com/Ryzee119/omnispeak), [3DS](https://github.com/RetroGamer02/omnispeak-3ds), [Archipelago](https://github.com/kodbyte/omnispeak-ap)) | Platform ports | No high-refresh or interpolation work |
| [Commander Genius](https://github.com/gerstrong/Commander-Genius) | Separate, non-faithful Keen 1–6 engine | Not usable for vanilla-exact work |
| [keen-nes](https://github.com/ronaldstoner/keen-nes), [CKGBA](https://github.com/john314thegeekygenius/CKGBA) | Clean-room console ports | Not relevant |

We found no public interpolation or high-refresh work for the Galaxy engine, and no official remaster
of Keen 4–6.

## Data

`KEEN_DATA=~/keen-data-decomp`: Keen 4 = v1.4 shareware (the full episode), Keen 5 = v1.4 Apogee.
Keen 6 is empty until #90. **Never modify `~/keen-data`**, because it is mounted into the live keen456
containers. Its Keen 4 is a different build that Omnispeak rejects.
