# DOS toolchain

Everything runs headless through `oracle/tools/dosbuild.sh` (DOSBox 0.74, `SDL_VIDEODRIVER=dummy`,
workdir = C:, `~/bcc` = D:, ~2 s overhead per invocation, errorlevel propagated).

## Compilers (`~/bcc`, never committed)
| dir | what | status |
|---|---|---|
| `BC31` | Borland C++ 3.1 (BCC 3.1, TLINK 5.1, TASM, MAKE, PRJ2MAK 2.0) | present 2026-09-19 |
| `BC30` | Borland C++ 3.0 (BCC 3.00, TLINK 5.0, TASM 3.0, MAKE, PRJ2MAK) | present 2026-09-19 |
| `BC20` | Borland C++ 2.0 — only its `LIB` dir is used; RCK4/5/6 link against it | present 2026-09-19 |

Byte-identical Keen 4 v1.4 / Keen 5 v1.4 / Keen 6 v1.4 = **BC30 compiler + BC30 INCLUDE + BC20 LIB**
(upstream readme, "Note that RCK4 … are set up to compile with BC30, but using the Library directory
from BC20"). BC31 is for the GT builds and Keen 6 v1.5 only.

## Pipeline (proven 2026-09-19 with BC31, Keen 4)
1. stage: copy `dos/` to an 8.3-clean build dir, add `oracle/vendor/LZEXE.EXE`, `oracle/vendor/MAKEOBJ.EXE`
2. `oracle/tools/ripstatic.py 4 $KEEN_DATA/keen4/KEEN4E.EXE <build>/KEEN4-6/static` — native CKPatch replacement
3. `MAKEOBJ` (DOS, via dosbuild) → `KEEN4/CK4*.OBJ`
4. `PRJ2MAK RCK4.PRJ RCK4.MAK RCK4.CFG` — recovers flags **and link order** from the binary `.PRJ`.
   Delete the emitted `RCK4.CFG` if you edit `LIBPATH`/`INCLUDEPATH`, or MAKE keeps the stale one.
5. `MAKE -fRCK4.MAK` → `KEEN4/OBJ/RCK4.EXE` — **8 s** wall time, 0 errors, 6 warnings (upstream says 3-4 are normal)

Recovered settings: `bcc -mm -a -f- -v -G -O -Z -vi- -Fc -weas -wpre`, `tasm /MX /ZI /O`,
`tlink /s/i/c/P-`, libs `graphics.lib + cm.lib`, startup `c0m.obj`, medium model.
Link order: CK_MAIN, CK_DEMO, CK_GAME, CK_PLAY, CK_TEXT, CK_STATE, CK_KEEN, CK_KEEN2, K4_SPEC, K4_ACT1-3,
ID_CA, ID_IN, ID_MM, ID_RF, ID_RF_A, ID_SD, ID_US_1, ID_US_2, ID_US_A, ID_VW, ID_VW_A, then the six data OBJs.

## Result with the wrong compiler (BC31 instead of BC30+BC20 libs)
Builds and **runs** (boots `/TEDLEVEL 1` in DOSBox). Not identical, as expected: load image 253,088 vs
251,712 bytes (+1,376), relocations 2,929 vs 2,931. Good enough for oracle/harness/instrumented builds (E4);
not for M1.

## M1 result — byte-identical (2026-09-19)
`make verify-k4 verify-k5` (= `oracle/tools/verify-dos.sh`), from a clean staging dir, ~14 s each:

| episode | original | result |
|---|---|---|
| Keen 4 v1.4 EGA Apogee | 105,108 B, sha256 `bdbb46746e02c0dd…` | **PASS** — unpacked image, relocations, entry/stack, and LZEXE-packed file all identical |
| Keen 5 v1.4 EGA Apogee | 106,417 B, sha256 `77f687268be09763…` | **PASS** — same |
| Keen 6 v1.4 EGA FormGen | 107,719 B, sha256 `f1b19d231eb3ba84…` | **PASS** (2026-11-06, #55) — unpacked image (260,432 B, `75216fa922c2d2fe`), relocations, entry/stack and packed file all identical. Same BC30 + BC20 LIB + RCK6.PRJ, no deviations |

What it took beyond upstream's readme:
- **Use PRJ2MAK from BC30, not BC31.** The 3.1 converter misreads the 3.0 project (emits `-vi-` and a
  different warning set); the 3.0 one gives the real flags: `-mm -a -f- -v -G -O -Z -vi -Fc` + the project's `-w` set.
- `bcc` on the command line aborts after 100 warnings (`id_in.c` alone has 100 "mixing pointers to signed and
  unsigned char"); the IDE never did. We append `-g255 -w-ucp` — warnings only, no effect on generated code.
- The floppy `.CA1/.CA2` split archives are a zip cut into parts **with a 4-byte header on each part**: strip 4 bytes
  from every part, concatenate, unzip. The zips flatten `INCLUDE\SYS\*.H`; recreate `SYS\{STAT,TIMEB,TYPES,LOCKING}.H`.
- Relocation tables must be compared as linear addresses — LZEXE re-normalises the `seg:off` pairs.

## Keen 6 notes (#55)
- `RCK6.PRJ` links with `tlink /x/i/c/P-` (no map), unlike K4/K5 (`/s`). `verify-dos.sh` and `keendump.sh`
  rewrite `/x` to `/s` in the generated MAK so a `.MAP` exists; verified the code is still identical.
- `oracle/dos/KEEN4-6/KEEN6/OBJ/fix_RCK6_v15.pat` and `RCK6E15`/`RCK6C*` are v1.5 / other variants; not used for v1.4.
- `make keendump-k6` works unchanged: DGROUP check PASS (734 publics, 0 moved), KEENDUMP.EXE 273,552 B.
  Demo 0 trace = 1077 frames x 7694 B (sizeof(gamestate) = 90), matching oracle/docs/TRACE_FORMAT.md.
