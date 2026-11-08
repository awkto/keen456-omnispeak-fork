# Symbol tables (TLINK map to names)

`oracle/docs/symbols/k4.sym` and `k5.sym` give every PUBLIC function and global of the byte-identical
Keen 4 / Keen 5 rebuilds an address in the original binary. They contain only names and
addresses (no game data).

## Regenerate

    make symbols            # runs oracle/tools/verify-dos.sh N first if the map is missing

The map is already produced by the normal build: `RCK4.MAK`/`RCK5.MAK` link with
`tlink /s/i/c/P-` (`/s` = detailed segment map plus publics by name and by value). Nothing in the
build was changed for this; the EXE is the same byte-identical one `make verify` proves.
The map lands at `~/keen-build/k{4,5}/KEEN4-6/KEEN{4,5}/OBJ/RCK{4,5}.MAP`.
Symbols are not in the map's "Line numbers" form (no `/l`), so there is no source-line info.

## Format of `kN.sym`

TSV, `#` lines are comments, one header line. Columns:

| column | meaning |
|---|---|
| name | C name with the leading `_` removed (`_RF_Refresh` becomes `RF_Refresh`); RTL/asm names unchanged (`F_LXMUL@`, `__setargv__`) |
| seg | link-time frame number, hex, **relative to the load segment** |
| off | offset in that frame, hex |
| linear | `seg*16+off`, hex: byte offset into the **unpacked** EXE image (EXE minus header) |
| kind | `code` if the address lies in a CODE-class segment, else `data` |
| segment | TLINK segment name (`ID_RF_TEXT`, `_DATA`, `_BSS`, `FAR_DATA` ...) |

Example (Keen 4): `RF_Refresh 1654 1C6E 181AE code ID_RF_TEXT`, `s_keenstand 2EE7 098C 2F7FC data _DATA`.
Checked against the real image: byte 0x181AE is `55 8B EC` (push bp; mov bp,sp) and the
`statetype` at 0x2F7FC decodes as left 138 / right 130 sprite (KEENSTANDLSPR/RSPR).

Counts: Keen 4 1587 (732 code, 855 data); Keen 5 1507 (717 code, 790 data).

## Segment-relative addresses

The linker knows nothing about where DOS loads the program. At run time the real address is
`(load_segment + seg):off`, where `load_segment = PSP segment + 0x10`. TLINK's entry point is
`0000:0000`, so **CS at the first instruction after load is the load segment**. It varies with
DOS memory layout (and DOSBox config), so always read it from the running process.

* Code is medium model: far calls, so a code pointer is `seg:off` (use both).
* Data: all of `_DATA`/`_BSS`/`_COMDEF_` is DGROUP (frame `2EE7` in Keen 4, `3034` in Keen 5).
  A **near** pointer into it is just the `off` column: `objtype.state` is declared
  `statetype *state;` in `oracle/dos/KEEN4-6/CK_DEF.H`, a near pointer in the medium model, so a
  state pointer value `0x098C` read from the DOS process means `s_keenstand` (Keen 4). Resolve by
  matching `off` against symbols whose `seg` equals the DGROUP frame. Linear = `0x2EE70 + off`.
* To convert a captured runtime address back: subtract the load segment, then
  `oracle/tools/map2syms.py lookup oracle/docs/symbols/k4.sym SEG:OFF --load-seg HEX` (or pass `0xLINEAR`)
  returns `name+offset`.

## statetype coverage

`oracle/tools/map2syms.py states oracle/docs/symbols/k4.sym oracle/dos/KEEN4-6 oracle/dos/KEEN4-6/KEEN4` compares all
`statetype xxx = {...}` definitions in the source to the map. None of them is `static`, so all
are PUBLIC. Keen 4: 376 definitions in the sources, 370 in the map; Keen 5: 327 definitions,
312 in the map. Everything missing is compiled out by `#ifdef KEEN5` / `#ifdef KEEN4` /
`#ifndef KEEN5` for that episode (e.g. `s_keenride`, `s_worldelevate` are Keen 5 only;
`s_keenmoon1`, `s_throwflag0` are Keen 4 only). Nothing in the compiled game is missing.
Only static functions and variables would be missing from a TLINK map (none for states).

## DOSBox-X debugger

    oracle/tools/map2syms.py bp oracle/docs/symbols/k4.sym RF_Refresh --load-seg 0x1A2B
    # prints: BP 307F:1C6E   ; RF_Refresh (code)

The DOSBox-X debugger has no symbol support, so we convert names to `BP seg:off`.
**Demo not done:** the pinned build `~/git/keen456/native/vendor/dosbox-x-2026.08.02/` was
compiled without the heavy debugger (no `BPINT`/`BP` command strings in the binary; `DEBUGBOX`
only opens nothing under the dummy video driver). To use it you need a DOSBox-X built with
`./configure --enable-debug=heavy` (curses front end, run in a real/tmux terminal, not
redirected). Then: start the game with `DEBUGBOX KEEN4E.EXE`, note CS at the first
instruction (= load segment), run `map2syms.py bp ... --load-seg <CS>`, type the printed
`BP` line in the debugger, `run`. For KEENDUMP-style tooling, prefer reading memory directly
and resolving via `lookup` instead of the debugger.

## Ghidra

Unpack the EXE to its image (see `oracle/tools/unlzexe.py`, strip the MZ header: image =
`d[hdr_paras*16:size]`) and run:

    ~/opt/ghidra_11.3.2_PUBLIC/support/analyzeHeadless PROJ p -import IMAGE.bin \
      -processor "x86:LE:16:Real Mode" -loader BinaryLoader -loader-baseAddr 1000:0000 \
      -postScript ghidra_import_syms.py oracle/docs/symbols/k4.sym 1000 -scriptPath oracle/docs/symbols

The second argument is the load segment used for `-loader-baseAddr` (hex). The script
(`oracle/docs/symbols/ghidra_import_syms.py`) creates a label per symbol and a function at each code
symbol. Tested headless: 1587 labels, `RF_Refresh` lands at `2654:1c6e` (= 1000 + 1654).
