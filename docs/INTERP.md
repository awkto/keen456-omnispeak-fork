# Display-rate interpolation: how it is driven

Status notes for keen456-decomp #74 ("Decouple presentation from simulation").
The code is `src/rf_interp.c`, hooked from `RF_Refresh` in `src/id_rf.c`.
It is the presentation-only fix `interpolation` (`src/ck_fix.h`), on in profile `enhanced`.

## How the loop works

The game loop is not restructured. `CK_PlayLoop` still runs one sim frame and
then calls `RF_Refresh`. With interpolation on, `RF_Refresh` calls `RFI_Frame`
in place of `VL_Present` and `RFL_CalcTics`:

1. The sim frame always advances by 2 tics (one vanilla frame at the 70 Hz PIT
   rate, 28.6 ms), except during demo playback, where it uses the demo tics (3).
   `frameStart += 2/70 s` per sim frame is the accumulator.
2. Until wall time reaches `frameStart + 2/70 s`, it composes and presents frames
   at display rate with `alpha = (now - frameStart) / (2/70 s)`. Each frame
   interpolates the camera and every sprite from the previous sim frame to the
   current one.
3. Then it hands back to the sim for the next frame. If the sim is behind, the
   presents are skipped (alpha is already 1 or more) and the sim catches up.

In demos (playback, recording, `/DUMPFILE`) `RFL_CalcTics` still runs, so
`TimeCount` and the playloop dump stay identical to vanilla.

## Snap rules

| Case | Rule | Where (file:line) |
|---|---|---|
| Large jump (teleport, warp within a level) | No interpolation: a move larger than `SNAP_UNITS` (48 * 16 = 768 units = 48 px, since 16 units = 1 px) is treated as a teleport, so `Lerp` returns the current value | `src/rf_interp.c:85`, `src/rf_interp.c:252` |
| Keen leaves the door state (`CK_ACT_keenEnterDoor4`) | The door think function warps Keen and recentres the camera. At the first sim frame after it, the camera and every sprite snap together (`prev = cur`), even when the warp is shorter than `SNAP_UNITS`. All door warps in the K4/K5 demos are longer than 48 px, so they were already snapped by distance | `src/rf_interp.c` (`SnapOnKeenTransition`) |
| Newly created sprite | Drawn at its current position (fresh), with no interpolation: `RFI_NoteSprite` sets `prev = cur` when the sprite draw entry is new | `src/rf_interp.c:227`, `src/id_rf.c:1260`, `src/id_rf.c:1337` |
| Sprite frame (chunk) change, e.g. walk cycle or state change | Still interpolated: the object position is interpolated, and each chunk's own origin is added at compose time | `src/rf_interp.c:364`, `src/rf_interp.c:365`, `src/rf_interp.c:222` |
| Level load | `RFI_Reset()` from `RF_NewMap` snaps the camera and all sprites on the first frame of the new map, by dropping the interpolation history | `src/rf_interp.c:237`, `src/id_rf.c:665` |
| Camera | Interpolated like sprites, with the same `SNAP_UNITS` rule | `src/rf_interp.c:350`, `src/rf_interp.c:252` |
| Quantisation per mode | `grid` = vanilla 2-px/EGA grid, `pixel` = whole pixels, `subpixel` = 1/4 pixel; applied with `Quant` | `src/rf_interp.c:346`, `src/rf_interp.c:350`, `src/rf_interp.c:367` |
| Keen riding a platform (`pixel`/`subpixel` only; `grid` unchanged) | Keen is drawn at the platform's quantised position plus `Quant(Lerp(relPrev, relCur))`, the interpolated Keen-minus-platform offset, so independent rounding cannot jitter him on the platform (keen456-decomp #85). Falls back to the normal path if either sprite or the relative offset would snap (`SNAP_UNITS`), or the platform is not drawn this frame | `src/rf_interp.c` (`Compose`) |
| Camera smoothing on (`camera-smoothing`, opt-in; `pixel`/`subpixel` only) | The displayed camera D is snapped to the target (V=0) when the target moves more than `SNAP_UNITS` in one compose, on `RFI_Reset`, on the first frame, and when the fix or mode changes | `src/rf_interp.c` (`SmoothCamera`) |
| Draw hook active (Enter-key status window, `rf_drawFunc`) | Interpolation is bypassed for those `RF_Refresh` calls: the vanilla frame is presented (the hook paints the screen surface, which the compositor never reads), then `RFI_Reset()` drops history so the next interpolated frame snaps. Without it the window pops in with no slide (keen456-decomp #77) | `src/id_rf.c` (`RF_Refresh`) |
| Scorebox / demo sign (HUD sprite; `pixel`/`subpixel`) | Drawn at the displayed camera plus its fixed screen offset (`curU - rf_scrollU`), not at its own lerp, so it stays pinned when `camera-smoothing` makes the camera differ from the plain lerp (#77). The debug CSV has `scorebox_x_qpx,scorebox_y_qpx` | `src/rf_interp.c` (`Compose`) |
| Widescreen view (#103) | The frame is `RF_ViewWidth()` game pixels wide; its left edge is `RF_WideViewLeft` of the displayed (lerped, quantised) camera, so the clamp at map edges and scroll blocks moves with the camera. The band itself comes from the sim camera, and the displayed camera is clamped into it on both axes (also at 320 wide), so a camera-smoothing overshoot never shows past a block or the map border. The scorebox is drawn last at the wide view's corner, after the black fill beyond the band. Presents outside `RFI_Frame` (draw hook, dialogs, fades, smoothing off) use `RFI_PresentWideStill`: grid compose at alpha 1 plus the vanilla screen over the camera window | `src/rf_interp.c` (`Compose`, `RFI_PresentWideStill`), `src/id_rf.c` |
| Demos and dumps | Game state is never touched; the vanilla clock is kept during demos, with `RFL_CalcTics` instead of the fixed 2-tic step | `src/rf_interp.c:507`, `src/rf_interp.c:511`, `src/id_rf.c:997` |

If you add a rule, add a row here.

### Snap audit (keen456-decomp #76)

Run on all 15 built-in demos (K4, K5, K6 demos 0-4) in `pixel` mode with
`OMNI_DEBUG_INTERP_SPRITES=<file.csv>` (one row per sprite per display frame, with the
interpolated screen position, prev/cur object position, and Keen's action) plus
`OMNI_DEBUG_INTERP_CAPTURE` for the frames. Findings:

- Door exits (K4 demos 0, 2, 3; K5 demo 2): Keen, a second sprite, and the camera warp 200-1400 px in one
  sim frame. Already snapped by `SNAP_UNITS`, all in the same sim frame (no frame where one is snapped and
  the other is not). Added the door-exit rule above for short warps, which no demo contains.
- Pole grabs (all episodes): Keen's `posX` moves up to 5 px onto the pole centre and the sprite's hotspot
  changes. Not snapped. The camera follows Keen, so with both interpolated Keen stays put on screen; a
  trial that snapped only Keen made him slide 8 px against the view over 2 display frames, which is worse.
- Ledge grab and pull (K5 demos 2, 3; K6 demos 0, 1): 8-14 px steps per sim frame, which is the actual
  motion in vanilla too (camera locked). Interpolated.
- Spawns and despawns: a sprite entry without history is `fresh` (`prev = cur`); entries are freed on
  despawn and reused as fresh. No lerp across a reused entry was seen.
- Level and world-map loads: `RFI_Reset`; the camera and sprite history are dropped on the new map.
- Other large steps (jump launch about 15 px per 3-tic demo frame, falling, bouncing enemies that reverse
  at 8 px per frame) are ordinary motion: they match the vanilla step and are not snapped.
- Gaps: no demo contains a K5 teleporter/elevator, a K6 door/teleport, the world map, or a death/respawn.

`OMNI_DEBUG_INTERP_SPRITES` also writes `# snap ...` comment lines where a rule fired.

## Camera smoothing (opt-in)

Fix `camera-smoothing` (keen456-decomp #86; presentation only, off in every profile; enable with `/FIX camera-smoothing` or `fix_camera-smoothing = true`) smooths the displayed camera in `pixel` and `subpixel` mode, never `grid`. The target T is the unrounded `Lerp(prev, cur, alpha)`. Each composed frame, with dt the real time since the previous compose (virtual time in a capture), the displayed velocity V is low-passed toward the velocity of T (tau 50 ms), D += V*dt, D is pulled toward T (`min(1, dt/100 ms)`), and |D-T| is clamped to 6 px. Everything drawn relative to the camera (tiles, sprites, the riding-Keen path) uses D. The debug CSV has `cam_dev_x_u,cam_dev_y_u` (D-T in units). Cost: Keen drifts a few pixels on screen and the view lags by about tau.

## Audit against #74

Columns: what #74 asks for · on `main` at f7a452d · on `feat/present`.

| #74 asks for | main @ f7a452d | feat/present |
|---|---|---|
| Fixed-step sim on the 70 Hz clock | Yes. Always 2 tics a step; the vanilla clock in demos | Unchanged |
| Presenter at display rate with (prev, cur, alpha) | Yes. Camera and sprites keep prev/cur, and alpha comes from wall time | Unchanged |
| Opt-in switch, vanilla by default | Yes. `/INTERP`, `interpolation` option, the `enhanced` profile | Unchanged |
| 60/75/120/144 Hz | Yes only with vsync. Without vsync the rate was uncapped (about 220 Hz on dummy video) | With vsync: the display paces it. Without vsync: sleeps to the display mode's rate, or to `OMNI_DEBUG_INTERP_HZ`. Measured 60.01 / 74.98 / 120.00 / 143.75 Hz (dummy video, 4 vCPU droplet). With no rate given, dummy video paces at 60.01 Hz. |
| VRR | Not handled specifically | Not tested on hardware. With VRR and vsync, the present rate is capped at the refresh rate of the display mode |
| Never sleep-spin | `SDL_Delay(1)` after every non-blocking present. That is not a spin, but the rate is uncapped | Sleeps to a deadline with ms-granular `SDL_Delay` (±0.5 ms jitter). No spin loop |
| Survive dropped frames | Yes. A present that arrives late is skipped | Unchanged |
| Survive stalls (window drag, breakpoint) | Catches up for 250 ms: a burst of up to about 8 sim frames with no present | Catches up for at most `rf_maxTics` (5) tics, then drops the rest. See "After a stall" below |
| Acceptance: sim trace identical to golden | Yes. `tools/vanilla-guard.sh`: enhanced == vanilla == tests/ on K4/K5 demos 0 and 1 | Yes, PASS again on feat/present (all 8 comparisons) |
| Acceptance: present timestamps logged | No | `OMNI_DEBUG_INTERP_PRESENTS=<file.csv>` |
| Renderers | `sdl2` and `sdl2sw` only | Adds `sdl2gl` (the Linux default). Still missing: `sdl2vk`, `sdl3`, `sdl3gpu` |

## After a stall (MAXTICS)

Vanilla `RFL_CalcTics` clips a long frame to `rf_maxTics` (5) tics and deducts
the excess from `TimeCount`. In other words, the game runs slow rather than
taking a huge step. Outside demos, the interpolating path never calls
`RFL_CalcTics`: every step is exactly 2 tics, so that clamp does not apply.

The enhanced path does this instead:

- When `RFI_Frame` starts more than `rf_maxTics` tics (71 ms) behind its
  schedule, it resets the schedule to now. The backlog is dropped, and game time
  is lost the same way vanilla loses the excess.
- A smaller backlog is caught up by running whole 2-tic sim frames back to back
  without presenting. That is at most 2 extra sim frames, 4 tics, which is within
  vanilla's 5-tic step.

So no step is ever larger than 2 tics, and a catch-up never covers more game
time than vanilla's clamp allows. Measured with SIGSTOP on the droplet:

- A 1 s stall advanced 1 sim frame.
- A 76 ms stall advanced 2 sim frames.

## Present log (cadence bench, #30)

`OMNI_DEBUG_INTERP_PRESENTS=<file.csv>` writes one line per presented frame:

```
present,host_us,dt_us,sim,alpha,compose_us,work_us,wait_us,gap_us,raf_lag_us,in_raf
```

- `host_us` is the host time after the present call returned, in µs since the
  first present. In an `OMNI_DEBUG_INTERP_CAPTURE` run it is virtual time instead.
- `dt_us` is the gap to the previous present.
- `sim` is the sim frame shown: the frame is interpolated from `sim-1` to `sim`.
- `alpha` is the interpolation factor used. In `grid` mode the logged alpha is
  always 1.000.
- Timing columns (keen456-decomp #98), only meaningful in realtime runs:
  `compose_us` is Compose() (layers, sprites); `work_us` is the palette expand,
  texture upload and draw calls; `wait_us` is time blocked on the display (vsync,
  or in the browser the requestAnimationFrame wait plus SDL's swap yield);
  `gap_us` is the time from the previous present returning to this frame's present
  loop starting (the sim frame and the game loop, when a sim frame ran in between).
- `raf_lag_us` (browser only): present end minus the rAF callback's timestamp.
- `in_raf` (browser only): 1 if the frame was drawn inside a requestAnimationFrame
  callback. Only those reach the screen at that refresh (see below); 0 elsewhere.

The file is flushed once per sim frame. Example (sdl2, dummy video, `/INTERP pixel`,
`/TEDLEVEL 1`, `OMNI_DEBUG_INTERP_SCRIPT=right:20-400`):

```
== OMNI_DEBUG_INTERP_HZ=60   (whole run: 60.01 Hz, dt sd 0.65 ms, sim 35.12 Hz)
250,4478063,15138,143,0.591
251,4494681,16619,144,0.119
252,4513030,18349,144,0.696
253,4527965,14935,145,0.343
== OMNI_DEBUG_INTERP_HZ=144  (whole run: 143.75 Hz, dt sd 0.51 ms, sim 35.01 Hz)
250,2096476,6702,60,0.809
251,2104477,8001,61,0.047
252,2111184,6707,61,0.323
253,2117826,6642,61,0.558
```

## Browser (WebAssembly) pacing (#98)

In the browser a WebGL draw reaches the screen at a given refresh only if it is
made inside that refresh's requestAnimationFrame callback. A draw made in a later
task (after any `emscripten_sleep`, i.e. a `setTimeout`) shows a refresh later and
is then overdrawn by the next rAF's frame, so the refresh it was meant for repeats
the previous picture.

The first web build waited for rAF *after* each present. At a sim boundary the
rAF callback found alpha >= 1, ran the sim frame, and the game loop's
`CK_Cross_Yield` (and SDL's swap) yielded before the next compose. Measured in
headless Chrome, gameplay: only 24 of 58 refreshes a second got a frame drawn
inside the rAF callback, and those all had alpha 0.5..1 (the 0..0.5 half never
reached the screen) -- fewer distinct frames than vanilla's 35 Hz, unevenly
spaced. This is what "smooths 10% of the time" was.

Now `RFI_Frame` waits for the rAF slot *before* choosing alpha
(`VL_WaitPresentSlot`), and returns at once while still inside a rAF callback
that has not drawn yet, so the sim frame and the next frame's compose and draw
happen in the same callback. `VL_PresentARGB` no longer waits after drawing.
The rAF wait counts as a yield for `CK_Cross_Yield`, which therefore no longer
breaks out of the callback between the sim and the draw. The browser clock for
alpha is `performance.now()` (SDL's counter there is `Date.now()`, whole ms).
After the fix, same test: 60 of 60 presents a second drawn inside rAF, alpha
spread evenly over 0..1, present interval p5/p50/p95 15.1/16.7/18.4 ms.

## Pacing without vsync

The pacer sleeps to a deadline and does not spin. It is active in any of
these cases:

- `swapInterval` is 0.
- `OMNI_DEBUG_INTERP_HZ` is set.
- 8 presents in a row either returned in under 0.5 ms or came less than 0.75
  of a refresh period after the previous one. That means vsync isn't pacing
  the presents (dummy video, Xvfb, a mailbox swap chain). This switch is sticky
  for the rest of the run.

The rate is the current display mode's refresh rate, or 60 if that is unknown.
`OMNI_DEBUG_INTERP_HZ` must be a positive integer. The pacer only ever runs when
vsync is off or found missing: it never adds sleeping on top of working vsync.

## Frame dumps (pixel compare)

Set `OMNI_DEBUG_FRAMES=<path>` to append the vanilla (non-interpolated) renderer's
visible 320x200 view to `<path>`, exactly 64000 bytes per `RF_Refresh` as EGA
palette indices 0-15, row-major. `RF_DebugDumpFrame` (`src/id_vl.c`) reads the
screen surface's 320x200 rect at the current scroll origin, the same region
`VL_Present` blits; it is a no-op when the variable is unset, and the file is
closed via `atexit`.

The same variable drives the interpolated renderer (`RFI_DebugDumpFrame`,
`src/rf_interp.c`), which appends one 64000-byte frame per sim frame by
downsampling the composited 1280x800 image. That only matches the simulation in
`grid` mode (alpha forced to 1); in the other modes it warns once and dumps
nothing. To compare the two renderers frame by frame: run the same demo once with
`/NOINTERP` and once with `/INTERP grid`, both with `OMNI_DEBUG_FRAMES=<path>`,
then compare the two files (same byte length, 64000 per frame).

## Fades

Vanilla fades wait 6 tics after presenting each of the 4 palette steps, so on a
vsynced display every step also costs up to one refresh and the fade gets
longer as the refresh rate drops. The `time-based-fades` fix (presentation
only, off in vanilla) schedules each step against an absolute deadline of
6/70 s per step from the start of the fade, so the fade lasts about 343 ms
sleep after each fade present to simulate a blocking vsync.

## Renderers

The compositor produces a 1280x800 ARGB frame (wider in widescreen, e.g. 1712x800 at 16:9; see FILTERS.md). Each backend only has to show it:

- **`sdl2`, `sdl2sw`** (`id_vl_sdl2.c`): `VL_PresentARGB` uses an SDL streaming
  texture.
- **`sdl2gl`** (`id_vl_sdl2gl.c`): `VL_PresentARGB` runs the normal present. It
  draws the frame as a BGRA texture over the screen surface's quad in the
  integer-scale FBO, with the fixed-function pipeline and no palette shader.
  `VL_FlipPageNoPresent` flips the double-buffered screen once per sim frame,
  the flip `VL_Present` would have done. Without it, RF's per-buffer eraser
  bookkeeping overflows ("Too many sprite erasers"). Tested under Xvfb with
  llvmpipe: renders the same as vanilla, at 52 fps at 1920x1080 on the
  droplet's CPU.
- **`sdl2vk`, `sdl3`, `sdl3gpu`**: interpolation is not built (`WITH_RF_INTERP`
  is unset), so `/INTERP` is not recognised. For SDL3, `rf_interp.c` would need
  the SDL3 headers and display-mode API, plus a texture present like the
  `sdl2` one.

Recommendation: ship `sdl2` for release builds. It is the most portable path, and
its SDL_Renderer picks GL, GLES, D3D or Metal itself. `sdl2gl` now works too, so
the upstream Linux default is not a trap.

## Menu

With `WITH_RF_INTERP`, Configure > Options has two extra items (keen456-decomp #97):

- **Smoothing**: cycles Off, Grid, Pixel, Subpixel. Off turns the `interpolation`
  fix off. The choice is saved as the `interpolation` config key, the same key
  `/INTERP` overrides, so it survives restarts.
- **Camera smoothing**: toggles the `camera-smoothing` fix, saved as
  `fix_camera-smoothing`.

Both are presentation-only, so demos and dumps are unaffected. Smoothing can be
switched on at runtime even if the game started with it off.
