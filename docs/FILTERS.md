# Screen filters, pixels and aspect (#101)

Ported from the old DOSBox-X web shell. Available in the `sdl2`, `sdl2sw` and `sdl2gl`
builds (native and WebAssembly), with smoothing on or off.

| Option | Config key | Flag | Values |
| --- | --- | --- | --- |
| Filter | `filter` | `/FILTER x` | `off` (default), `scanlines`, `crt` (scanlines + RGB mask + vignette), `curved` (barrel warp + the CRT look), `rgb` (aperture mask), `soft` (blur + light scanlines), `amber`, `green` |
| Pixels | `pixels` | `/PIXELS x` | `crisp` (default), `light`, `low`, `medium`, `high` (blur amount, like the old Smooth setting) |
| Aspect | `aspectMode` | `/ASPECT x` | `auto` (default: the existing "correct aspect ratio" switch), `4:3`, `as-is` (square pixels, whole-number scale), `16:10`, `16:9`, `fit` (widescreen, see below) |

All three are in the Options menu (FILTER, ASPECT, PIXELS; each press cycles) and on the
WASM launcher page (`web/index.html`).

## How it works

`src/id_vl_filter.c` runs on the CPU over the final 4x ARGB frame (1280x800) just before the
backend uploads it, so one implementation serves SDL2, SDL2-GL and WebGL. The effects are laid
out on the 320x200 game grid (scanline pitch = game row, RGB mask period = 3 game pixels,
vignette over the whole frame), with the old shader's parameters. Per-pixel multipliers
(scanlines x mask x vignette, and the curved sampling map) are precomputed once per
filter/size; the per-frame work is one multiply pass. Blur (soft, pixels) runs at half
resolution. With smoothing off and a filter on, the 320x200 frame is expanded 4x first.

`off` + `crisp` returns the input untouched: output is pixel-identical to before. Frame dumps
(`OMNI_DEBUG_FRAMES`) are taken from the composed 8-bit buffer and never see the filters.

Not ported exactly: the old `soft` filter's CSS `saturate(1.06)` is dropped; amber/green are
fixed tints approximating the old CSS hue-rotate chains.

## Widescreen (#103)

`16:10`, `16:9` and `fit` are real widescreen: the game shows more of the level instead of
stretching the 320x200 picture. The view is 200 lines of EGA pixels that are 1.2 times taller
than wide (as on a 4:3 monitor), so its width is the aspect times 240, rounded to a multiple of 4:

| Aspect | View | Notes |
| --- | --- | --- |
| `16:10` | 384x200 | |
| `16:9` | 428x200 | 0.3% narrower than an exact 16:9 pixel aspect; the region itself is exactly 16:9 |
| `fit` | window shape, 320..560 wide | as much as the window allows; letterboxed below 4:3 and beyond 560 (about 21:9), never stretched |
| `auto`, `4:3`, `as-is` | 320x200 | unchanged |

Whole-number scaling is not applied in the widescreen modes (it would break the region's aspect).
On native builds a new window opens at the mode's aspect.

How it works:

- The camera and the refresh manager (`id_rf.c`) are untouched: still the vanilla 320-px port.
  `rf_interp.c` composes the wider frame straight from the map planes and sprite table, centred
  on the camera window (`RF_WideViewLeft`). At the map edges and at scroll blocks the view stops,
  so it never shows anything the vanilla camera could not scroll to; when the space between two
  blocks (or a whole map) is narrower than the view, it is centred and the rest is black.
- The band is always taken from the sim camera (`rf_scrollXUnit`), and the displayed camera
  (lerped, or overshooting with `camera-smoothing`) is only clamped into it, horizontally and
  vertically (`RF_ClampViewTop`). An earlier build took the band from the displayed camera: at a
  map edge the smoothed camera overshot the stop by a pixel, the border block dropped out of the
  band and the view jumped 54 px into the border for about 19 sim frames (reported on K5 level 1).
- Animated tiles outside RF's 21x14 port take their frame from the animation timer
  (`RF_DisplayTileAt`), so the sides animate too.
- The scorebox sits in the top-left corner of the wide view.
- Anything drawn straight onto the 320-wide screen during play (pause and other dialogs, the
  Enter-key status window, palette fades, and every frame when smoothing is off) goes through
  `RFI_PresentWideStill`: the wide frame is composed on the vanilla 2-px grid and the vanilla
  screen is laid over its centre, where it matches pixel for pixel. After the play loop ends
  (death, level exit, the loading box) the last wide frame is kept under those overlays.
- Menus, help, high scores and title/text screens, demos and the vanilla profile stay
  320-wide, pillarboxed (centred, same scale) inside the wide region.
- Needs the `widescreen` fix (on in the `enhanced` profile; `/FIX widescreen` otherwise; the
  web launcher adds it for the widescreen modes). It affects the simulation: actors wake up and
  keep acting across the whole visible view, not only the centre 320 px (see
  [ACTORS.md](ACTORS.md) §3.1). Like every sim fix it is forced off in demo playback and
  recording and with `/DUMPFILE`, so those show the 320-wide view.
- Filters lay their scanlines and mask out on the wider game grid the same way.

## Testing

`OMNI_DEBUG_FILTER_SHOT=<file.ppm>` (with `OMNI_DEBUG_FILTER_SHOT_FRAME=n`) writes the n-th
frame handed to the filter stage (filtered or not, at its own width: 1712x800 for a 16:9 game
frame) as a PPM and exits; it is a test hook only. Frames that go the plain 320x200 path with
filters off are not counted. The web launcher accepts `?args=/TEDLEVEL%201%20/NOWAIT` (extra
command-line arguments) for tests. `OMNI_DEBUG_INTERP_STATS=1` adds the
per-frame filter cost to the stats line.

In the browser an explicit aspect mode makes the canvas fill the window and the engine
letterboxes inside it. The launcher accepts `?filter=crt&pixels=low&aspect=16:9` to preselect.
