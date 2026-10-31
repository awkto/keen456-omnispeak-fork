# Display-rate interpolation: how it is driven

Status notes for keen456-decomp #74 ("Decouple presentation from simulation").
The code is `src/rf_interp.c`, hooked from `RF_Refresh` in `src/id_rf.c`.
It is the presentation-only fix `interpolation` (`src/ck_fix.h`), on in profile `enhanced`.

## How the loop works

The game loop is not restructured. `CK_PlayLoop` still runs one sim frame and
then calls `RF_Refresh`. With interpolation on, `RF_Refresh` calls `RFI_Frame`
in place of `VL_Present` and `RFL_CalcTics`:

1. The sim frame always advances by 2 tics (one vanilla frame at the 70 Hz PIT
   rate, 28.6 ms). `frameStart += 2/70 s` per sim frame is the accumulator.
2. Until wall time reaches `frameStart + 2/70 s`, it composes and presents frames
   at display rate with `alpha = (now - frameStart) / (2/70 s)`. Each frame
   interpolates the camera and every sprite from the previous sim frame to the
   current one.
3. Then it hands back to the sim for the next frame. If the sim is behind, the
   presents are skipped (alpha is already 1 or more) and the sim catches up.

In demos (playback, recording, `/DUMPFILE`) `RFL_CalcTics` still runs, so
`TimeCount` and the playloop dump stay identical to vanilla.

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
present,host_us,dt_us,sim,alpha
```

- `host_us` is the host time after the present call returned, in µs since the
  first present. In an `OMNI_DEBUG_INTERP_CAPTURE` run it is virtual time instead.
- `dt_us` is the gap to the previous present.
- `sim` is the sim frame shown: the frame is interpolated from `sim-1` to `sim`.
- `alpha` is the interpolation factor used. `grid` mode draws with alpha 1, but
  the log shows the clock's alpha.

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

## Renderers

The compositor produces a 1280x800 ARGB frame. Each backend only has to show it:

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
