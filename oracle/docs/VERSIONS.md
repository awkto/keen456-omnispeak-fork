# Which builds we have, and which the reconstruction can reproduce

Identified 2026-09-19 with `oracle/tools/unlzexe.py` by testing each unpacked image against the
per-version data offsets in the reconstruction's `static/ripck{4,5,6}.pat` (a version matches
when `MAPHEAD` starts with the RLEW tag `CD AB` at the listed offset and the last `EGAHEAD`
entry equals the size of `EGAGRAPH.CK?`).

The reconstruction reproduces: Keen 4 v1.4 EGA (Apogee/FormGen, GT), Keen 5 v1.4 EGA (Apogee, GT),
Keen 6 v1.4 and v1.5 EGA (FormGen) — plus the CGA variants, which we ignore.

| file | packed bytes | packed sha256 (first 16) | load image | verdict |
|---|---|---|---|---|
| `~/keen-data/keen4/KEEN4E.EXE` | 102,730 | `676057cb70dfab7c` | 246,944 | **unsupported — older than v1.4** (v1.0–1.2). `EGAHEAD`/`MAPHEAD` sit ~4.3 KB lower than v1.4 (`MAPHEAD` at image+0x23780 vs 0x24830); `EGAGRAPH.CK4` is 487,332 bytes vs 520,581 |
| keen456 `games/keen4.jsdos` → `KEEN4E.EXE` | 105,108 | `bdbb46746e02c0dd` | 251,712 | **Keen 4 v1.4 EGA (Apogee)** — exact match on all offsets. Use this as the Keen 4 baseline |
| `~/keen-data/keen5/KEEN5.EXE` | 106,417 | `77f687268be09763` | 254,832 | **Keen 5 v1.4 EGA (Apogee)** — exact match → `RCK5.PRJ` |
| `~/keen-data/keen6/KEEN6C.EXE` | 104,938 | `050dadf0c1902fcf` | 254,768 | **unsupported — older than v1.4** (almost certainly v1.0). `MAPHEAD` at image+0x23C50 vs 0x25080 in v1.4; `EGAGRAPH.CK6` 457,309 bytes |
| `keen-all-backup.zip:keen6/KEEN6.EXE` | 104,938 | `d599f678649ba069` | — | the unpatched original of the above |
| `~/keen-data-decomp/keen6/KEEN6.EXE` (user upload 2026-11-06) | 107,719 | `f1b19d231eb3ba84` | 260,432 | **Keen 6 v1.4 EGA (FormGen)** — `MAPHEAD` tag at image+0x25080, last `EGAHEAD` = 464,726 = `EGAGRAPH.CK6` → `RCK6.PRJ` |

## KEEN6C.EXE
`KEEN6C.EXE` is the stock `KEEN6.EXE` with **two bytes changed in the packed file**
(so the patch lands inside the LZEXE stream, not in the stub):

```
0x4426: 9A -> EB
0x4427: 6A -> 16
```

It is an **EGA** build ("C" = cracked, not CGA). Once we have a v1.4/v1.5 `KEEN6.EXE`, the
copy-protection skip becomes a source-level `#ifdef` and this patch is irrelevant.

## Consequences
- **Keen 4:** nothing to obtain. Keen 4 *is* the shareware episode, so the v1.4 shareware EXE is
  the complete game; all tools default to it. The older copy in `~/keen-data/keen4` should be
  replaced by the v1.4 files (data files differ too — Omnispeak rejects the old `EGAGRAPH`).
- **Keen 5:** ready.
- **Keen 6:** v1.4 EGA obtained 2026-11-06 (`~/keen-data-decomp/keen6`); #55/#64 unblocked.
