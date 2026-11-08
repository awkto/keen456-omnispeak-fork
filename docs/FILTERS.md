# Screen filters, pixels and aspect (#101)

Ported from the old DOSBox-X web shell. Available in the `sdl2`, `sdl2sw` and `sdl2gl`
builds (native and WebAssembly), with smoothing on or off.

| Option | Config key | Flag | Values |
| --- | --- | --- | --- |
| Filter | `filter` | `/FILTER x` | `off` (default), `scanlines`, `crt` (scanlines + RGB mask + vignette), `curved` (barrel warp + the CRT look), `rgb` (aperture mask), `soft` (blur + light scanlines), `amber`, `green` |
| Pixels | `pixels` | `/PIXELS x` | `crisp` (default), `light`, `low`, `medium`, `high` (blur amount, like the old Smooth setting) |
| Aspect | `aspectMode` | `/ASPECT x` | `auto` (default: the existing "correct aspect ratio" switch), `4:3`, `as-is` (square pixels, whole-number scale), `16:10`, `16:9`, `fit` |

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

## Testing

`OMNI_DEBUG_FILTER_SHOT=<file.ppm>` (with `OMNI_DEBUG_FILTER_SHOT_FRAME=n`) writes the n-th
filtered frame as a PPM and exits; it is a test hook only. `OMNI_DEBUG_INTERP_STATS=1` adds the
per-frame filter cost to the stats line.

In the browser an explicit aspect mode makes the canvas fill the window and the engine
letterboxes inside it. The launcher accepts `?filter=crt&pixels=low&aspect=16:9` to preselect.
