# Per-frame state trace format (KEENDUMP / Omnispeak `/DUMPFILE`)

The oracle trace written by the instrumented DOS build (KEENDUMP, tracker #57) and by the
Omnispeak fork's playloop dumper (#93). **Both sides write the same bytes**: we did not invent a
format. We adopted the one Omnispeak already writes in debug builds and made the DOS build write
it too. Two dumps can be compared directly, and they can be byte-identical: Keen 4 demo 0 and
Keen 5 demo 0 are, see [Evidence](#evidence-2026-11-01).

**Version:** format 1 = Omnispeak's dumper as of engine `main` `dda98e5` (`src/ck_play.c:2285-2296`,
`CK_SaveGameState`/`CK_SaveObject` in `src/ck_game.c:94-158, 231-266`). The file has no header
and no version field, so the version is fixed by the writers. If either side ever changes the
layout, give it a new name and section here (format 2). Never change format 1 silently.

## Producing a trace

| side | build | command line |
|---|---|---|
| DOS | `make keendump-k4` / `keendump-k5` (= `oracle/tools/keendump.sh build 4\|5`) → `$KEEN_BUILD/k<N>-dump/KEENDUMP.EXE` (unpacked; default `KEEN_BUILD=~/keen-build`) | `KEENDUMP.EXE /NOWAIT /PLAYDEMO <n> /DUMPFILE <file>` |
| Omnispeak | `make -C src DEBUG=1` (defines `CK_DEBUG`, which turns on `CK_ENABLE_PLAYLOOP_DUMPER`, `src/ck_config.h:70`) | `omnispeak /EPISODE <4\|5> /PLAYDEMO <n> /DUMPFILE <file>` |

One-shot DOS run (headless DOSBox through `oracle/tools/dosbuild.sh`; throwaway copy of the data; the
game quits by itself when the demo ends, ~37 s for a 812-frame demo):

    make verify-k4 && make keendump-k4             # the normal map is needed for the DGROUP check
    oracle/tools/keendump.sh run 4 0 out/k4-d0.trc         # episode, demo number, output file

The same EXE runs under the pinned DOSBox-X harness too. `oracle/harness/dosrun.sh` has no
"wait for exit" mode, so give it a key script that only sleeps:
`oracle/harness/dosrun.sh 4 --exe .../KEENDUMP.EXE --args "/NOWAIT /PLAYDEMO 0 /DUMPFILE KD.TRC" --keys sleep50.txt --wait 3 --timeout 120 --keep`,
and take `KD.TRC` from the kept `game/` dir.

**Driving both sides the same way.** `/PLAYDEMO n` exists in Omnispeak (`src/ck_main.c:787-793`:
`CK_PlayDemo(n)` then `Quit(0)`), so the KEENDUMP build gets an option with the same name. It runs
`RunDemo(n)` (CK_DEMO.C:1978) once, right after `InitGame`, and then `Quit(NULL)`. That is the same
function the attract loop calls, minus the title/StarWars screens before it. Neither path depends
on that history: `RunDemo` → `NewGame`, and `PlayLoop` reseeds the RNG to 0 in demo mode. The DOS
side needs `/NOWAIT`, or the start-up text screen waits for a key. `/NOWAIT` changes nothing
during demo playback, because it only enables debug keys and `CheckKeys` is not called during
playback. `/DUMPFILE` without `/PLAYDEMO` traces every `PlayLoop` iteration of whatever is played
(attract demos, a game) on both sides.

Option names are matched as whole words, case-insensitive. Put the demo number before another
option, not last: the stock `US_CheckParm` scans past a non-alphabetic argument such as `0` into
the following memory (harmless, existing behaviour, but better to avoid).

**File naming.** The DOS side writes inside the emulated C: drive, so use an 8.3 name there
(`KD.TRC`). Store traces as `k<ep>-d<demo>.trc` (e.g. `k4-d0.trc`), with `-omni` for the fork's
(`k4-d0-omni.trc`). Traces are data-derived: **never commit Keen 5 traces**. Keen 4 shareware
golden traces may go under `tests/dos-golden/` later (#58), not before.

## Framing

The file is `N` records back to back, no header, no separators, little-endian throughout.
`N` = number of `PlayLoop` iterations traced. For a demo run it equals the demo's frame count
(`oracle/docs/DEMOS.md`: K4 demo 0 = 812, K5 demo 0 = 842), unless Keen dies first. **Record index =
frame number** (0-based, since level start).

| episode | `sizeof(gamestate)` | record size | per 812 frames |
|---|---:|---:|---:|
| Keen 4 | 86 | 4 + 86 + 100×76 = **7690** | 6.2 MB |
| Keen 5 | 88 | **7692** | |
| Keen 6 | 90 | 7694 | (DOS build verified: demo 0 = 1077 frames) |

A reader must be told the episode (`tracecmp.py -e`). The record sizes are too close to infer it
from the file length.

## Sampling point

Once per `PlayLoop` iteration, **after the think/move pass and before `HandleRiding`**:
DOS `CK_PLAY.C:2248` (`KD_Frame()`), Omnispeak `ck_play.c:2285`. At that point `PollControls`
and every object's `StateMachine` have run for this frame. Contacts, in-tiles, react, camera and
`RF_Refresh` have not. (docs/ACTORS.md §7 recommends a second point at the top of the loop. That
would be format 2.)

## Record layout

### Header word (offset 0)

| off | size | DOS | Omnispeak | compared |
|---:|---:|---|---|---|
| 0 | u32 | `TimeCount` (read with interrupts off) | `SD_GetTimeCount()` | **no**, wall-clock |

In demo mode, `RF_CalcTics` sets `TimeCount = lasttimecount + 3` at the end of every frame. At
the dump point it is therefore `6 + 3·frame` (`3` for frame 0) **plus the 70 Hz ticks that
elapsed during this frame's input and think pass**. That excess depends on emulator speed.
`tracecmp.py info` prints its range. It was 0 at 25 000 cycles and up to 5 at 1 500 cycles.

### `gamestate` (offset 4) = DOS `gametype` (CK_DEF.H:361-382) = Omnispeak `CK_SaveGameState`

All fields are 16-bit except score/nextextra. There is no padding: every member is a word, and
enums and `boolean` are 16-bit ints with BC++ `-a`.

| rec off | gs off | type | DOS | Omnispeak | notes |
|---:|---:|---|---|---|---|
| 4 | 0 | u16 | `worldx` | `mapPosX` | |
| 6 | 2 | u16 | `worldy` | `mapPosY` | |
| 8 | 4 | u16[25] | `leveldone[GAMELEVELS]` | `levelsDone[25]` | |
| 58 | 54 | i32 | `score` | `keenScore` | |
| 62 | 58 | i32 | `nextextra` | `nextKeenAt` | |
| 66 | 62 | i16 | `ammo` | `numShots` | |
| 68 | 64 | i16 | `drops` | `numCentilife` | |
| 70 | 66 | K4: i16 ×2 | `wetsuit`, `rescued` | `ep.ck4.wetsuit`, `membersRescued` | |
| 70 | 66 | K5: i16 ×3 | `keycard`, `destroyed`, `numfuses` | `ep.ck5.securityCard`, `word_4729C`, `fusesRemaining` | |
| 74 / 76 | 70 / 72 | i16[4] | `keys[4]` | `keyGems[4]` | K4 / K5 offsets |
| 82 / 84 | 78 / 80 | i16 | `mapon` | `currentLevel` | |
| 84 / 86 | 80 / 82 | i16 | `lives` | `numLives` | |
| 86 / 88 | 82 / 84 | i16 | `difficulty` | `difficulty` (enum → int16) | demo: `gd_Normal` = 2 |
| 88 / 90 | 84 / 86 | u16 | `riding` (near ptr) | `CK_ConvertObjPointerTo16BitOffset(ck_keenState.platform)` | object pointer, see below |

The whole block is compared.

### Objects (offset 4 + sizeof(gamestate)): `objarray[100]`, slot order, 76 bytes each

All 100 slots in array order (not list order), free ones included. DOS writes `objarray` raw;
Omnispeak writes `CK_SaveObject` for each `ck_objArray[i]`.

| obj off | DOS `objtype` | Omnispeak `CK_object` | representation | compared |
|---:|---|---|---|---|
| 0 | `obclass` | `type` | enum value, same numbering | yes |
| 2 | `active` | `active` | `ac_no/yes/allways/removable` = 0..3 | yes |
| 4 | `needtoreact` | `visible` | 0/1 | yes |
| 6 | `needtoclip` | `clipped` | 0..2 | yes |
| 8 | `nothink` | `timeUntillThink` | | yes |
| 10, 12 | `x, y` | `posX, posY` | u16 global units | yes |
| 14, 16 | `xdir, ydir` | `xDirection, yDirection` | | yes |
| 18, 20 | `xmove, ymove` | `deltaPosX, deltaPosY` | | yes |
| 22, 24 | `xspeed, yspeed` | `velX, velY` | | yes |
| 26 | `ticcount` | `actionTimer` | | yes |
| 28 | `state` (near ptr) | `currentAction->compatDosPointer` (0 if NULL) | **DS offset of the statetype**; names via `oracle/docs/statemap-k?.tsv` | yes |
| 30 | `shapenum` | `gfxChunk` | sprite chunk number | yes |
| 32 | `priority` | `zLayer` | | yes |
| 34-42 | `left, top, right, bottom, midx` | `clipRects.unitX1 … unitXmid` | | yes |
| 44-52 | `tileleft … tilemidx` | `clipRects.tileX1 … tileXmid` | | yes |
| 54-60 | `hitnorth, hiteast, hitsouth, hitwest` | `topTI, rightTI, bottomTI, leftTI` | | yes |
| 62-68 | `temp1..temp4` | `user1..user4` | raw 16-bit; may hold a DS pointer (object, or sprite for stunned stars `temp3`) stored as a DOS offset on both sides | yes (see note) |
| 70 | `sprite` (near ptr) | `RF_ConvertSpriteArrayPtrTo16BitOffset(sde)` | `spritearray + 32·index`, 0 = none | **no** (refresh-manager allocation, presentation) |
| 72 | `next` (near ptr) | `CK_ConvertObjPointerTo16BitOffset(next)` | object pointer | yes, as slot |
| 74 | `prev` (near ptr) | same for `prev` | object pointer | yes, as slot |

**Near pointers.** The DOS build is medium model, so all data pointers are 16-bit DGROUP offsets.
Omnispeak converts its native pointers to the same numbers using constants from
`EPISODE.CK?`:

| | Keen 4 | Keen 5 | Omnispeak variable |
|---|---|---|---|
| `objarray` (object *p* = `objarray + 76·slot`) | `0xA807` | `0x9E6F` | `ck_exe_objArrayOffset` |
| `dummyobj` (`GetNewObj(true)` when full; never linked) | `0xC5D3` | `0xBC3B` | `ck_exe_tempObjOffset` |
| `spritearray` | `0xD622` | `0xCD50` | `ck_exe_spriteArrayOffset` |
| states | `0x098C` = `s_keenstand` … | `0x0888` = `s_keenstand` … | `%action … <dosofs>` in `ACTION.CK?` |

These are the original v1.4 addresses. Our normal rebuild is byte-identical, and the KEENDUMP
build is checked to keep every DGROUP address (below), so all three binaries produce the same
numbers. `NULL` is 0. Omnispeak's non-debug `CK_SaveObject` (saved games) writes dummy
`sde/next/prev` values: only the dumper build writes the real ones.

**Comparison rules** (what `oracle/tools/tracecmp.py cmp` does; full tracediff with names is #59):

- `TimeCount`: never compared.
- `gamestate`: every field.
- **Live objects only**: the list from slot 0 (`player`) along `next`. The live-slot list itself
  must match (a spawn or remove that differs shows up here). Each live object: every field except
  `sprite`; `next`/`prev` translated to slot numbers.
- **Free slots are not compared.** They hold stale contents from their last use, and on DOS the
  free list is threaded through `prev` (`InitObjArray`). They matched byte for byte in the runs
  below anyway.
- `temp1..4` holding sprite pointers (stunned stars, `temp3`) are compared, because both sides
  store DOS offsets and they matched. If they diverge only there, treat it as presentation
  (docs/ACTORS.md §7).
- Not in format 1: `tics`, `lasttimecount`, `rndindex`, the scroll origin, `playstate`, Keen's
  input/jump globals. A desync shows up in object fields within a frame or two anyway. Add these
  in a format 2 if diagnosing needs them (docs/ACTORS.md §7).

## Determinism: the dump cannot perturb the game

Demo playback runs on fixed tics: `RF_CalcTics` in demo mode (ID_RF.C:1605-1616) busy-waits
until `TimeCount ≥ lasttimecount + 6`, then sets `lasttimecount += 3`, `TimeCount = lasttimecount
+ 3`, `tics = 3`. Time spent writing the trace, or a slow emulator, only stretches the wait or
overruns it. Overruns are discarded, so `tics`, `lasttimecount` and therefore all game state are
the same. Only the raw `TimeCount` at the dump point carries wall-clock noise, which is why it is
excluded. **This holds for demo playback only.** In live play `tics = TimeCount -
lasttimecount`, so the extra write time per frame can change `tics` and make the run different
from an untraced one.

Other traps for format users: K5 sequences that spin on `TimeCount` and world-map
teleport/elevator animation read `TimeCount` (docs/ACTORS.md §6). They are not hit by the
built-in demos (K4 demo 0 is level 1, K5 demo 0 level 1).

## KEENDUMP implementation (dos/)

Everything is behind `#ifdef KEENDUMP` (off by default):

- `oracle/dos/KEEN4-6/CK_DUMP.C`: the writer and the option parser. It is `#include`d at the end of
  `CK_PLAY.C`, so no project or link list changes.
- `CK_PLAY.C:2248`: `KD_Frame()` after the think pass. `CK_MAIN.C:529`: `KD_Main()` after
  `InitGame`. `CK_DEF.H:478`: prototypes.
- The writer uses `CA_FarWrite` and `open`/`close`, which are already linked, so no new RTL
  modules come in. It writes 3 DOS writes per frame and truncates the file on close.
- The build adds `-DKEENDUMP` to the generated `RCK?.MAK` config, in its own staging dir
  `k<N>-dump`. `make verify-k4 verify-k5` is untouched and still byte-identical.

**The DGROUP rule.** The trace records near pointers (`state`, `next/prev`, `riding`, `sprite`),
so KEENDUMP must not move anything in DGROUP. `objarray` and friends are communal (`_COMDEF_`)
variables placed *after* all `_DATA` and `_BSS`. A single string literal or near global anywhere
would shift them all. Therefore `CK_DUMP.C` has **no near globals/statics and no string
literals**: persistent data is `far` (its own `CK_PLAY<n>_DATA` FAR_DATA segments), and error text
is copied to the stack before `Quit`. `oracle/tools/keendump.sh build` compares the KEENDUMP `.MAP`
against the normal build's and fails unless every DGROUP public has the same offset, no DGROUP
public is added, and all DGROUP segment lengths (`_DATA`, `_BSS`, `_COMDEF_` …) are equal. Only
the DGROUP *frame* (segment) moves, because the code grew. That is irrelevant for near pointers.
Far code pointers inside the state tables do change, but they are not traced.

## Evidence (2026-11-01)

- `make verify-k4 verify-k5` with the KEENDUMP changes in `dos/`: both PASS, packed EXEs
  byte-identical (K4 105,108 B `bdbb4674…`, K5 106,417 B `77f68726…`).
- DGROUP check: K4 775/775 DGROUP publics (368 `s_*` states) moved 0, added 0, segment lengths
  equal. K5 720/720 (310 `s_*`), moved 0, added 0. `objarray` A807/9E6F, `spritearray`
  D622/CD50 unchanged.
- K4 demo 0: 6,244,280 B = 812 records. Two DOSBox 0.74 runs and one DOSBox-X 2026.08.02 run
  (normal core, 25 000 cycles) were byte-identical (sha256 `a0f328cb…`). A DOSBox 0.74 run at
  `cycles=fixed 1500` (frames overran by up to 5 tics) differed only in `TimeCount` of 4 records,
  and `tracecmp cmp` reported EQUAL.
- K5 demo 0: 6,476,664 B = 842 records. Two runs byte-identical (`4ddd8598…`).
- Omnispeak fork `main` `dda98e5`, `DEBUG=1`, `/PLAYDEMO 0 /DUMPFILE`: **byte-identical** to the
  DOS traces for K4 demo 0 and K5 demo 0, including `TimeCount`, `sprite` and free slots.

## tracediff: first divergence, human-readable (#59)

    oracle/tools/tracediff.py -e 4 A.trc B.trc [--context N] [--summary] [--all-fields]

Uses the comparison rules above (the same parser as `tracecmp.py`). It finds the first frame
where any compared field differs and prints the frame number, then every differing field of that
frame (up to 12): `gamestate.<field>`, a differing live-object list (spawn/remove), or
`slot S [class] .field: A=… B=…`. Object classes come from the `classtype` enum of `CK_DEF.H`
(embedded; a unit test checks it against the header), and `state` values show the DOS and
Omnispeak names from `oracle/docs/statemap-k<N>.tsv` (`0x098C (s_keenstand / CK_ACT_keenStanding)`).
`state` is listed first within an object. It then prints `--context N` frames (default 8) before
the divergence, for both traces, with the key fields of the first differing object (class, x, y,
xspeed, yspeed, xdir, ydir, state, ticcount; `*` marks values that differ). A frame-count
mismatch is reported too, and counts as divergent even when the common frames are equal.

- `--all-fields` also compares `TimeCount`, `sprite`, raw `next`/`prev`, and free slots.
- `--summary` prints one line (`EQUAL <n> frames` or `DIVERGE frame … slot … .field: A=… B=…`)
  and exits 0 equal, 1 divergent, 2 usage/parse error (bad file size, unreadable file). The
  full report uses the same exit codes.

Tests: `python3 oracle/tools/test_tracediff.py` (synthetic traces only).
