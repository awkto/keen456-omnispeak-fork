# Jitter catalogue: every source of uneven motion in Keen 4/5

*Tracker #50. Built from what the project already documents; nothing here was re-measured.
Citations: [TIMING.md](TIMING.md) (§n), [ID_RF.md](ID_RF.md), [ACTORS.md](ACTORS.md),
[HARNESS.md](HARNESS.md) "Motion trace (#82)", [PLAN.md](PLAN.md), the #89 spike notes (memory note
`keen-interp-spike-findings`), and the engine fork's `docs/INTERP.md` and `src/ck_fix.h` on
`keen456-omnispeak-fork` `main`. DOS cites are `oracle/dos/KEEN4-6/<file>:<line>`; Omnispeak cites are the fork's
`src/<file>:<line>`.*

**Marking.** Anything tagged **[inference]** is my reading of the cited code or docs and has not been
run or measured. Everything else is a citation. Keen 6 was not considered (no v1.4 data, #90).

**Ground rules for reading the table.**

- The sim runs at `tics=2`, 35 Hz, in live play (TIMING §3) and at `tics=3`, 23.3 Hz, in demos
  (TIMING §7). Interpolation (`interpolation` fix, INTERP.md) is presentation-only and keeps the
  sim at a fixed 2 tics outside demos.
- Fix toggles that exist today (`src/ck_fix.h`): `interpolation` (sim = false), `clock-continuity`
  (sim = true, #84 F1), `stall-charge-once` (sim = true, #84 F2). There is **no** toggle yet for
  #85-#88.
- "Sim-affecting" means the fix changes game state and therefore demo/trace sync. The fork forces
  such fixes off during demos and `/DUMPFILE` unless `/FORCEFIXES` is given (`ck_fix.h`).

---

## Summary table

| ID | Source | Where you see it | Measured | Status | Sim-affecting fix? |
|---|---|---|---|---|---|
| J1 | 2-px EGA camera and sprite grid | scrolling, Keen walking | cam_x step stddev 2.0-3.5 px (vanilla, 35 Hz); 1.67 → 0.44 → 0.11 at 60 Hz | Fixed by interpolation (`pixel`/`subpixel`); residual in #86 | No |
| J2 | 35 Hz sim vs display refresh (cadence) | everything | EGA 60 Hz: 2,2,1 hold pattern; VGA 70 Hz: 91-99% exact 2 | Fixed by interpolation | No |
| J3 | PIT clock vs VGA refresh beat | everything, DOS only | 70.088/70.072 Hz vs 70.086 Hz; beat ~519 s / ~69 s | Not applicable in fork (synthetic clock); not worth fixing | n/a |
| J4 | Sub-pixel velocities (1/16 px units) | Keen and actors moving | 3 px/frame walk; 2,4,2,4 after flooring | Mostly J1; residual open → #87 | No (presentation), yes if velocities changed |
| J5 | Camera follow rule (`ScrollScreen` dead zone, vertical follow speed) | background vs Keen | scr_x/scr_y step stddev in baseline table | Open → #86 | Presentation variant possible [inference] |
| J6 | Keen vs a moving platform | Keen on platforms | none | Open → #85 | Probably presentation-only [inference] |
| J7 | Per-state tic counters and the `tics` carry-over | sprite animation durations | none | Open → #88 | Yes, if sim states change |
| J8 | `tics=2` vs `tics>2` behavioural differences (24 sites) | per-frame step sizes, thresholds | none (qualitative) | Not a jitter source at `tics=2`; matters for demos only | n/a |
| J9 | Odd-tic acceleration and the per-frame clamps | jump arcs, falls | none | Not worth fixing at `tics=2` | Yes |
| J10 | Interruption clock jumps (R1, R2, R3) | first frames after menu/dialog | none | Fixed: `clock-continuity` | Yes |
| J11 | MAXTICS double charge (R4) | after a real stall | none | Fixed: `stall-charge-once` | Yes (stalls only) |
| J12 | Tile animation steps (`RFL_AnimateTiles`) | water, lights, conveyors | none | Not a jitter source at `tics=2`; R1 part fixed by J10 | No |
| J13 | Score box and 1-px panning | HUD | none (KEEN70HZ regression) | Not applicable to vanilla; pitfall for #77 | No |
| J14 | "Fix Jerky Motion" (`jerk`) / mid-screen flip | DOS only | none | Not a fork source; no-op | n/a |
| J15 | Interpolation's own artefacts (snap, stall catch-up) | teleports, hitches | none | Rules documented; verify | No |

---

## J1. 2-pixel EGA grid for camera and sprites

- **Mechanism.** `xpanmask = 6` ("dont pan to odd pixels", ID_RF.C:204, :318) makes the displayed
  camera origin `(originxglobal >> 4) & ~1`. Sprites have 4 pre-shifts at 0, 2, 4, 6 px
  (`CAL_CacheSprite`, ID_CA.C:1356-1407; `RF_PlaceSprite`, ID_RF.C:1977-1986), so they sit on the same
  even grid; sprites cached with `shifts=2` or `1` move on a 4 or 8 px grid. Vertical has no mask
  (ID_RF.C:1058, :1987). Omnispeak reproduces it exactly: `rf_scrollXUnit & 0xef` (id_rf.c:1458-1459)
  and `(unshiftedX&7)/2` (id_rf.c:1306-1315). (ID_RF.md §2.)
- **Where.** Walking is `xmove=24` units/tic (CK_KEEN.C:157), 48 units = 3 px per frame; floored to 2 px
  it steps **2, 4, 2, 4** px. Any odd-pixel-per-frame motion does the same. The scene scrolls
  unevenly past a Keen who stays at a fixed screen position.
- **Measured.**
  - #89 spike, 60 Hz display, Keen at 3 px/frame: scroll-step stddev **1.67 vanilla → 0.44 whole-pixel
    interpolation → 0.11 quarter-pixel** (PLAN.md; memory note). The effect is display-independent: also
    present at 144 Hz and VRR, and larger than the 35-vs-60 Hz cadence effect.
  - #82 baseline, Keen 4 golden demos, sim-frame level, grid 2 (HARNESS.md): `cam_x` step stddev 2.05-3.49,
    with 36-188 "uneven" same-direction size changes per demo; `alt` (1-px reversals) is 0 everywhere. Keen's
    own `scr_x` stddev is 1.22-2.62. These demos run at `tics=3` and include real input, so they are not
    the pure 3 px/frame walk.
- **Status.** Fixed by interpolation: `pixel` quantises to 16 units (1 px), `subpixel` to 4 units (1/4 px),
  `grid` keeps the vanilla 32-unit X grid (INTERP.md; rf_interp.c:278-306, :346-367). The user judged
  whole-pixel indistinguishable from quarter-pixel (PLAN.md). Residual (relative camera-vs-Keen offset
  from the follow rule) goes to #86 (J5).
- **Sim-affecting?** No. The grid is a presentation quantisation; the sim keeps full-resolution units.
  Beware the opposite trap: panning at 1 px while sprites stay on the 2-px grid makes things worse
  (KEEN70HZ, TIMING §9). The interpolator avoids this by composing the whole frame from unquantised
  positions.

## J2. 35 Hz sim against the display refresh (cadence)

- **Mechanism.** Each 2-tic sim state is held for as many refreshes as fit (TIMING §3). VGA 70.086 Hz:
  exactly 2 refreshes. EGA ~60 Hz: 1.71, a **2,2,1,2,2,2,1** pattern (TIMING §5). At 120 Hz a 3/4 mix, at
  144 Hz mostly 4 with a 5 about every ninth frame (TIMING §10 E8-5).
- **Where.** Everything that moves, uniformly. This is the "judder" the original always had on EGA.
- **Measured.** DOSBox-X at 70.09 Hz: 91-99% of presented frames exactly 2 vblanks apart (TIMING
  intro). Cadence arithmetic above is from TIMING, not a fresh measurement.
- **Status.** Fixed by interpolation: it presents at the display rate and lerps from the previous to the
  current sim frame (INTERP.md "How the loop works"). Measured present pacing with the pacer: 60.01 /
  74.98 / 120.00 / 143.75 Hz, dt stddev about 0.5-0.65 ms (INTERP.md "Audit"). Cost: one sim frame
  (28.6 ms) of latency (memory note).
- **Sim-affecting?** No.

## J3. PIT game clock against the VGA refresh (slow beat)

- **Mechanism.** `TimeCount` runs at 70.088 Hz (AdLib music) or 70.072 Hz (music off), because
  `SDL_SetIntsPerSec` divides by 1192030 and truncates (ID_SD.C:154-157, :897-899). VGA mode 0Dh refreshes
  at 70.086 Hz. The phase drifts once per ~519 s (music) or ~69 s (no music) (TIMING §1).
- **Where.** DOS only: a pair of frames shown for 1 and 3 refreshes instead of 2 and 2 when the phase
  crosses the flip window (TIMING §3). Explains why 91-99% and not 100% are exact.
- **Status.** Not present in the fork once interpolation owns the clock (`frameStart += 2/70 s`; TIMING
  §8, INTERP.md). Without interpolation the fork uses the emulated ISR with the same rates
  (TIMING §1). KEEN70HZ locks the clock to vblank; we do not need that (TIMING §9). Not worth fixing.
  Open nit: INTERP.md uses `dur = tics/70` while the real rate is 70.07-70.09 Hz; irrelevant to
  smoothness, matters only for exact A/B speed (TIMING §10 E8-4).
- **Sim-affecting?** Not applicable.

## J4. Sub-pixel velocities (units are 1/16 px)

- **Mechanism.** Global coordinates are 1/16 px (`>> G_P_SHIFT`). A constant velocity that is not a
  multiple of 16 units per frame gives 1-2-1-2 px steps when the position is rounded to pixels, and a
  camera doing the same thing out of phase reads as shimmer (issue #87 text). Per-frame motion is
  `speed*tics` (linear sites, ACTORS §8 items 21-23), step states move in whole `xmove` quanta at
  state boundaries (ACTORS §8 item 4), and platform speeds are `12*tics`, `16*tics` etc.
- **Where.** Walking, platforms, flyers, any actor with a non-integral px/frame speed. On the grid it
  merges with J1 (the 2,4,2,4 pattern is the 3 px/frame walk).
- **Measured.** Nothing separate from J1; the #82 baseline cannot separate the two (the trace score is
  on the floored grid).
- **Status.** With interpolation at `pixel` or `subpixel`, sprite and camera are lerped from the same
  unquantised positions and quantised once (rf_interp.c:350, :367), so most of this is already gone.
  **[inference]** What #87 can still add at `subpixel` is small: the spike's quarter-pixel stddev was
  already 0.11. Open → **#87**; I would re-measure after J5/J6 before spending effort here.
- **Sim-affecting?** Presentation: no. Changing the sim's velocities would be, and should not be done.

## J5. Camera follow: dead zone, vertical follow speed, look up/down

- **Mechanism.** `ScrollScreen` (CK_PLAY.C:1529-1650; verified): horizontally the camera does not move
  while Keen's `x` is between `originx + 9 tiles` and `originx + 12 tiles`, and outside it follows by
  the exact overshoot. So after Keen enters the dead zone's edge the camera copies his motion 1:1
  (hence J1's pattern), and in the middle Keen moves against a still background. Vertically, the follow
  speed is `min(pixels-to-global>>7, 0x30) * tics`, floored at `0x10` and capped at `pix` (:1629-1647), so
  vertical catch-up is a speed-scaled step each frame; look up/down moves `tics` px per frame, clamped
  to 167/33 (ACTORS §8 items 11). The floor dominates at small tics (ACTORS §8).
- **Where.** Keen's screen position stepping unevenly as he crosses the dead-zone edge; vertical
  settling after jumps and landings; the background advancing unevenly against Keen.
- **Measured.** #82 baseline (HARNESS.md, grid 2, k4-d0..d4): `scr_x` step stddev 1.22-2.62,
  `scr_y` step stddev 3.86-4.61 (large because jumps and falls move Keen on screen), `cam_x`
  2.05-3.49. The tool re-derives the camera by running the game's `ScrollScreen` with `tics=3`, so
  these include the demo `tics=3` behaviour, not live `tics=2`.
- **Status.** The 2-px component is fixed by interpolation. What is left (rule-level stepping, the
  nonlinear vertical follow) is open → **#86**.
- **Sim-affecting?** The camera origin does not feed the sim except through activation and
  deactivation boxes (CK_PLAY.C:1397-1415, ACTORS §3.1), so changing it **could** change which
  objects are active; **[inference]** a presentation-only camera (smoothed copy used only by the
  renderer, while the sim keeps the vanilla origin) avoids that and is the variant to try first, as #86
  suggests. The trace has no camera (HARNESS.md "Camera"), so verification needs the motion-trace tool
  or the present log.

## J6. Keen against a moving platform

- **Mechanism.** From the code, not yet measured. Per frame (ACTORS §3.1): think/move for every object
  in list order (Keen and the platform each move and clip), then `HandleRiding` (CK_PLAY.C:2248,
  CK_KEEN.C:1892), then reaction/placement (`RF_PlaceSprite`), then the camera. `HandleRiding` pushes Keen by
  `plat->xmove`, snaps his Y to the platform top (`ytry = top - bottom - 16`), then ORs the platform's
  sub-2-px bits into Keen's `x` (`ob->x |= plat->x & SUBPIXELMASK(2)`, CK_KEEN.C:1926-1964). The
  source comments say this prevents Keen and the platform sprite moving back and forth against each other
  when the screen scrolls on the 2-px grid, and that it is "a bit buggy": the bits are not cleared
  first, and in EGA mode it applies even while Keen walks on the platform.
- **Where.** Keen sliding or shaking by up to 1 px relative to the platform's top surface, particularly while
  Keen walks on it or the camera is scrolling.
- **Measured.** Nothing. #85 asks for a before/after motion trace. The motion-trace tool does not yet
  give a Keen-minus-platform relative series (`--class` selects one object only, HARNESS.md).
- **Status.** Open → **#85**.
- **Inference (flagged).** (a) The OR without clearing means Keen's x bit 4 (1 px) is forced to the
  platform's, so his X can jump by up to 16 units on a frame with no input; under interpolation that
  jump would be lerped as a real 1 px move relative to the platform. (b) Keen's Y is snapped to the
  platform top every frame, but the platform sprite's Y comes from its own move, so they should agree;
  any visible vertical shake would be from the prev/cur lerp using different histories for Keen and the
  platform. (c) When the platform is later in the object list than Keen, Keen is moved first and then
  corrected, so the corrected position is what both prev and cur record. None of this is verified.
- **Sim-affecting?** The alignment code writes Keen's real `x`, so removing or changing it **changes the
  sim** (demo traces through platform levels would diverge). **[inference]** A presentation-only
  variant is to render Keen at `platform_screen_pos + offset` while riding, keeping the sim alone.

## J7. Per-state tic counters: uneven animation frame durations

- **Mechanism.** `StateMachine`/`DoActor` carry an excess of up to `tics-1` into the next state
  (CK_STATE.C:1156-1190); the excess is discarded when the next state's `tictime <= excess`
  (non-skippable truncation, :1176-1179), and `DoActor(ob,-1)` runs for non-skippable `tictime==0`
  states (ACTORS §3.2, §8 items 1-3). Think frequency is per frame, not per tic (item 5).
- **Where.** Walk cycles and other animations: individual frames shown for unequal numbers of sim
  frames when `tics` is not a divisor of the state's `tictime`.
- **Measured.** None. #88 has no data yet; no animation-duration metric exists in `motiontrace.py`.
- **Status.** Open → **#88**. ACTORS §8 shows the dependence on `tics`: at `tics=2` excess is 0 or 1,
  at `tics=3` it is 0-2. With the enhanced clock fixed at `tics=2` the differences collapse to a single
  column that is reproducible in live play (TIMING §10 E9-2). **[Inference from the carry rule]**: even at
  `tics=2`, a state with odd `tictime` leaves a 1-tic excess every other frame, so its on-screen
  duration alternates between two values.
- **Sim-affecting?** Yes if the fix changes which states are traversed or when thinks run. A
  presentation-only variant exists in principle (choose the displayed sprite from the continuous
  elapsed tic count instead of the state chain) but it fights the pipeline because the sprite shape is
  written by the state machine **[inference]**. Verification needs a `tics=2` input-replay harness,
  since the built-in demos run at `tics=3` (TIMING §10 E9-2).

## J8. `tics=2` against `tics>2`: the 24 divergence sites

- **Mechanism.** ACTORS §8 lists them: carry-over/truncation, step states in whole `xmove` quanta,
  think frequency per frame, odd-tic loops, move clamps (a fall at `yspeed=70` is 140/210/280/350 for
  tics 2/3/4/5, clamped to 255 from `tics>=4`), push-to-floor, camera speed scaling, `invincible`,
  pogo `oldfirecount += tics` (pogo on the 6th frame at tics=2, 4th at tics=3), pole lockout
  (19 tics ≈ 10 frames at 2, 7 at 3), probability sites `US_RndT() < tics*k`, etc.
- **Where.** Not a visible jitter source in itself at a steady `tics=2`. It becomes one only when `tics`
  varies from frame to frame (slow machine: `tics` 3-5).
- **Measured.** None for jitter. The built-in demos run at `tics=3`, so golden traces cover the
  `tics>2` column and not live play (ACTORS §8, TIMING §7).
- **Status.** Removed as a source by the fixed 2-tic interpolating clock (INTERP.md "How the loop
  works"; TIMING §10 E9-1). In vanilla it remains, by design. Not worth fixing separately.
- **Sim-affecting?** n/a.

## J9. Odd-tic acceleration and the per-frame move clamps

- **Mechanism.** `DoGravity`, `AccelerateX/Xv/Y`, `FrictionX/Y` loop `for (i = lasttimecount-tics;
  i < lasttimecount; i++)` and accelerate only on odd `i` (CK_STATE.C:1360-1660; ACTORS §5). At
  `tics=2` that is exactly one acceleration per frame (parity-safe); at `tics=3` it is 1,2,1,2.
  `DoGravity` returns early on an odd tic when `yspeed` is in -4..-1, dropping that frame's `ytry`
  (:1371-1376). `ClipToWalls` clamps `xtry` to ±239 and `ytry` to -239..255 (:462-477).
- **Where.** Jump apex and fall arcs. At `tics=2` the arcs are regular; at `tics=3` (demos) uneven
  by one acceleration step.
- **Measured.** None.
- **Status.** Not a source at `tics=2`. **[inference]** The `DoGravity` early return could cause a one-frame
  vertical hiccup near the apex at `tics=2` too (odd tic always lands on the same phase), but the docs do
  not show it as measured. Not worth fixing; any fix is sim-affecting and breaks vanilla physics.
- **Sim-affecting?** Yes.

## J10. Interruption clock jumps (menus, pause, status window, dialogs): R1, R2, R3

- **Mechanism.** TIMING §6. `RF_ForceRefresh` runs two `RF_Refresh` calls, each ending in
  `RF_CalcTics`. After a menu of `M > 5` tics: refresh #1 gets `tics=M` clipped to 5, refresh #2 animates
  tiles with `tics=5` and then clamps and waits (ID_RF.C:1278-1283, :1602-1603, :1627, :1637-1641). The
  four flagged `lasttimecount = TimeCount; // BUG` lines (CK_PLAY.C:779, :859, :876, :897) are
  effectively no-ops (R3).
  **There is no sim tic burst** (TIMING §6); the premise of #84 does not hold for the sim.
- **Real residue.** **R1**: animated tiles skip about 3 tics ahead in the first two frames after any
  menu or dialog. **R2**: `lasttimecount` jumps by the menu's wall time; `lasttimecount`-phased sprites
  (K4 thrusters `(lasttimecount>>2)&1`, K5 Sphereful `(lasttimecount/8)&15`) jump phase, and the pole
  re-grab lockout (19 tics) expires during a pause.
- **Where.** The first frames after closing a menu, pause, status window or dialog.
- **Measured.** None (analysis only).
- **Status.** **Fixed by the `clock-continuity` toggle** (#84 F1; `ck_fix.h`: on in `enhanced`, off in
  `vanilla`). Under interpolation the same behaviour is inherent because the clock is synthetic
  (TIMING §8, §10 E9-1).
- **Sim-affecting?** Yes (R2 changes pole-lockout gameplay; R1 writes animated tile numbers into the map
  plane), but invisible to demo traces because `CheckKeys` does not run in playback (TIMING §6). The
  toggle is therefore marked `sim = true` and is forced off in demos.

## J11. MAXTICS double charge on real stalls: R4

- **Mechanism.** On a frame longer than 5 tics, `lasttimecount` is set to the unclipped time but
  `TimeCount` is pulled back by only the excess, so the next frame measures `elapsed - excess`, can go
  negative, is lifted by the clamp, and waits 2 more real tics (ID_RF.C:1602-1603, :1627, :1637-1641;
  ID_RF.md §7). Example: a 10-tic spike costs 14 real tics for 7 sim tics (TIMING §6, ID_RF.md §7).
- **Where.** After an in-play hitch of more than 5 tics, the game runs slow for up to 2 frames.
  Real on slow or stalling machines; does not occur at a steady 35 fps. Demos are immune (TIMING §7).
- **Measured.** None for jitter. INTERP.md measured the fork's replacement policy: a 1 s stall advanced
  1 sim frame; a 76 ms stall advanced 2.
- **Status.** **Fixed by `stall-charge-once`** (#84 F2) for the vanilla loop; under interpolation there
  is no MAXTICS path (every step is 2 tics) and a backlog over `rf_maxTics` is dropped (INTERP.md
  "After a stall"). KEEN70HZ made the same change (TIMING §9). Open design point carried in TIMING §10
  E8-3: interpolation can still catch up a 71-250 ms backlog by running up to ~2 sim frames without
  presenting (older notes said ~8, INTERP.md says at most 2 extra now); see J15.
- **Sim-affecting?** Yes, on stalls only; the toggle is `sim = true`.

## J12. Tile animation (`RFL_AnimateTiles`)

- **Mechanism.** `count -= tics; while (count < 1) { advance; count += SPEED }` once per refresh, writing
  the new tile into the map plane (ID_RF.C:931-1005; ID_RF.md §6). Uses the *previous* frame's `tics`.
  DOS can step several frames in one refresh; Omnispeak advances at most one (`if`, id_rf.c:557).
- **Where.** Water, lights, conveyors. Animated tiles change only at 35 Hz sim steps, and at 70 Hz-style
  hold patterns on the display.
- **Measured.** Shipped data analysis (ID_RF.md "Animation speeds in the shipped data", #95): **at
  `tics=2` nothing can double-step in K4 or K5** (shortest speed is 2, and 2 is not < 2). The
  `tics=3` demo case has 8 K4 foreground chains of speed 2 (tiles 981-984, 1482-1485) where DOS cycles
  1.5x faster than the fork; gameplay identical, visual phase differs only.
- **Status.** Not a jitter source at `tics=2`. The R1 skip is fixed by `clock-continuity` (J10). The
  interpolator composes from the map planes (INTERP.md "Renderers"); tile animation is still stepped
  at the sim rate, so it stays at 35 Hz presentation (E8-7, TIMING §10); a smoother tile animation is an
  option, not a defect, and **[inference]** not worth changing since tile animations are slow (most speeds
  are 4-16+ tics).
- **Sim-affecting?** Stepping at the sim rate is required (it writes the map plane); only presentation
  could be smoothed.

## J13. Score box and 1-pixel panning

- **Mechanism.** The score box is a sprite (`scoreobj`, CK_PLAY.C:1786) placed in map coordinates each
  frame (TIMING §9). With the vanilla 2-px camera and sprite grid it stays put. KEEN70HZ's "1 Pixel
  Panning" (`xpanmask` 7) panned the background at 1 px while sprites stayed on the 2-px grid, so on odd
  pans the box landed ±1 px from where it should be, and Keen (followed by the camera) wobbled the same
  way (TIMING §9).
- **Where.** HUD shake. Absent in vanilla by construction; a failure mode for any 1-px camera.
- **Measured.** None.
- **Status.** Not present in vanilla. The interpolated renderer composes the HUD in screen space
  (ROADMAP #77 "HUD/scorebox pinned in screen space"). The status-window scroll (`vislines ± tics*8`) is
  composed onto the vanilla screen that `rf_interp` bypasses; from reading the code it seems not to appear
  in interp mode and has not been run (ID_RF.md §10, TIMING §10 E8-8). **[inference]** Verify this in a
  browser/capture before declaring the HUD fine.
- **Sim-affecting?** No.

## J14. "Fix Jerky Motion" and mid-screen flips (DOS)

- **Mechanism.** `jerk` only doubles the vblank test in `SyncVBL` from 5 to 10 reads (ID_VW_AE.ASM:1054),
  to avoid mistaking a horizontal blank for vblank on a fast bus, which flips the pel pan mid-screen
  and shows up to 7 px of misalignment for one refresh (TIMING §4; this is the asm interpretation and
  was not reproduced on hardware).
- **Where.** DOS hardware only.
- **Status.** In Omnispeak nothing reads `ck_fixJerkyMotion` (ck_play.c:93-94); presentation goes through
  the backend's vsync. It is a no-op and not a jitter fix (TIMING §4, §10 E9-4). Answers the question in
  the original #50 list ("what 'Fix Jerky Motion' really changes").
- **Sim-affecting?** No.

## J15. Interpolation's own artefacts

These are potential sources introduced by the fix itself; the rules are in INTERP.md "Snap rules".

- **Snap threshold.** A move larger than `SNAP_UNITS` (768 units = 48 px) is treated as a teleport and
  not lerped (rf_interp.c:85, :252). Legitimate fast motion above 48 px per sim frame would snap
  (**[inference]** none expected in Keen 4/5 gameplay since `ClipToWalls` clamps one-frame moves to under
  256 units = 16 px, ACTORS §4; camera snapping from scroll blocks or a re-centre is the realistic case).
- **New sprites** draw at their current position with no lerp (`prev = cur`, rf_interp.c:227).
  **[inference]** A sprite that spawns moving (e.g. a shot) appears 1 sim frame "ahead" compared with a
  continuously lerped one; unlikely to be visible.
- **Sprite-chunk changes** are still lerped on the object position, with each chunk's own origin added at
  compose time (rf_interp.c:364-365), so a frame change does not shift the sprite.
- **Stall catch-up** (E8-2, E8-3 in TIMING §10): `frameStart` resets after a stall, and discontinuity
  points beyond the 0.25 s rule/`SNAP_UNITS` (level load is handled by `RFI_Reset`) are open design
  items. INTERP.md now implements: reset after more than `rf_maxTics` tics behind, otherwise catch up by
  at most 2 extra sim frames.
- **One sim frame of display latency** (memory note) is a trade-off, not jitter.
- **Not sim-affecting** by construction; the vanilla guard checks the trace (INTERP.md "Audit": PASS on
  K4/K5 demos 0 and 1).

---

## What matters most with interpolation ON

With the fork's `interpolation` fix on (and `clock-continuity` / `stall-charge-once` in `enhanced`),
J1, J2, J3, J10 and J11 are done, and J8/J12/J14 are not sources. What is left, ranked by my estimate of
perceived smoothness. **The ranking is my inference**, since only J1 has display-rate measurements; the
others have never been measured and the first step in each issue is to capture a number.

| Rank | Source | Issue | Why |
|---|---|---|---|
| 1 | J6 Keen against a moving platform | **#85** | A visible relative shake between two things the eye compares directly; likely changes character under interpolation because Keen's `x` is rewritten each frame by `HandleRiding`. Needs a Keen-minus-platform metric first. |
| 2 | J5 Camera follow stepping (dead zone, vertical follow) | **#86** | Governs how the whole world moves relative to Keen all the time. Largest remaining unevenness in the baseline (cam_x stddev 2.0-3.5 at the sim level, before interpolation; vertical stddev 3.9-4.6). Presentation-only variant is feasible. |
| 3 | J7 Animation frame durations | **#88** | Visible on walk cycles, but only for odd state lengths and only if `tics` varies or the carry rule bites at `tics=2`. Only fixable in the sim; verification needs a `tics=2` replay harness. |
| 4 | J4 Sub-pixel velocity stepping | **#87** | Mostly absorbed by J1's fix; re-measure after J5/J6 and probably fold into them. |
| 5 | J13 HUD / status window in interp mode | none (E8-8, #77) | Not a jitter source in vanilla, but check it is not broken in interp mode. Not tied to #85-#88. |
| 6 | J15 stall policy and snap rules | none (E8-2/E8-3) | Robustness, not steady-state smoothness. |

**Suggested order for M5.** (1) extend `motiontrace.py` with a relative series (Keen-minus-platform; and
a present-log score at display rate), then (2) #86 and #85 together since they share the camera/ride
quantisation chain, then (3) #88, then (4) #87 as a residual check. All of #86/#85/#87 should first try a
presentation-only variant; only #88 is likely to need a sim-affecting toggle.

## Gaps and things I did not do

- No emulator or build was run; every "where you see it" for J4-J9 is from the code and docs, not a
  capture. The original #50 task asked for a repro scene and a motion-trace plot per item; this
  catalogue has no repro scenes yet, and only J1/J5 have numbers.
- Issue #50's candidate (4) "scorebox shake" is covered by J13 (a KEEN70HZ pitfall, not a vanilla
  source); candidate (1) "four lasttimecount BUG sites" is refuted for the sim (J10); candidate (9) "what Fix
  Jerky Motion changes" is J14.
- The #82 baseline is sim-frame level at `tics=3` (demos), not live play at `tics=2`, and does not model
  scroll blocks.
- New issues to file from this catalogue (if the coordinator agrees): a relative-motion metric for
  `motiontrace.py` (Keen vs platform, and animation-frame duration histogram for #88); a `tics=2`
  input-replay harness (TIMING §10 E9-2); a check of the status-window slide in interp mode (J13).
