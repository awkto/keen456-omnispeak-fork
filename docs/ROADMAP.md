# Roadmap

> **2026-11-08: consolidated into this repo.** Historical snapshot: it describes the old two-repo layout (parent `keen456-decomp` + `engine/` submodule). Issue numbers are unchanged; the tracker was renamed to `github/keen456-omnispeak-fork`. Paths like `harness/` and `tools/` now live under `oracle/`.

Generated from the tracker on 2026-11-01: <https://gitlab.dnsif.ca/github/keen456-omnispeak-fork> (epics live in the `github` group).
The *why* behind this structure is in [`PLAN.md`](PLAN.md). Tier: **O** = `tier::opus` (judgement), **D** = `tier::deepseek` (mechanical, objectively checkable), **H** = needs a human.

## Status 2026-11-07
- **Decomp/oracle:** Keen 4, 5 and 6 (v1.4) rebuild byte-identical (#55 closed). The engine fork is trace-identical to DOS on all 15 built-in demos, interpolation off and on (K6: #64 closed).
- **Smooth rendering:** interpolation with snap rules (#76), pinned scorebox + status window (#77), aspect fix (#96), in-game Smoothing menu (#97) — all merged.
- **Distribution:** Windows/Linux tester packages (`tools/package-{windows,linux}.sh`, optional Keen 5/6 with `KEEN_EXTRA`); WASM build live at https://keen456x.pro.dnsif.ca (#70, `tools/deploy-keen456x.sh`); GPL source shipped with each build (#72, partial).
- **Open for the user:** play-test feedback (#79, #70), camera smoothing (#86), step-state actors (#88).

**Status: planning only.** M1 is done for Keen 4 + 5; nothing after it has started.

## Milestones

- **M1 — DOS baseline (Keen 4 + 5)** — Keen 4 v1.4 and Keen 5 v1.4 rebuild byte-identical from `dos/` (`make verify-k4 verify-k5`) — achieved 2026-09-19. Open items are tooling polish. Keen 6 moved to M7.
- **M2 — DOS oracle: reference traces (Keen 4 + 5)** — An instrumented DOS build (KEENDUMP) writes per-tic state traces for every built-in Keen 4 and Keen 5 demo; `tracediff` names the first divergence. This is the ground truth the engine fork is checked against.
- **M3 — Engine fork, vanilla-verified (Keen 4 + 5)** — Our Omnispeak fork exists, carries the #89 interpolation prototype behind a toggle, and its vanilla mode is trace-identical to DOS on every built-in Keen 4 and Keen 5 demo.
- **M4 — High refresh** — Smooth presentation at 60 / 75 / 120 / 144 Hz and VRR by interpolating over the untouched 35 Hz simulation. Vanilla stays trace-identical.
- **M5 — Jitter fixes** — Catalogued engine jitter fixed behind per-fix toggles in the `enhanced` profile; `vanilla` still trace-identical.
- **M6 — Ship through keen456** — The engine fork ships to Linux, WASM and Android through the keen456 launchers, GPL-compliant, with an upstream-sync routine.
- **M7 — Keen 6 (waits on v1.4/v1.5 data)** — Byte-identical Keen 6 rebuild, Keen 6 reference traces and fork verification. Blocked on the user supplying a v1.4 or v1.5 EGA copy (#90).

## Critical path

M1 ✅ (K4+K5) → **#91** decide where the fork lives → **#92** rebase the interpolation spike onto upstream Omnispeak
→ KEENDUMP golden traces for K4+K5 (**#18 → #57 → #58**) + fork trace writer (**#93**) → **#60 / #62** fork's vanilla mode trace-identical to DOS (**M3**)
→ compat framework (**#83**) → interpolated presentation (**#74–#79**, **M4**) → jitter fixes (**#84–#88**, **M5**) → ship via keen456 (**#69–#71**, **M6**).

#91 and #92 do not need the oracle, so the fork can be stood up in parallel with M2. Keen 6 (M7) joins whenever its data arrives (#90).

## E1 — Foundations — repo, data policy, toolchain

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/24>

Repo, data policy, LZEXE handling and CI. The toolchain (Borland C++ 3.0 + 2.0 LIB, headless DOSBox build) is done; remaining items are polish.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #2 | D | M1 | Game-data policy + `KEEN_DATA` resolution shared by every tool | — |
| #5 | D | M1 | LZEXE 0.91: native unpacker + scripted repack | — |
| #6 | D | M1 | CI: GitHub Actions with SKIP-when-no-data semantics | #2 |

## E2 — Byte-identical DOS baseline (the oracle)

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/25>

K1n9_Duk3's reconstruction rebuilt byte-identical from `dos/`. **Done for Keen 4 + 5 (2026-09-19).** Keen 6 waits on a v1.4/v1.5 copy (#90 → #55, M7). This build is the oracle the engine fork is verified against.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #9 | D | M1 | Static data ripper: replace CKPatch + MAKEOBJ with native tools | #5 |
| #55 | O | M7 | Byte-identical rebuild: Keen 6 EGA | #90 |
| #90 | H | M7 | Obtain Keen 6 v1.4 or v1.5 EGA (FormGen) — our copy is v1.0 and cannot be reproduced (human) | — |

## E3 — Engine notes (trimmed)

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/26>

Only the engine documentation that the DOS oracle and the high-refresh/jitter work depend on: refresh manager (#14), actor/state system and DOS↔Omnispeak state mapping (#18), timing model incl. the ID_SD timer (#19), Keen physics (#20). The rest was closed in the 2026-11-01 pivot: Omnispeak's readable C already covers it.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #18 | O | M2 | docs/ACTORS.md — the state machine / actor system | — |
| #14 | O | M4 | docs/ID_RF.md — refresh manager (scrolling, update blocks, sprites, animated tiles) | — |
| #19 | O | M4 | docs/TIMING.md — the complete timing model, with measurements | #14 |
| #20 | O | M5 | docs/KEEN_PHYSICS.md — player movement and its tic-dependence | #18 |

## E4 — Verification harness — demos, traces, frames

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/27>

Objective pass/fail: KEENDUMP (instrumented DOS build) writes per-tic state traces of every built-in demo; `tracediff` names the first divergence; frame compare, cadence bench and motion-trace tool measure presentation. All of it now targets the Omnispeak fork instead of our own port.

**Exit (M2):** golden traces for every Keen 4 + 5 demo.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #27 | D | M2 | DOS ground-truth runner: boot any episode headless, drive it, capture | #2 |
| #28 | D | M2 | Demo format + `oracle/tools/demo.py` (dump, verify, synthesise) | #2 |
| #56 | D | M2 | Symbol maps: TLINK `.MAP` → DOSBox-X debugger + Ghidra labels | — |
| #57 | O | M2 | KEENDUMP: instrumented DOS build that writes a per-tic state trace | #18 #56 |
| #58 | D | M2 | Golden traces for every built-in demo, all episodes | #27 #28 #57 |
| #59 | D | M2 | `oracle/tools/tracediff.py` — first divergence between two traces, human-readable | #57 |
| #29 | D | M3 | Frame compare: port framebuffer vs DOSBox capture at the same demo frame | #27 #92 |
| #30 | D | M4 | Cadence bench: frame-time histograms for DOS build and port | #27 #92 |
| #82 | D | M4 | Motion-trace tool: per-frame screen-space positions → CSV + plots, flag uneven steps | #92 |

## E6 — Engine fork — Omnispeak, vanilla-verified against DOS

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/29>

Fork Omnispeak (#91), land the #89 interpolation prototype behind a toggle (#92), make the fork write the same per-tic trace as the DOS oracle (#93), build the vanilla/enhanced compat framework (#83), and prove the fork's vanilla mode trace-identical to DOS on every built-in demo: Keen 4 (#60), Keen 5 (#62), Keen 6 later (#64).

**Exit:** M3 (Keen 4 + 5), M7 (Keen 6).

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #60 | O | M3 | Keen 4: engine fork's vanilla mode is trace-identical to DOS on every built-in demo | #58 #59 #93 |
| #62 | O | M3 | Keen 5: engine fork's vanilla mode is trace-identical to DOS on every built-in demo | #58 #59 #93 |
| #83 | O | M3 | Compat framework: `vanilla` / `enhanced` profiles, one toggle per fix, CI guard | #92 |
| #91 | H | M3 | DECISION: where the engine fork lives — repo, visibility, layout, upstream policy | — |
| #92 | O | M3 | Rebase the #89 interpolation spike onto current upstream Omnispeak; land it in the fork, off by default | #91 |
| #93 | O | M3 | Trace writer in the engine fork: emit the KEENDUMP trace format during demo playback | #18 #57 #91 |
| #64 | O | M7 | Keen 6: golden traces + engine fork trace-identical on every built-in demo | #55 #58 #93 |

## E7 — Distribution — Linux, WASM, Android

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/30>

Ship the engine fork through the keen456 launchers (Linux, WASM, Android) with GPL compliance, and keep it in sync with upstream Omnispeak (#94).

**Exit:** M6.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #69 | D | M6 | Linux: ship the native engine through the keen456 launcher (.deb / AppImage) | #60 #62 #79 |
| #70 | O | M6 | WASM: Emscripten build, offered in the keen456 web app (replaces ROADMAP Path 2) | #69 |
| #71 | D | M6 | Android: native engine inside `android-native` | #69 |
| #72 | D | M6 | GPL-2 compliance for every shipped build | #91 |
| #94 | O | M6 | Upstream relationship: periodic rebase on sulix/omnispeak and offering fixes upstream | #92 |

## E8 — High-refresh rendering — 60 / 75 / any Hz

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/31>

Smooth presentation at 60 / 75 / 120 / 144 Hz and VRR: interpolate over the untouched 35 Hz simulation (decided by the #89 spike, whole-pixel default). Productise the spike in the engine fork.

**Exit:** M4.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #74 | O | M4 | Decouple presentation from simulation: fixed-step sim, display-rate render loop | #83 #92 |
| #75 | O | M4 | Full-frame tile renderer with arbitrary sub-pixel-independent camera origin | #74 |
| #76 | O | M4 | Sprite interpolation with snap rules | #75 |
| #77 | O | M4 | Camera interpolation; HUD/scorebox pinned in screen space | #76 |
| #78 | D | M4 | Tile animation, palette fades and screen effects at display rate | #75 |
| #79 | D | M4 | High-refresh acceptance bench: 60 / 75 / 120 / 144 Hz | #30 #76 #77 #78 |

## E9 — Jitter & animation fixes

Epic: <https://gitlab.dnsif.ca/groups/github/-/epics/32>

Catalogue and fix the engine's own uneven motion and animation in the engine fork, each fix behind a toggle in the `enhanced` profile; `vanilla` stays trace-identical.

**Exit:** M5.

| # | tier | MS | issue | blocked by |
|---|---|---|---|---|
| #50 | O | M5 | RESEARCH: jitter catalogue — every source of uneven motion, with repro and capture | #19 #82 |
| #51 | D | M5 | A/B review page: vanilla vs. enhanced captures for human sign-off | #82 |
| #84 | O | M5 | Fix: `lasttimecount`/`TimeCount` swapped at CK_PLAY.C:777/857/874/895 (tic burst after pause, menu, level start) | #50 #82 #83 |
| #85 | O | M5 | Fix: Keen shakes relative to the platform he is riding | #50 #82 #83 |
| #86 | O | M5 | Fix: camera follow stepping / 2-px scroll granularity | #50 #82 #83 |
| #87 | O | M5 | Fix: uneven pixel stepping from sub-pixel velocities | #50 #82 #83 |
| #88 | O | M5 | Fix: uneven animation frame durations when `tics` varies | #50 #82 #83 |

## Closed in the 2026-11-01 pivot

Each carries a closing comment with the reason. Reopen any of them if we ever go back to a from-scratch port.

- #10 docs/ARCHITECTURE.md — boot to play loop, manager layering
- #11 docs/ID_MM.md — memory manager
- #12 docs/ID_CA.md — cache manager + EGAGRAPH / GAMEMAPS / AUDIO formats
- #13 docs/ID_VW.md — EGA video layer
- #15 docs/ID_IN.md — input manager + demo record/playback
- #16 docs/ID_SD.md — sound manager + the timer that defines game time
- #17 docs/ID_US_1/2.md — user manager: menus, config, text windows, RNG
- #21 docs/ACTORS_K4.md — Keen 4 actor catalogue
- #22 docs/ACTORS_K5.md — Keen 5 actor catalogue
- #23 docs/ACTORS_K6.md — Keen 6 actor catalogue
- #24 docs/FORMATS_SAVE_CONFIG.md — SAVEGAM?.CK? and CONFIG.CK?
- #25 docs/EPISODE_DIFFS.md — every `#ifdef KEEN4/5/6` in shared code
- #26 docs/PORT_HAZARDS.md — inventory of 16-bit and DOS assumptions
- #31 DECISION: port architecture — conventions, types, SDL version, relation to Omnispeak
- #32 port/ skeleton: compat.h, build system, null backends — game code compiles
- #33 Port ID_MM: handle-based allocator over malloc
- #34 Port ID_CA: file I/O + Huffman / Carmack / RLEW in portable C
- #35 Port ID_VW + ID_VW_A/AE.ASM: software EGA
- #36 Port ID_RF + ID_RF_A.ASM: refresh manager, bug-for-bug
- #37 SDL video backend: window, integer scale, 4:3 aspect, vsync present
- #38 Port ID_IN: SDL keyboard/gamepad → DOS scancodes; demo playback intact
- #39 Port ID_SD timing core: a 70 Hz `TimeCount` that never drifts
- #40 Port ID_SD audio: OPL2 emulation, IMF music, AdLib sfx
- #41 Port ID_SD audio: PC speaker sfx synthesis
- #42 Port ID_US_1/2 + ID_US_A.ASM: menus, config, text, exact `US_RndT`
- #43 Filesystem layer: data dir discovery, case-insensitive open, user dir
- #44 Keen 4 boots natively to the title screen and main menu
- #45 Port CK_DEMO.C: intro, terminator text, star-wars scroller (111 asm lines)
- #46 Port CK_TEXT.C: help and story page viewer
- #47 Port CK_PLAY.C + CK_STATE.C with exact 16-bit semantics
- #48 Port CK_KEEN.C + CK_KEEN2.C (player, world map, scorebox `MemDrawChar` asm)
- #49 Port Keen 4 actors: K4_ACT1-3.C + K4_SPEC.C
- #61 Port Keen 5 actors: K5_ACT1-3.C + K5_SPEC.C
- #63 Port Keen 6 actors: K6_ACT1-3.C + K6_SPEC.C
- #65 Save/load compatibility with DOS saves, both directions
- #66 DECISION: three binaries vs. one binary with runtime episode select
- #67 Keen 6: `--nocopy` flag to skip the Creature Question
- #68 Warnings-clean, ASan/UBSan across all demos, and a fuzzed loader
- #73 DECISION: high-refresh strategy — interpolate over a 35 Hz sim vs. run the sim faster
- #80 Experimental 70 Hz simulation: audit and fix per-frame logic
- #81 RESEARCH: back-port vblank-locked pacing to the DOS build for the DOSBox-X / js-dos wrappers

The E5 epic (*Native port — platform layer*) was closed with them.
