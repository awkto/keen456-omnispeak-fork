# Provenance of `dos/`

Pristine import of **"Reconstructed Commander Keen 4-6 Source Code"**, Copyright (C) 2021 K1n9_Duk3, GPL-2.0-or-later.

- Source: <http://k1n9duk3.shikadi.net/files/modding/keensource456.zip> (announced <https://pckf.com/viewtopic.php?t=11505>)
- Downloaded: 2026-09-19 · newest file inside: 2026-01-04 · 131 files
- sha256 of the zip: `c9467959050d77d007aff52dd03dcee6b19962d29169ae3a66b0b44037df69a9`
- The GitHub mirror `sparky4/keen4-6` carries an older (2023) revision; do not use it.

Not tracked (third-party DOS binaries, fetched into `vendor/` instead): `LZEXE.EXE` (Fabrice Bellard's
LZEXE 0.91), `KEEN4-6/static/MAKEOBJ.EXE` (its source, `makeobj.c`, is tracked).

Every tracked file is byte-identical to upstream (`.gitattributes`: `dos/** -text`). Any later change to
`dos/` must be a separate, explained commit and must keep `make verify` passing.

## Local changes

- **KEENDUMP** (#57): `KEEN4-6/CK_DUMP.C` (new), hooks in `CK_PLAY.C`, `CK_MAIN.C`, `CK_DEF.H`, all
  inside `#ifdef KEENDUMP` (off by default; the normal build stays byte-identical). Trace writer
  and `/DUMPFILE`, `/PLAYDEMO` options, see `docs/TRACE_FORMAT.md`.
