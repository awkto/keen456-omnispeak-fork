# Actors: the state machine, the object list and the per-frame pipeline

*Tracker #18. Keen 4 and Keen 5 (v1.4); Keen 6 only where shared code branches for it.*
Written for an agent who will port, trace or modify this code without reading all of it. Every
claim cites `oracle/dos/KEEN4-6/<file>:<line>` (shortened here to `<file>:<line>`) and, where the fork
has an equivalent, Omnispeak `src/<file>:<line>` (this repo (formerly the `engine/` submodule), branch `main`, commit
`46cefb3`). This doc is the basis for the trace format of KEENDUMP (#57) and the fork's trace
writer (#93), see [Trace format](#7-trace-format-recommended-fields).

## 1. Data structures

### `statetype` (CK_DEF.H:323-336). Omnispeak: `CK_action` (ck_def.h:306-330)

One `statetype` is one animation frame/behaviour step. All states are initialised globals named
`s_*` (plus `sc_deadstate` and `sc_badstate`, CK_STATE.C:1988-1990), declared one per line, for
example `statetype s_keenstand = {KEENSTANDLSPR, KEENSTANDRSPR, stepthink, false, true, 4, 0, 32,
KeenPauseThink, KeenContact, KeenStandReact, &s_keenstand};` (CK_KEEN.C:61). Shared files pick
episode-specific states with `#ifdef KEEN4/5/6` (for example `s_keenride` is `#ifndef KEEN4`,
CK_KEEN.C:63-65).

| byte | DOS field | Omnispeak field | meaning |
|---:|---|---|---|
| 0 | `leftshapenum` | `chunkLeft` | sprite chunk when `xdir <= 0`; `0` = keep the current shape, `-1` = invisible |
| 2 | `rightshapenum` | `chunkRight` | sprite chunk when `xdir > 0`; if 0, the state has no shape and the think sets `shapenum` |
| 4 | `progress` | `type` | `step`/`slide`/`think`/`stepthink`/`slidethink` = 0..4 = `UnscaledOnce`/`ScaledOnce`/`Frame`/`UnscaledFrame`/`ScaledFrame` |
| 6 | `skippable` | `protectAnimation` | see §3.2; the names sound opposite, but the semantics are identical |
| 8 | `pushtofloor` | `stickToGround` | keep the object glued to 45° slopes (§4) |
| 10 | `tictime` | `timer` | duration in 70 Hz tics; `0` = never advances |
| 12 | `xmove` | `velX` | global units per state (step) or per tic (slide) |
| 14 | `ymove` | `velY` | same, vertical |
| 16 | `think` (far code ptr) | `think` | |
| 20 | `contact` (far) | `collide` | called on overlap with another active object |
| 24 | `react` (far) | `draw` | called if `needtoreact`; usually places the sprite |
| 28 | `nextstate` (**near** data ptr) | `next` | |

`sizeof(statetype)` is 30: the DOS build is **medium model** (far code, near data). Omnispeak's
action validator reads exactly this layout out of a real EXE's data segment (ck_main.c:454-479:
`+16/+20/+24` as `uint32`, `+28` as `uint16`), and consecutive state offsets in its action files
differ by `0x1E`.

### `objtype` (CK_DEF.H:338-359). Omnispeak: `CK_object` (ck_def.h:347-393)

76 bytes, 38 words. The DOS field order is the save-game order, and Omnispeak's `CK_SaveObject`
(ck_game.c:94-163) writes the same 38 words in the same order.

| byte | DOS | Omnispeak | notes |
|---:|---|---|---|
| 0 | `obclass` | `type` | `classtype` enum, CK_DEF.H:225-321 |
| 2 | `active` | `active` | `ac_no/ac_yes/ac_allways/ac_removable` = `OBJ_INACTIVE/ACTIVE/ALWAYS_ACTIVE/EXISTS_ONLY_ONSCREEN` (0..3) |
| 4 | `needtoreact` | `visible` (bool) | written as a 0/1 word |
| 6 | `needtoclip` | `clipped` | `cl_noclip/cl_midclip/cl_fullclip` = `CLIP_not/normal/simple` |
| 8 | `nothink` (Uint16) | `timeUntillThink` | think calls to skip; counts **DoActor calls**, not tics |
| 10, 12 | `x`, `y` (Uint16) | `posX`, `posY` | global units: 16 per pixel, 256 per tile (ID_RF.H:66-67) |
| 14, 16 | `xdir`, `ydir` | `xDirection`, `yDirection` | -1/0/1 |
| 18, 20 | `xmove`, `ymove` | `deltaPosX/Y` | **actual** displacement this frame, after clipping (CK_STATE.C:606-607) |
| 22, 24 | `xspeed`, `yspeed` | `velX`, `velY` | global units per tic |
| 26 | `ticcount` | `actionTimer` | tics spent in the current state |
| 28 | `state` (near ptr) | `currentAction` | **state identity, see §2** |
| 30 | `shapenum` | `gfxChunk` | sprite chunk number (same numbering on both sides) |
| 32 | `priority` | `zLayer` | |
| 34-42 | `left, top, right, bottom, midx` | `clipRects.unitX1, unitY1, unitX2, unitY2, unitXmid` | hit box in global units |
| 44-52 | `tileleft ... tilemidx` | `clipRects.tileX1 ... tileXmid` | same in tiles |
| 54-60 | `hitnorth, hiteast, hitsouth, hitwest` | `topTI, rightTI, bottomTI, leftTI` | wall info of the tile hit this frame; `PLATFORMEDGE` = `0x19` (CK_DEF.H:79) |
| 62-68 | `temp1..temp4` | `user1..user4` (int16) | per-class scratch; may hold an object or sprite pointer as a 16-bit DS offset |
| 70 | `sprite` (near ptr) | `sde` | refresh-manager sprite entry |
| 72, 74 | `next`, `prev` | `next`, `prev` | |

### The object list (CK_PLAY.C:1763-1891). Omnispeak: ck_play.c:392-472

`objarray[MAXACTORS=100]` (CK_PLAY.C:73, CK_DEF.H:39). `InitObjArray` threads all slots into a
free list through `prev`, then allocates slot 0 = `player` and slot 1 = `scoreobj`
(CK_PLAY.C:1763-1791). `GetNewObj` pops the **head** of the free list, `memset`s it to 0, appends it
to the **tail** of the live list, and sets `active=ac_yes`, `needtoclip=cl_midclip`
(CK_PLAY.C:1807-1836). `RemoveObj` unlinks and pushes the slot back on the free-list head
(CK_PLAY.C:1848-1882): slots are reused LIFO. If the array is full, `GetNewObj(true)` hands out
`dummyobj`, which is never linked.

Consequences for tracing:

- **Object identity = slot index** (`(ob - objarray)`, or the raw DS offset, which is
  `objarray_ofs + 76*slot`). Omnispeak implements the identical allocator, so the same
  spawn/remove sequence gives the same slots on both sides. It already converts object pointers
  to the DOS offset (`CK_ConvertObjPointerTo16BitOffset`, ck_play.c:476-483, with
  `ck_exe_objArrayOffset` = `0xA807` (K4) / `0x9E6F` (K5) from `EPISODE.CK?`).
- **List order = allocation order**, not slot order. All four per-frame passes walk from
  `player` along `next`, so the order is behaviourally significant: contact pairs, RNG consumption
  order and `continue`-after-`RemoveObj` all depend on it.
- `objectcount` is incremented by `GetNewObj` but **never decremented** in DOS
  (CK_PLAY.C:1834 and no decrement in `RemoveObj`); Omnispeak decrements it (ck_play.c:471). It
  only feeds debug output, so do not compare it.
- Freed slots keep stale contents until reused, on both sides. Compare live objects only.

## 2. State identity: DOS names and Omnispeak names

**DOS:** `objtype.state` is a 16-bit near pointer, i.e. the state's offset in DGROUP. Code
identifies states by address (`ob->state == &s_keenlookup2`, CK_PLAY.C:1569).

**Omnispeak:** states are `%action` lines in `data/keen4/ACTION.CK4` and `data/keen5/ACTION.CK5`,
parsed at startup by `CK_VAR_ParseAction` (ck_act.c:488-560) into `ck_actionData[]`, and looked
up by name (`CK_GetActionByName`, ck_act.c:264). Format:

    %action <name> <dosofs> <left> <right> <type> <protect> <stick> <timer> <velX> <velY> <think> <collide> <draw> <next>

The second field is `compatDosPointer` (ck_def.h:325-329): **the DOS near-pointer value of the
same state** in the original v1.4 EXE. Omnispeak uses it for DOS-compatible saved games
(ck_game.c:101, `CK_LookupActionFrom16BitOffset` ck_act.c:316) and for its validator
(`CK_RUN_ACTION_VALIDATOR`, ck_main.c:437-483).

**Names do not match.** DOS `s_keenstand` is Omnispeak `CK_ACT_keenStanding`,
`s_keensuitdie1` is `CK4_ACT_KeenSwimDie0`. Zero of 370 (K4) and zero of 312 (K5) names are equal.
Sprite names differ as well (`KEENSITREAD1SPR` and `@SPR_KEENSITREAD1`, `SCUBAKEENL1SPR` and
`@SPR_KEENSWIML1`). **The correspondence is 1:1 anyway**, keyed by DS offset:

| | DOS states | Omnispeak actions | matched | DOS-only | Omnispeak-only | next-state consistent | sprite names differ |
|---|---:|---:|---:|---:|---:|---:|---:|
| Keen 4 | 370 | 370 | 370 | 0 | 0 | 370/370 | 170 |
| Keen 5 | 312 | 312 | 312 | 0 | 0 | 312/312 | 62 |

Generated by `oracle/tools/statemap.py 4|5` into [`statemap-k4.tsv`](statemap-k4.tsv) and
[`statemap-k5.tsv`](statemap-k5.tsv) (source names and line numbers only, no game data). Method:
DOS states are taken in link order (`CK_STATE, CK_KEEN, CK_KEEN2, K?_SPEC, K?_ACT1..3`, from
`RCK?.PRJ`) and declaration order, which is ascending DS offset. Omnispeak actions are sorted by
`compatDosPointer`, and the two sequences are aligned on every field both sides store verbatim
(type, skippable, pushtofloor, tictime, xmove, ymove, which callbacks are NULL, 0/-1 shapes). Every
pair is then cross-checked through `nextstate`. All pairs align with no gaps. Every next-pointer
agrees, offsets ascend within each module, and consecutive offsets differ by exactly 30 bytes
except at 2 (K4) / 5 (K5) places where other initialised data sits between states.
(One K5 action, `CK_ACT_keenCantEnterDoor`, gives its sprite as the literal `126` where DOS says
`KEENWAITR2SPR`.)

**What the traces should carry: the 16-bit DS offset**, not a name. KEENDUMP writes
`(Uint16)ob->state` as is; the fork writes `currentAction->compatDosPointer` (as `CK_SaveObject`
already does). The TSVs translate offsets to names on both sides for humans. Two caveats:

- The offsets in the action files are those of the **original** Apogee v1.4 EXE. Our rebuild is
  byte-identical (`make verify-k4 verify-k5`), so they hold for it too. A KEENDUMP build adds code
  and data behind an `#ifdef`, and data added **before** the states shifts them. Keep KEENDUMP
  globals in a module linked after `K?_ACT3`, or emit a per-build offset table (e.g. from a
  linker map), or translate with `statemap` order + `&s_x` at startup.
  **Resolved in #57:** KEENDUMP adds no DGROUP data at all (far data only, no string literals),
  and `oracle/tools/keendump.sh` proves every DGROUP address unchanged. See
  [`TRACE_FORMAT.md`](TRACE_FORMAT.md).
- An optional byte-exact check that needs a binary, which this doc did not run: build
  Omnispeak with `CK_RUN_ACTION_VALIDATOR` and run it on our rebuilt, unpacked EXE. It
  compares every action field against the EXE's data segment.

## 3. The state machine

### 3.1 The per-frame pipeline (`PlayLoop`, CK_PLAY.C:2182-2422; Omnispeak `CK_PlayLoop`, ck_play.c:2210)

```
PlayLoop                                                   CK_PLAY.C:2182
 │ TimeCount = lasttimecount = tics = 3                    :2202   (US_InitRndT(false) in demos :2196)
 └─ do {
    1 input      PollControls → IN_ReadControl(0,&c)         :2206, :1919   (one demo byte-pair per frame)
    2 think/move for obj = player..: activate / deactivate    :2211-2242
                     StateMachine(obj)                      :2244  → CK_STATE.C:1146
                       DoActor(obj, tics) (+ carry loop)    CK_STATE.C:1029, :1156-1190
                         state->think → sets xtry/ytry, may NewState/ChangeState
                       shape from state, ClipToWalls/FullClipToWalls   :1201-1229
    3 riding     if gamestate.riding: HandleRiding(player)    :2248-2251 → CK_KEEN.C:1892
    4 contact    for obj, for check=obj->next..: overlap → obj.contact(obj,check); check.contact(check,obj)  :2256-2284
    5 in-tiles   CheckInTiles(player) / CheckWorldInTiles     :2289-2296 → CK_KEEN.C:2053
    6 react      for obj: fell out of map → die/RemoveObj;  if needtoreact: react(obj)  (= RF_PlaceSprite)  :2301-2325
    7 camera     ScrollScreen / WorldScrollScreen (K4 map 17 = world-style)  :2329-2340 → :1529
                 UpdateScore(scoreobj)                      :2341
    8 draw+time  RF_Refresh → … → RF_CalcTics  (next frame's tics)   :2351 → ID_RF.C:2329, :2388, :1595
    9 timers     invincible -= tics                         :2353-2357  (K6: groundslam :2360)
   10 keys       demo: any key ends; else CheckKeys          :2385-2403
    } while (playstate == ex_stillplaying)
```

Omnispeak follows the same order: activation and `CK_RunAction` (ck_play.c:2243-2284), its
**existing per-frame dumper** right after the think pass (ck_play.c:2285-2296), riding (2299),
contacts (2302-2324), in-tiles (2326-2359), draw (2361-2385), camera (2388-2391), score box (2394),
`RF_Refresh` (2399), invincibility timer (2401).

**Activation (step 2, CK_PLAY.C:2213-2241).** An inactive object becomes `ac_yes` (and
`needtoreact`) once its tile box overlaps the visible port grown by 1 tile on every side
(`originxtile-1 .. originxtilemax+1`; port = 21×14 tiles, ID_RF.H:73-74). An active object outside
the **inactivate box** (port ± `INACTIVATEDIST` = 4 tiles in K4, **6 in K5**: K4_DEF.H:61,
K5_DEF.H:61; computed in `CenterActor`/`ScrollScreen`, CK_PLAY.C:1397-1415, :1705-1716; Omnispeak
`CK_activeLimit`, ck_play.c:1969-1972) is removed if `ac_removable`. Otherwise, unless
`ac_allways`, it is deactivated **with probability `tics*2/256` per frame** (`US_RndT() < tics*2
|| screenfaded || loadedgame`, :2234), and its think is skipped. The RNG is called once per
out-of-range object per frame. Deactivated objects keep their state and `ticcount`.

### 3.2 `StateMachine` and `DoActor` (CK_STATE.C:1029-1229). Omnispeak: `CK_RunAction`/`CK_ActionThink`, ck_play.c:500-685

`StateMachine` (CK_STATE.C:1146):
1. `xmove = ymove = xtry = ytry = 0` (:1151).
2. `excess = DoActor(ob, tics)` (:1156). If the state changed, `ticcount = 0`.
3. **Carry loop** (:1171-1190): while `excess != 0`:
   `if (!state->skippable && state->tictime <= excess) excess = DoActor(ob, state->tictime-1); else excess = DoActor(ob, excess);`
   and reset `ticcount` on every state change.
4. `state == NULL` → `RemoveObj` (:1191-1195).
5. Shape: if `rightshapenum`, pick right/left by `xdir > 0`. A shape of `-1` becomes 0 (:1201-1215).
6. If `xtry || ytry || shape changed || hitnorth == PLATFORMEDGE` → `FullClipToWalls` (cl_fullclip)
   else `ClipToWalls` (:1217-1229). Note: a `cl_noclip` object still goes through `ClipToWalls`,
   which moves it and computes its box but skips the wall tests.

`DoActor(ob, n)` (:1029-1137):

| `progress` (Omnispeak) | while `ticcount+n < tictime` (or `tictime == 0`) | on reaching `tictime` |
|---|---|---|
| `step` (UnscaledOnce) | `ticcount += n`; **no motion, no think** | `xtry += xdir*xmove`, `ytry += ydir*ymove` **once**; think once; go to `nextstate` |
| `slide` (ScaledOnce) | `ticcount += n`; `xtry += n*xdir*xmove` (per tic); no think | move `usedtics*xmove` (the rest of the state only); think once; next |
| `think` (Frame) | think once per call; `tictime`, `ticcount`, `xmove` ignored; never advances by itself | — |
| `stepthink` (UnscaledFrame) | `ticcount += n`; think once per call; no state motion | as `step` |
| `slidethink` (ScaledFrame) | `ticcount += n`; per-tic motion; think once per call | as `slide` |

Lines: think branch :1036-1049; still-running branch :1052-1078; completion :1079-1137
(`usedtics = tictime - ticcount`, `excess = ticcount+n - tictime`, :1083-1084). A think is
skipped (and `nothink--` instead) while `nothink > 0` (:1040-1047, :1070-1077, :1111-1118).
Completion advances to `nextstate` **only if the think did not change the state itself**
(:1121-1124). If the think removed the object (`state = NULL`), it returns 0.

How `tics` turns into motion, exactly:

- **State motion** comes from the table above. Steps move whole `xmove` amounts on state
  boundaries. Slides move `xmove` per tic, split exactly at the boundary.
- **Think-driven motion** is the think's own `xtry = xspeed*tics` (`T_Velocity`,
  `T_Projectile`, CK_STATE.C:1688-1722) or the per-tic loops in §5.
- `think`/`stepthink`/`slidethink` thinks run **once per `DoActor` call**, i.e. once per frame
  plus once per carry-loop iteration, **not once per tic**. `step`/`slide` thinks run once per
  state completion. Any think that does a fixed amount of work per call (counters, `nothink`,
  `US_RndT()` draws, `temp` increments without `tics`) is frame-rate dependent.
- The carry loop's `!skippable && tictime <= excess` branch runs the new state for only
  `tictime-1` tics, so the state is entered but not left, and the remaining excess is
  **discarded**. For a non-skippable state with `tictime == 0` and excess > 0 it calls
  `DoActor(ob, -1)`. That sets `ticcount = -1`, runs the state's think, and for a `slide*` state
  moves **one tic backwards**. Omnispeak reproduces both (ck_play.c:645-652).
- **Latent bug**, not reachable with vanilla data: when the think removes the object
  (`state = NULL`), `DoActor` returns 0, but `StateMachine` then reads `state->` if excess was
  non-zero (comment at CK_STATE.C:1163). Omnispeak substitutes a fake partial state
  (ck_play.c:496-508, 637-643).

`NewState` (:1245-1290) sets state and shape (no `ticcount` reset), and clips with
`xtry=ytry=0`. `ChangeState` (:1297-1330) also resets `ticcount=0`, sets `needtoreact`, and clips
unless standing on a platform. Omnispeak: `CK_SetAction`/`CK_SetAction2`, ck_phys.c:784-834.
`CK_SetAction`'s shape choice is written differently from `NewState`'s (`if right && xdir>0 …
else if left …`). The two differ only for states with exactly one of left/right = 0. No K4/K5
state has that (checked with statemap's parser), so the difference is latent.

## 4. Movement, clipping, slopes (CK_STATE.C:404-1011). Omnispeak: ck_phys.c

- `ClipToWalls` (:404-608). `pushtofloor` states add `ytry = |xtry| + 16`, or `ytry = 145` on a
  platform (:420-445), to stay glued to slopes. Then **clamp**: `xtry` to ±239, `ytry` to
  `-239..255` (:462-477), so an object can never cross a whole tile (256) in one frame. Then it
  moves, recomputes the hit box from the sprite's `spritetable` box, and resets `hit*`. If
  `needtoclip`, it runs `ClipToEnds` (floors/ceilings), the player-only `PlayerBottomKludge` /
  `PlayerTopKludge`, then `ClipToSides`. If the push-to-floor found no floor it undoes the push
  (:583-604). Finally `xmove/ymove += actual displacement` (:606-607). Always sets `needtoreact`.
  Omnispeak: `CK_PhysUpdateNormalObj`, ck_phys.c:361.
- **Slopes:** `ClipToEnds` (:212-260) reads the north/south wall value of each tile under
  `tilemidx`; its low 3 bits (`SLOPEMASK`) index `wallclip[8][16]` (:33-42) by the pixel column
  within the tile. The floor **snap window** is `maxmove = -|midxmoved| - bottommoved - 16`
  (:220), so it grows with the distance moved this frame. Keen walking on slopes gets
  `slopespeed[hitnorth&7]*tics` extra `xtry` (CK_KEEN.C:192, :812).
- `ClipToSides` (:266-305) tests only the leading tile column; `MoveObjHoriz` does not update
  `midx` in K4/K5 (K6 updates `midx` but not `tilemidx`, :86-100).
- `FullClipToWalls` (:620-748) is used for `cl_fullclip` (platforms, some flyers): the same clamp,
  then a full-box `CheckPosition` retry. `PushObj` / `ClipToSprite*` (:766-1011) let solid
  objects (platforms) push Keen. `HandleRiding` (CK_KEEN.C:1892) carries Keen with
  `gamestate.riding` and re-aligns his `x` to the platform's sub-pixel phase (EGA: 2-px grid).

## 5. The tic loops (CK_STATE.C:1360-1660)

`DoGravity`, `DoWeakGravity`, `AccelerateX/Xv/Y`, `FrictionX/Y` and some actor thinks (K4_SPEC.C:1132,
K5_ACT3.C:917, :1933) iterate **per tic** over absolute time:

    for (i = lasttimecount - tics; i < lasttimecount; i++) { if (i & 1) speed += accel (clamped); try += speed; }

So acceleration happens only on **odd absolute tics** ("limited precision"), and the result depends
on `lasttimecount`'s parity, not just on `tics`. On an odd tic, `DoGravity` returns early when `yspeed`
is in `-4..-1` (:1371-1376), which skips the rest of that frame's `ytry` accumulation.
`DoTinyGravity` has `if (!i & 3)` (:1442), true only for `i == 0`. Because `PlayLoop` sets
`lasttimecount = tics = 3` (CK_PLAY.C:2202), that is the first frame of each level. Omnispeak
copies all of this (ck_phys.c:847-1000, `(tickCount ? 0 : 1) & 3`).

## 6. Timing: where `tics` comes from

- `TimeCount` is incremented in the PIT ISR at 70 Hz (ID_SD.C:782 / :804: 560 Hz /8 with AdLib
  music, 140 Hz /2 without).
- `RF_CalcTics` (ID_RF.C:1595-1645, end of `RF_Refresh`):
  - **Normal play:** busy-wait until `TimeCount - lasttimecount >= MINTICS (2)`, then
    `tics = that`. If `tics > MAXTICS (5)`, then `TimeCount -= tics-5` and `tics = 5` (the game
    slows down). At a steady 35 fps, tics = 2.
  - **Demo record and playback:** wait until `TimeCount >= lasttimecount + 6`, then set
    `lasttimecount += 3`, `TimeCount = lasttimecount + 3` and **`tics = DEMOTICS = 3`**
    (ID_RF.H:41). Demos are fully deterministic: `tics`, `lasttimecount` and `TimeCount` at frame
    boundaries do not depend on machine speed or on time spent dumping. `lasttimecount` advances
    by 3 per frame, so its parity alternates, and the odd-tic loops of §5 accelerate 1, 2, 1, 2 …
    times per frame (at tics=2 it is always exactly 1).
- Omnispeak: `RFL_CalcTics` (id_rf.c:986-1027) with `rf_minTics/maxTics/demoTics = 2/5/3`
  (id_rf.c:614-616, configurable: they must stay at the defaults for vanilla traces). `tics` is
  `SD_GetSpriteSync()`, `lasttimecount` is `SD_GetLastTimeCount()`.
- **Inner refresh loops** call `RF_Refresh` without a `PlayLoop` iteration, which advances
  `lasttimecount`/`TimeCount` between two traced frames. They are world-map `Teleport` /
  `Elevator` (CK_KEEN2.C:703-830, :941-1040), the status window (CK_PLAY.C:1289-1327) and every
  `RF_ForceRefresh` (two refreshes, ID_RF.C:1281-1282). This is why the trace must record
  `lasttimecount`.

## 7. Trace format: recommended fields

**Sampling point:** one record per `PlayLoop` iteration, taken at the **top of the loop before
`PollControls`** (CK_PLAY.C:2206; Omnispeak ck_play.c:2239). At that point the record holds the
complete result of the previous frame, plus the `tics`/`lasttimecount` the next frame will use.
Omnispeak's built-in dumper (`/DUMPFILE`, debug builds, `CK_ENABLE_PLAYLOOP_DUMPER`,
ck_config.h:71, ck_play.c:2285-2296) samples after the think pass instead and writes
`u32 TimeCount` + `CK_SaveGameState` + all 100 objects in the 76-byte DOS layout. It is a useful
second sampling point (it localises a divergence to think versus contact/react), and the fork can
reuse its writer.

**Per-frame header.** All of these exist in both codebases.

| field | DOS | Omnispeak | notes |
|---|---|---|---|
| frame | count of PlayLoop iterations since level start | same | the join key |
| mapon | `gamestate.mapon` | `ck_gameState.currentLevel` | |
| tics | `tics` (unsigned 16) | `SD_GetSpriteSync()` | 3 in demos |
| lasttimecount | `long` | `SD_GetLastTimeCount()` int32 | drives the odd-tic loops; jumps across inner refresh loops |
| TimeCount | `longword` | `SD_GetTimeCount()` | deterministic only in demo mode; read by world-map Keen/teleporter animation (CK_KEEN2.C:762-765) |
| rndindex | `rndindex` (ID_US_A.ASM:34, word, 0..255) | `US_GetRndI()` (id_us_1.c:666) | same 256-byte table; best single desync detector |
| origin | `originxglobal, originyglobal` (unsigned) | `rf_scrollXUnit, rf_scrollYUnit` (int) | mask to 16 bits |
| centerlevel | `centerlevel` (CK_PLAY.C:70) | `screenYpx` (ck_play.c:1931, static) | look up/down camera |
| playstate | `playstate` | `ck_gameState.levelState` | enum values to be mapped |
| riding | slot of `gamestate.riding` or -1 | `ck_keenState.platform` | |
| Keen globals | `jumptime`, `leavepoletime` (CK_KEEN.C:239-240), `invincible`, `oldfirecount`, `jumpheld/pogoheld/fireheld/upheld` | `ck_keenState.jumpTimer/poleGrabTime/…`, `ck_invincibilityTimer` | |
| input | `c.xaxis, c.yaxis, c.button0, c.button1`; `jumpbutton, pogobutton, firebutton` | `ck_inputFrame`, `ck_keenState.*IsPressed` | confirms demo replay |
| score etc. | `gamestate.score, nextextra, ammo, lives, drops, keys[4]`, K4 `wetsuit, rescued`, K5 `keycard, numfuses` | `ck_gameState.*` | |

**Per object.** Walk the live list from `player`, in list order, and write the slot index plus
the 38-word DOS layout of §1, with `state` as the DS offset, `next/prev` as slot indices (or
DS offsets, as Omnispeak's dumper does), and `sprite` as is. Comparison rules:

- Compare `obclass … hitwest, temp1..4` exactly. These are all 16-bit, in the same units on both
  sides.
- `state`: by DS offset. Use statemap only to print names.
- `sprite` and `temp` fields that hold a sprite pointer (stunned stars use `temp3`,
  CK_PLAY.C:2238): **informational only**. They depend on refresh-manager allocation, which is
  presentation (Omnispeak converts with `ck_exe_spriteArrayOffset`, id_rf.c:1321-1329).
- Do not compare `objectcount` (§1) or free slots.

**Representation differences to normalise:**

1. **Widths:** every object field is 16-bit on both sides, but Omnispeak computes in `int` and
   stores to `int16_t`/`uint16_t`. The stored values agree; intermediate comparisons can differ
   where DOS relies on 16-bit wrap or unsigned compares (for example `originxtile-1` is
   unsigned in DOS, CK_PLAY.C:2213). Record such divergences in `docs/ENGINE_DIVERGENCES.md`
   (AGENTS.md rule 6).
2. `TimeCount`/`lasttimecount` are 32-bit on both sides. `tics` is unsigned 16-bit.
3. `needtoreact` (int) vs `visible` (bool): write 0/1.
4. Enums (`active`, `needtoclip`, `progress`) have the same numeric values. `playstate` vs
   `levelState` needs a mapping table.
5. Object order: identical allocator, so the list order and slot numbers are directly comparable.
   A slot mismatch is itself a divergence (a spawn or remove differs).
6. Omnispeak's tunables must be at their vanilla values: `rf_minTics/maxTics/demoTics`
   (id_rf.c:614-616), `CK_activeLimit` (EPISODE.CK?), and no `ck_fixJerkyMotion`-style fixes on
   the sim path.

## 8. Every place behaviour differs between `tics=2` and `tics>2`

Normal play at full speed runs at tics=2. **Built-in demos always run at tics=3.** The golden
traces therefore exercise the `tics>2` column. tics=4/5 only occur on slow machines (5 = MAXTICS
clamp).

Engine core:

1. **Carry-over excess** (CK_STATE.C:1156-1190): `excess ≤ tics-1`, so it is 0/1 at tics=2 and up
   to 2 at tics=3. More states get traversed in one frame, and more thinks run per frame (one per
   carry iteration).
2. **Non-skippable truncation** (:1176-1179): excess is discarded when the next state's
   `tictime ≤ excess`. At tics=2 that needs `tictime ≤ 1`; at tics=3, `tictime ≤ 2`. Animation
   lengths in frames differ.
3. **`DoActor(ob,-1)`** for a non-skippable `tictime==0` next state whenever excess > 0 (§3.2).
   This is more frequent at tics≥3.
4. **Step states** move in whole `xmove` quanta at state boundaries, so per-frame displacement
   becomes lumpier at higher tics. **Slide** motion stays tic-exact.
5. **Think frequency** is per frame, not per tic. This affects `nothink` (`R_Walk` sets
   `nothink = US_RndT()>>5` frames, CK_STATE.C:1796-1830), `T_Stunned`'s star cycle
   (:1747-1755), and every think without `tics` in it.
6. **Odd-tic loops** (§5): one acceleration per frame at tics=2 (always). At tics=3 it alternates
   1 and 2 with `lasttimecount` parity. `DoGravity`'s early return drops the remaining tics'
   `ytry`.
7. **Move clamps** in `ClipToWalls`/`FullClipToWalls` (:462-477, :631-646): falling at
   `yspeed=70` gives `ytry = 140/210/280/350` for tics 2/3/4/5. tics≥4 is clamped to 255
   (terminal velocity changes).
8. **Push-to-floor** `ytry = |xtry|+16` (:430-442) and the **floor snap window**
   `-|midxmoved|-bottommoved-16` (:220) both scale with per-frame motion.

Play loop and camera:

9. Deactivation probability `tics*2/256` per frame (CK_PLAY.C:2234).
10. `invincible -= tics` (:2353-2357).
11. Look up/down camera moves `tics` px per frame, clamped at 167/33 (:1569-1594). Vertical
    follow speed is `min(speed,0x30)*tics` with a floor of `0x10` (:1625-1647), and the floor
    dominates at small tics.
12. `PollControls` old-style/demo pogo detection `oldfirecount += tics`, pogo once `> 8`
    (CK_PLAY.C:1957-1960): with the button held, pogo fires on the 6th frame at tics=2 and on the
    4th at tics=3 (demos always take this path).

Keen (CK_KEEN.C):

13. `KeenStandThink` start-walk `xtry = xdir*xmove*tics/4` (:490).
14. `KeenPauseThink` idle timer `temp1 += tics` (:579) toward the 200/300/700-tic thresholds (:584-610).
15. `KeenLookDownThink` drop-through `ymove = max(4,tics)*PIXGLOBAL` (:701): the same for tics
    2..4, larger at 5.
16. `KeenWalkThink` slope push `slopespeed*tics` (:812); `T_LineUp` `16*tics` (:853-859).
17. `KeenAirThink`/`KeenPogoThink` `jumptime` (:1040-1050, :1165-1173): the ascent is tic-exact
    (`ytry = yspeed*jumptime` on the last part), but the switch to the second jump state and the
    button-release cut happen per frame.
18. Pole re-grab lockout `lasttimecount - leavepoletime < 19` (:295-302, :1278): about 10 frames
    at tics=2, 7 at tics=3.
19. Conveyor `xtry = ±8*tics` (:2184-2191, :2227-2233); `KeenDieThink` `xspeed*tics` (:1640).
20. `R_Stunned` star timer `temp1 += tics`, step at >10 (CK_STATE.C:1975).

Actors (thresholds or RNG comparisons scaled by `tics`; plain `speed*tics` moves are linear
and only change the per-frame step):

21. K4: `T_Council` `tics*8 > rnd` (K4_ACT1.C:322); `T_PestFly` `rnd < tics`, `< tics*2`
    (K4_ACT1.C:1109-1115); `T_Cloud` `rnd < tics` (K4_ACT2.C:277); `T_Bubble` `rnd < tics*16`
    (K4_ACT3.C:766); `BerkeDrawReact` `temp1 += temp2*tics` (K4_ACT2.C:580); `InchContact`
    per-frame touch counter keyed on `(Sint16)lasttimecount` (K4_ACT2.C:732); `R_Platform`
    animation `(lasttimecount>>2)&1` (K4_ACT2.C:1252); `T_KeenSwimSlow` timer and odd-tic loop
    (K4_SPEC.C:1116-1140). Linear: platforms `12*tics` (K4_ACT2.C:1163), drop platform `16*tics`
    (:1335), Dopefish `32*tics` (K4_ACT3.C:642-720), Mimrock (K4_ACT3.C:408).
22. K5: mines `tics*10`, `temp2/temp3 ±= tics*4` (K5_ACT3.C:300, :405-519); `T_Spindread` and
    `T_Sphereful` odd-tic loops (K5_ACT3.C:917, :1933); `R_Sphereful` animation
    `(lasttimecount/8)&15` (:2003). Linear: platforms `12*tics` (K5_ACT1.C:583, :677, :967,
    :1076), `T_DropPlatSit` `tics<<4` (:799), `T_Volte` `tics<<5` (:1239).
23. Shared: `T_FootFly` `temp1 -= tics` (CK_KEEN2.C:637), `PathThink` `temp3 += tics` (:1298),
    `R_Shot` `ymove*tics` (:1544), world-map `Teleport`/`Elevator` `move = tics*2`,
    `duration += tics` with shapes from `TimeCount` (:729-830, :959-1011), `T_Elevate`
    `64*tics` (:845).
24. Presentation only: tile animation `count -= tics` (ID_RF.C:943), status window slide
    `vislines ± tics*8` (CK_PLAY.C:1310, :1330).

## Port notes

- **Memory model:** medium. Data pointers (`statetype *`, `objtype *`, `sprite`,
  `gamestate.riding`) are 16-bit DS offsets, and saved games and `temp` fields store them raw.
  Code pointers (think/contact/react) are far (32-bit). Omnispeak maps between the two with
  `compatDosPointer` and the `*16BitOffset` helpers.
- `int` is 16 bits: `x, y` and the box are `Uint16` and wrap; `xtry/ytry` are `Sint16`; `tics` is
  `unsigned`. Mixed signed/unsigned compares (for example the activation test against
  `originxtile-1`, and the `ClipToEnds` upward loop that never ends at `tiletop == 0`,
  CK_STATE.C:239) are part of vanilla behaviour.
- Map access goes through `Uint16 far *` into `mapsegs[]` (CK_DEF.H:70-73); tile info is `tinf[]`.
- The RNG is assembly (`US_RndT`/`US_InitRndT`, ID_US_A.ASM:63-112): an index into a 256-entry
  table, seeded from DOS time (INT 21h/2Ch) outside demos and 0 in demos (CK_PLAY.C:2194-2201,
  CK_GAME.C:451-456).
- `RF_Refresh` has inline asm and port I/O (EGA write modes, `VW_SetScreen`, ID_RF.C:2329-2389).
  `SetPalette` is `int 10h` (CK_DEF.H:67).
- `TimeCount` is written by the timer ISR (ID_SD.C:760-810). `RF_CalcTics` busy-waits on it.
- Keen 6 differences in shared code: `MoveObjHoriz` updates `midx` (CK_STATE.C:92-94), there is
  an extra slope-wall fix in `ClipToWalls` (:543-578), `groundslam` exists (CK_PLAY.C:2360-2364),
  and `KEEN6Ev15` compiler workarounds apply.

## Timing relevance

- **Reads `tics`:** `StateMachine` (CK_STATE.C:1156), all §5 loops, `T_Projectile`,
  `T_WeakProjectile`, `T_Velocity` (:1688-1722), `R_Stunned` (:1975), PlayLoop deactivation and
  `invincible` (CK_PLAY.C:2234, :2355), `ScrollScreen` (:1569-1647), `StatusWindow`
  (:1307-1330), `PollControls` (:1959), and everything in §8 items 13-23.
- **Reads `lasttimecount`:** §5 loops, `CheckGrabPole` (CK_KEEN.C:295-302, :1278),
  `InchContact`, `R_Platform` (K4_ACT2.C:732, :1252), `R_Sphereful` (K5_ACT3.C:2003). It is
  reset by `singlestep` and debug keys (CK_PLAY.C:779, :859, :876, :897, flagged BUG: assigned
  the wrong way round). `PlayLoop` sets it to 3 (:2202).
- **Reads `TimeCount`:** `RF_CalcTics`, world-map teleport/elevator animation
  (CK_KEEN2.C:762-818), K5 sequences that spin on `TimeCount - lasttimecount < 4`
  (K5_SPEC.C:933-937, :1099-1107), `CK_GAME.C:427` (fade) and `:737` (blink).
- **Waits:** `RF_CalcTics` busy-waits for `MINTICS` or, in demos, `2*DEMOTICS`. `singlestep`
  calls `VW_WaitVBL(14)` and `extravbls` calls `VW_WaitVBL(n)` (CK_PLAY.C:2369-2380). The main
  loop is **not** vblank-synced (AGENTS.md).
- For high refresh and interpolation: everything above stays on the 35/23.3 Hz sim tick.
  Interpolation may read `x, y, shapenum, xmove, ymove` and the origin, but must not change
  `tics`, `lasttimecount` or the RNG, or the trace changes.
