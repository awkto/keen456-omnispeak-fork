# ID_RF: the refresh manager (scrolling, update blocks, sprites, animated tiles)

*Tracker #14. Keen 4 and Keen 5 (v1.4); Keen 6 only where shared code branches for it.*
Written for an agent who will port, trace or modify this code without reading all of it. Every
claim cites `oracle/dos/KEEN4-6/<file>:<line>` (shortened here to `<file>:<line>`). Where the fork has an
equivalent, it is cited as Omnispeak `src/<file>:<line>` (this repo (formerly the `engine/` submodule), branch `main`,
commit `f7a452d`). Our fork's only changes to `id_rf.c`/`id_rf.h` relative to `upstream-master`
are the 20 `WITH_RF_INTERP` lines (§10); every other difference listed in §9 is upstream Omnispeak.

The actor side (objects, `tics`, `PlayLoop`, what reads `lasttimecount`) is in
[ACTORS.md](ACTORS.md). It is not repeated here.

The two facts that the high-refresh and jitter work (E8/E9) rests on:

1. **The horizontal scroll and every sprite sit on a 2-pixel grid of absolute map pixels;
   vertical is 1-pixel** (§2).
2. **`RF_CalcTics` gives the sim 2 tics per frame (35 Hz) normally, a fixed 3 (23.3 Hz) in demos,
   and at most 5.** The time it clips off is also charged to the next frame (§7).

---

## 1. The model

**Coordinates** (ID_RF.H:66-71, ID_RF.C:156-165): *global* units are 1/16 pixel (`PIXGLOBAL=16`,
`TILEGLOBAL=256`). The camera is `originxglobal/originyglobal` (Uint16, global units). Derived values:
`originxtile/ytile` (tile), `originxscreen` (EGA: tile×2 **bytes**), `originyscreen` (tile×16
lines), `originmap` (byte offset into a map plane). `panx/pany` are the pixel offsets inside the
origin tile (0-15). All of these are recomputed by `RFL_CalcOriginStuff` (ID_RF.C:1045-1070).

**The port** is 21×14 tiles (`PORTTILESWIDE/HIGH`, ID_RF.H:73-74). The visible screen is 20×13
tiles (`SCREENTILESWIDE/HIGH`, ID_RF.C:50-51) plus up to 15 px of pan. All drawing is
tile-aligned in the port. Smooth scrolling comes only from moving the CRTC start address and
the pel-pan register.

**Three EGA pages** (EGA only, ID_RF.C:201-202, :308-314). The EGA virtual line is 64 bytes =
512 px (`SCREENWIDTH`, ID_VW.H:47), and `SCREENSPACE = 64*240` (ID_RF.C:53).

| page | `screenstart[i]` | role |
|---|---|---|
| 0, 1 | `0`, `SCREENSPACE` | double-buffered view pages. `screenpage` is displayed (`displayofs`); `otherpage` is drawn (`bufferofs`) |
| 2 | `2*SCREENSPACE` | **master screen** (`masterofs`): the background and foreground tiles, never sprites |

Tiles are drawn once, into the master (`RFL_NewTile`, ID_RF_A.ASM:204). They are then copied
master→view with latched write-mode-1 copies (`RFL_UpdateTiles`, ID_RF_A.ASM:373). When a sprite
moves, it is erased by copying its old rectangle back from the master (`RFL_EraseBlocks`,
ID_RF.C:2075). When the view crosses a tile boundary, all three `screenstart`s move by one tile
(`screenmove`, ID_RF.C:1792-1815). They float through the 64 KB plane and rely on 16-bit address
wrap. The `compatability` flag (ID_RF.C:144, set from config, ID_US_1.C:437) is for SVGAs that do not
wrap: it copies a page to the other end of memory when it would cross 64 KB (ID_RF.C:1796-1810).
CGA instead has one buffer at `0` and a master at `0x8000`, and copies the whole buffer to the
screen each frame (`VW_CGAFullUpdate`, ID_RF.C:3004).

## 2. The 2-pixel horizontal grid (exact)

**Screen.** `RFL_CalcOriginStuff` sets `panx = (originxglobal>>4) & 15` and
`panadjust = panx/8 + ylookup[pany]` (ID_RF.C:1056-1059). `RF_Refresh` then calls
`VW_SetScreen(bufferofs+panadjust, panx & xpanmask)` (ID_RF.C:2363). `xpanmask = 6`, with the
comment "dont pan to odd pixels" (ID_RF.C:204, :318). The byte offset supplies `panx & 8` and the
pel-pan register supplies `panx & 6`, so the **displayed horizontal origin is
`(originxglobal >> 4) & ~1`**: the camera position floored to an even absolute map pixel.
`VW_SetScreen` writes the start address and then `ATR_PELPAN` (ID_VW_AE.ASM:1076-1107). Vertical
has no mask: `pany = (originyglobal>>4) & 15` goes fully into `ylookup` (ID_RF.C:1058), so vertical
is on a 1-pixel grid.

**Sprites.** Each EGA sprite is cached with up to 4 pre-shifted copies at 0, 2, 4 and 6 pixels
(`CAL_CacheSprite`, ID_CA.C:1356-1407; `MAXSHIFTS=4`, ID_VW.H:84). `RF_PlaceSprite` computes
`pixx = (globalx+orgx) >> 4`, `shift = (pixx&7)/2` and `screenx = pixx >> 3` (bytes)
(ID_RF.C:1977-1986). The drawn position is therefore `pixx & ~1`: **the same even absolute map
pixel grid as the camera**. A sprite cached with `shifts=2` only has 0 and 4 px shifts (shift indices
0,1 → copy 0; 2,3 → copy 1, ID_CA.C:1367-1383), so it moves on a 4-pixel grid. With `shifts=1` it
moves on 8 pixels. Vertical sprite position is `globaly >> 4` (ID_RF.C:1987): 1 pixel.

**Why.** The source gives no reason beyond "dont pan to odd pixels". From the code: four shifts
cost 4× sprite memory (eight would cost about 8×). The camera mask then keeps the camera on the
same grid as the sprites. When the camera follows Keen at a fixed offset (ScrollScreen,
CK_PLAY.C:1560-1567), `floor2(keen) - floor2(camera)` stays constant, so the tracked player does
not shimmer against the screen edge. A 1-px camera with 2-px sprites would make Keen jitter by
1 px. `HandleRiding` shows id cared about this relative jitter: it copies the platform's sub-2-px
bits into Keen's `x` (CK_KEEN.C:1926-1964, `SUBPIXELMASK(2)`).
**This rationale is inferred, not documented.**

**Consequence (the jitter root, measured in #89, see [PLAN.md](PLAN.md)).** Walking is
`slidethink` with `xmove=24` global/tic (CK_KEEN.C:157). At `tics=2` that is 48 units = 3 px per frame.
Floored to 2 px, the visible camera (and Keen) step **2, 4, 2, 4** px per frame. Anything moving an
odd number of pixels per frame alternates like this. Vertical motion has no such alias.

**NOPAN** (`/NOPAN`, ID_VW.C:124): `VW_SetScreen` skips the pel-pan write (ID_VW_AE.ASM:1100) and
sprites always use shift 0 (ID_RF.C:1981-1982). Everything horizontal is then on an 8-px grid.

**Omnispeak.** `VL_SetScrollCoords(RF_UnitToPixel(rf_scrollXUnit & 0xef),
RF_UnitToPixel(rf_scrollYUnit & 0xff))`, with the comment "0xef for the X-direction to match EGA
keen's 2px horz scrolling" (id_rf.c:1458-1459). `0xef` clears bit 4 (one pixel = 16 units). The
bits above `0xff` are dropped because the buffer itself scrolls by whole tiles. So
`(u & 0xef) >> 4 == ((u>>4) & 15) & ~1 == panx & 14`, which is **identical to DOS**. Sprites:
`shift = (unshiftedX&7)/2`, `x = unshiftedX & ~7` (id_rf.c:1306-1315), and the shifted copies are
built the same way (id_ca.c:664-700). This is also identical. NOPAN: `vl_noPan` forces shift 0
(id_rf.c:1310) and freezes the X scroll (id_vl.c:973-978).

## 3. Scrolling

- **`RF_Scroll(x, y)`** (EGA ID_RF.C:1754-1880; CGA :2463). Callers pass deltas in global units,
  clamped to ±0xFF so the camera moves less than a tile per frame (CK_PLAY.C:1682-1698,
  :1470-1487). It calls `RFL_BoundScroll`. If the origin tile changed by more than 1, it calls
  `RF_NewPosition` (full redraw, :1775-1782). If it changed by 0, it returns, and only the pan changes.
  If it changed by exactly 1: move the three screens (:1792-1815), float both update windows by
  ±1 / ±`UPDATEWIDE` (:1817-1827), draw the new edge row/column into the master with `RFL_NewRow`, drop
  the anims that scrolled off (`RFL_RemoveAnimsOnX/Y`), and rewrite the row-end zeros and the terminator
  (:1837-1887).
- **`RFL_BoundScroll`** (ID_RF.C:1124-1182) adds the delta, then applies **scroll blocks**. Moving
  right into a vertical block column snaps `originxglobal &= 0xff00`. Moving left snaps to the next
  tile. The same applies vertically. Map edges are scroll blocks too: `RF_NewMap` adds 4 at
  `MAPBORDER` (ID_RF.C:451-454); levels add their own (K4_SPEC.C:501, :505; K5_SPEC.C:507).
- **`RF_NewPosition(x, y)`** (EGA ID_RF.C:1697-1735; CGA :2412) is the absolute version.
  `RFL_BoundNewOrigin` (:1435-1525, clamps to `originx/ymin/max` and pushes the origin away from blocks;
  two known bugs are commented in place) resets the animation list, resets both update windows to
  base, and marks all 21×14 tiles new. Callers: `CenterActor` (CK_PLAY.C:1389), `RF_ForceRefresh`,
  high scores (CK_DEMO.C:2024).

## 4. Update blocks

`update[2][UPDATESIZE]` (ID_RF.C:177) holds one byte per port tile per page: 22 bytes per row
(`UPDATEWIDE = 21+1`, the extra byte is a row-end 0), 14 rows, then `UPDATETERMINATE = 0x0301`
(ID_RF.H:128). There are spare rows on both sides (`UPDATESPARESIZE`, ID_RF.C:63-65) so that
`updatestart[page]` can **float** when the view scrolls a tile without anything being copied. Each
`RF_Refresh` resets `updatestart[otherpage]` to `baseupdatestart` and clears it to 0 with an inline
`rep stosw` (ID_RF.C:2372-2378). `updateptr` = `updatestart[otherpage]` = the page being drawn.

| value | set by | meaning |
|---|---|---|
| 0 | refresh clear | nothing to do |
| 1 | `RFL_NewTile` (ID_RF_A.ASM:209-215), in **both** pages | copy this tile master→view. `RFL_UpdateTiles` finds 1s with `repne scasb` and copies horizontal runs as one block (ID_RF_A.ASM:373-520) |
| 2 | `RFL_EraseBlocks` (ID_RF.C:2156) | a sprite was erased here; sprites overlapping it must redraw |
| 3 | `RFL_UpdateSprites` (ID_RF.C:2256-2266) | a sprite was drawn here. `RFL_MaskForegroundTiles` (ID_RF_A.ASM:535) redraws masked foreground tiles (`INTILE` high bit) over 3s before priority 3 |

The terminator's bytes are `01 03`, so the scan for 1 stops at the first terminator byte and the
scan for 3 at the second (ID_RF_A.ASM:386, :547).

`RF_MapToMap`/`RF_MemToMap` (ID_RF.C:1298-1366, :1377-1425) change map tiles at run time (doors,
switches, bridges). They call `RFL_NewTile` for changed tiles that are on screen and re-check them
for animation. K5's map doors call them before the RF state exists. This is only safe because no
door is near the top-left of the map (K5_SPEC.C:311-338).

## 5. Sprites and the erase/redraw cycle

`spritearray[MAXSPRITES=60]` (ID_RF.H:47/55) holds a free list plus `prioritystart[PRIORITIES=4]`
lists. New and moved-priority sprites are **pushed at the head** (ID_RF.C:1954-1960). Drawing goes
priority 0→3 and, within a priority, from the head (ID_RF.C:2203-2207), so the most recently linked
sprite is drawn first, i.e. underneath. Masked foreground tiles go between priority 2 and 3
(`MASKEDTILEPRIORITY`, ID_RF.H:64; ID_RF.C:2205-2206).

- **`RF_PlaceSprite(&user, gx, gy, chunk, draw, priority)`** (EGA ID_RF.C:1900-2009). Chunk 0 or
  -1 means remove. For an existing sprite it posts erase blocks for its old rectangle:
  `updatecount==0` (drawn on both pages) → erase on both pages, `==1` → only `screenpage`,
  `==2` (never drawn) → nothing (:1925-1929). Then it fills in position, shift, size and tile box
  and sets `updatecount = 2` ("draw on next two refreshes", :2002). `user` is the object's
  `sprite` field (a near pointer, see [ACTORS.md](ACTORS.md)).
- **`RF_RemoveSprite`** (ID_RF.C:2020-2061) posts the same erase blocks, unlinks the sprite and
  frees it.
- **Per refresh:** `RFL_EraseBlocks` copies each pending erase rectangle from the master into the
  page being drawn and marks 2s. `RFL_UpdateSprites` redraws a sprite if `updatecount` is non-zero (then
  decrements it) **or** if any tile under it is non-zero. Redrawn sprites mark 3s and are drawn with
  `VW_MaskBlock` or `VW_InverseMask` (`maskdraw` = white flash) (ID_RF.C:2187-2320). Anything
  overlapping a moved sprite is therefore redrawn on that page too, in priority order.

## 6. Animated tiles

- `RF_MarkTileGraphics` (ID_RF.C:521-707, called from `SetupGameLevel`, CK_GAME.C:478) walks both
  planes. It gives each **unique** animated start tile one `allanims[]` timer (`tiletype {current,
  count}`, foreground tiles have bit 15 set) and **writes a near pointer to it into the info plane**
  (:565/:579, :650/:664). Every copy of a tile therefore animates in lockstep. It also marks every frame of the
  chain for caching. Limits: `MAXANIMTYPES` 65 (K4/K6) / 80 (K5) (ID_RF.H:49, :57).
- `RFL_CheckForAnimTile` (ID_RF.C:751-819) adds an on-screen instance to `animarray[MAXANIMTILES=90]`.
  It is called for each new tile from `RFL_NewRow`, `RF_NewPosition` and `RF_MapToMap/MemToMap`.
  Instances are removed when they scroll off (`RFL_RemoveAnimsOnX/Y`) or are overwritten
  (`RFL_RemoveAnimsInBlock`).
- `RFL_AnimateTiles` (ID_RF.C:931-1005), the first step of each `RF_Refresh`, does
  `count -= tics; while (count < 1) { advance; count += SPEED/MSPEED of the new tile }`
  (:943-957). **It can advance several frames in one refresh.** It then writes the new tile into the
  **map plane** for every on-screen instance whose timer changed (`*(mapplane) = tile & 0x7fff`,
  :988) and calls `RFL_NewTile`. Off-screen instances keep their old map value until they scroll on.
  K6 plays STOMPSND/FLAMESND when a visible chain reaches tile 2152/2208 (:960-965, :475-484).

## 7. `RF_Refresh` and the adaptive tic loop

```
RF_Refresh (EGA, ID_RF.C:2329-2389)
  RFL_AnimateTiles            :2335   reads tics (from the previous CalcTics)
  EGAWRITEMODE(1); RFL_UpdateTiles; RFL_EraseBlocks       :2340-2343   latched copies master→buffer
  EGAWRITEMODE(0); RFL_UpdateSprites                      :2350-2351   (incl. MaskForegroundTiles)
  refreshvector()             :2357-2358  hook: status window, DelayedFade
  VW_SetScreen(bufferofs+panadjust, panx & xpanmask)      :2363   SyncVBL, then CRTC start + pel pan
  clear update[otherpage], flip screenpage/otherpage      :2370-2384
  RF_CalcTics()               :2388
```

The CGA `RF_Refresh` (ID_RF.C:2975-3010) is the same pipeline without page flip or pan. It ends
in `VW_CGAFullUpdate` (:3004) and the same `RF_CalcTics()` call (:3009). **There is one tic loop,
not two.** `RF_CalcTics` (ID_RF.C:1595-1643) is called from both refresh copies (:2388 EGA,
:3009 CGA). The CGA copy still declares `long newtime,oldtimecount;` (:2977), a leftover of the
loop once being inline in `RF_Refresh`. The issue's "~1550-1595 / ~2930" refer to these.

```c
if (lasttimecount > TimeCount) TimeCount = lasttimecount;         // :1602-1603  "paused a LONG time"
if (DemoMode) {                                                   // :1605  record, playback, PlayDone
    oldtimecount = lasttimecount;
    while (TimeCount < oldtimecount + DEMOTICS*2) ;               // :1610-1612  busy-wait to L+6
    lasttimecount = oldtimecount + DEMOTICS;                      // :1613  L += 3
    TimeCount     = lasttimecount + DEMOTICS;                     // :1614  clock := L+3 ahead
    tics = DEMOTICS;                                              // :1615  always 3
} else {
    do { newtime = TimeCount; tics = newtime - lasttimecount; }   // :1622-1626  busy-wait >= MINTICS
    while (tics < MINTICS);
    lasttimecount = newtime;                                      // :1627
    if (tics > MAXTICS) { TimeCount -= tics - MAXTICS; tics = MAXTICS; }   // :1637-1641
}
```

`MINTICS=2`, `MAXTICS=5`, `DEMOTICS=3` (ID_RF.H:39-41). `TimeCount` is the 70 Hz PIT counter
(ACTORS.md §6). `RF_NewMap` resets `lasttimecount = TimeCount; tics = 1` (ID_RF.C:458-459).

- **Normal play:** the frame ends once at least 2 tics have passed since the last frame, so a fast
  machine runs at exactly 35 Hz with `tics=2`. A slower frame yields `tics` 3-5 (the sim takes
  bigger steps).
- **MAXTICS clip, exactly:** `lasttimecount` has already been set to the *unclipped* `newtime`.
  Only `TimeCount` is pulled back by the excess `e = tics-5`. The next frame therefore measures
  `elapsed - e`. If that is negative, the clamp at :1602 lifts `TimeCount` to `lasttimecount` and
  the MINTICS wait adds 2 more real tics. Example: a 10-tic spike gives `tics=5`. If the next frame
  renders in 2 tics, it then waits 2 more and gets `tics=2`. That is 14 real tics for 7 sim tics:
  **the excess is lost once by the clip and up to once more by the charge to the next frame.** A
  machine that is steadily slow (10 tics per frame) settles at `tics=5`, half speed.
- **Demos:** `TimeCount` is left 3 ahead of `lasttimecount`, and the next call waits until it is 6
  ahead. A frame therefore lasts `max(3 real tics, render time)`, and `tics` is always 3. The sim is
  exactly reproducible. `lasttimecount` advances by 3 per frame, so its parity alternates (ACTORS.md §5).
- `FadeAndUnhook` sets `TimeCount = lasttimecount` so the fade-in is not counted
  (CK_GAME.C:421-430). Every extra `RF_Refresh` (`RF_ForceRefresh` = `RF_NewPosition` + 2
  refreshes, ID_RF.C:1278-1283; status window, teleports) runs `RF_CalcTics` again
  (ACTORS.md §6).

**Omnispeak `RFL_CalcTics`** (id_rf.c:989-1030) is a line-for-line copy, including the clamp
(:992-993), the demo branch (:995-1009, `in_demoState != IN_Demo_Off`, PlayDone included as in DOS)
and the MAXTICS charge (:1021-1029, `SetLastTimeCount(inctime)` before the `TimeCount` decrement).
`tics` is computed as a 16-bit difference (:1013), as DOS's `unsigned`. The waits sleep
(`SD_WaitTick`) instead of spinning. MIN/MAX/DEMO come from config `rf_minTics/maxTics/demoTics`
with defaults 2/5/3 (id_rf.c:617-619). The static initialiser `rf_minTics = 1` (:145) is overwritten
there. **The second copy on the fork side is `RFI_Frame`** (§10), which replaces this function
outside demos.

## 8. Public API

Callers are Keen 4/5 game code (Keen 6 only where it is the sole caller). Omnispeak names are on
the right.

| DOS function | purpose | DOS callers | Omnispeak |
|---|---|---|---|
| `RF_Startup` ID_RF.C:266 | tables, page offsets, `xpanmask=6` | CK_MAIN.C:158 | `RF_Startup` id_rf.c:613 (tile surface + tic config); ck_main.c:176 |
| `RF_Shutdown` :348 | empty | CK_MAIN.C:113, CK_PLAY.C:381 | `RF_Shutdown` :624 |
| `RF_FixOfs` :367 | reset page offsets and pan after direct screen use | CK_TEXT.C:777/807/891/986, CK_GAME.C:912/931/952, CK_DEMO.C:1379/1398/1911, CK_PLAY.C:828, ID_US_2.C:1867, K5_SPEC.C:918/1118 | none (`// RF_Reset()` comments, e.g. ck_game.c:997) |
| `RF_NewMap` :404 | map size tables, origin limits, clear lists, border scroll blocks, reset tic clock | `SetupGameLevel` CK_GAME.C:467 | `RF_NewMap` :630; ck_game.c:608 |
| `RF_MarkTileGraphics` :521 | cache tiles, build `allanims`, info-plane pointers | CK_GAME.C:478 | :266; ck_game.c:619 |
| `RF_SetScrollBlock` :1097 | add a scroll block (`horizontal` = blocks vertical motion) | K4_SPEC.C:501/505, K5_SPEC.C:507, K6_SPEC.C:401/405 | :196 (param named `vertical`; same truth value) |
| `RF_NewPosition` :1697 (CGA :2412) | jump camera, full redraw | `CenterActor` CK_PLAY.C:1389, `DrawHighScores` CK_DEMO.C:2024, `RF_ForceRefresh` | `RF_Reposition` :1032; ck_play.c:1966 (`CK_CentreCamera`), :850, ck_inter.c:1609 |
| `RF_Scroll` :1754 (CGA :2463) | smooth-scroll camera by a delta | `WorldScrollScreen` CK_PLAY.C:1487, `ScrollScreen` :1698 | `RF_SmoothScroll` :1055; ck_play.c:2016 (`CK_MapCamera`), :2198 (`CK_NormalCamera`) |
| `RF_MapToMap` :1298 | copy a block of map tiles (3 planes) | K4_ACT1.C:95-187, K5_ACT3.C:366-367 | `RF_ReplaceTileBlock` :693 |
| `RF_MemToMap` :1377 | write tiles from memory into one plane | CK_KEEN.C:934/950/992/1989/2025/2424, CK_KEEN2.C:766-1631 (10 sites), K4_ACT3.C:269, K5_SPEC.C:311/360, K6_SPEC.C | `RF_ReplaceTiles` :746 |
| `RF_ClearBlock` :1534 | post erase blocks for screen text | **no callers** | `RF_EraseRegion` :1132 |
| `RF_RedrawBlock` :1571 | mark tiles for redraw | **no callers** | none |
| `RF_PlaceSprite` :1900 (CGA :2578) | add or move a sprite | `R_Draw`/`R_Walk`/`R_WalkNormal`/`R_Stunned` CK_STATE.C:1784-1983, CK_KEEN.C (7), CK_KEEN2.C (9), ~60 actor sites in K4_ACT*/K5_ACT* | `RF_AddSpriteDraw` :1238 (+`...Using16BitOffset` :1350 for `temp` fields) |
| `RF_RemoveSprite` :2020 (CGA :2689) | remove a sprite | `RemoveObj` CK_PLAY.C:1856/1859, `PlayLoop` :2236/2238, CK_PLAY.C:832, CK_KEEN2.C:1018, K4_ACT2.C:1257-1282, K5_ACT3.C:296/394/461 | `RF_RemoveSpriteDraw` :1143 (+16-bit variant :1340) |
| `RF_CalcTics` :1595 | adaptive tic loop (§7) | only `RF_Refresh` :2388/:3009 | `RFL_CalcTics` :989 (static use only) |
| `RF_Refresh` :2329 (CGA :2975) | draw and show a frame, then compute `tics` | `PlayLoop` CK_PLAY.C:2355; `StatusWindow` :1289-1327; `Teleport`/`Elevator` CK_KEEN2.C:729-959; `CheckWorldInTiles` :1011; `CheckHighScore` CK_DEMO.C:2108-2109 | `RF_Refresh` :1436; ck_play.c:2399, :1848-1908, ck_map.c:445-538, ck5_map.c, ck_inter.c:1717-1718 |
| `RF_ForceRefresh` :1278 | `RF_NewPosition` + 2× `RF_Refresh` | CK_PLAY.C:745-896 (cheats/debug), `KeenContact` CK_KEEN.C:1769/1796, CK_KEEN2.C:1070, K4_ACT3.C:1313, K6_ACT1.C | `RF_ForceRefresh` :682 |
| `RF_SetRefreshHook` :1190 | per-refresh callback before display | `DelayedFade` CK_GAME.C:406/426, `StatusWindow` CK_PLAY.C:1303-1335 | `RF_SetDrawFunc` :608 |
| `RF_FindFreeBuffer` :1657 | spare EGA page offset | `StatusWindow` CK_PLAY.C:1292 | `#if 0` (ck_play.c:1856); uses surfaces |

Internal (`RFL_`): `RFL_NewTile`, `RFL_UpdateTiles` and `RFL_MaskForegroundTiles` (asm), plus
`RFL_CalcOriginStuff`, `RFL_BoundScroll`, `RFL_BoundNewOrigin`, `RFL_NewRow`,
`RFL_AnimateTiles`, `RFL_CheckForAnimTile`, `RFL_RemoveAnims*`, `RFL_InitAnimList`,
`RFL_InitSpriteList`, `RFL_EraseBlocks` and `RFL_UpdateSprites`. `StatusWindow` calls
`RFL_InitAnimList` directly (CK_PLAY.C:1290).

## 9. Globals

| global | decl | meaning |
|---|---|---|
| `tics`, `lasttimecount` | ID_RF.C:141-142 | sim step and frame clock (§7, ACTORS.md). Omnispeak `SD_Get/SetSpriteSync`, `SD_Get/SetLastTimeCount` |
| `compatability` | :144 | SVGA no-wrap mode (§1); from config |
| `originx/yglobal`, `originx/ytile`, `originx/yscreen`, `originmap` | :156-159 | camera (§1). Omnispeak `rf_scrollX/YUnit` only; the rest is derived on demand |
| `originx/ymin/max` | :160 | camera limits from `RF_NewMap` (MAPBORDER=2 tiles). Omnispeak `rf_scrollX/YMin/MaxUnit` |
| `panx, pany, pansx, pansy, panadjust` | ID_VW.C (extern) | pan within the origin tile; `panadjust` = start address part |
| `xpanmask` | :204 | `6`: the 2-px mask (§2). Omnispeak: the literal `0xef` |
| `screenstart[3]`, `screenpage`, `otherpage`, `masterofs` (+ `bufferofs`, `displayofs` in ID_VW) | :201-208, :162 | the three EGA pages (§1). Omnispeak `rf_tileBuffer` + `VL_GetActiveBuffer()` |
| `update[2][]`, `updatestart[2]`, `baseupdatestart[2]`, `updateptr` | :177-180 | update arrays (§4). Omnispeak `rf_dirtyBlocks[2][21*14]` as a ring with `rf_dirtyBufferOffset` |
| `blockstarts[]`, `updatemapofs[]`, `uwidthtable[]`, `mapbwidthtable[]` | :170-173, :148 | lookup tables (screen offset / map offset per update slot) |
| `mapwidth, mapheight, mapbyteswide, ...` | :146-147 | map dimensions |
| `spritearray[60]`, `prioritystart[4]`, `spritefreeptr` | :211-212 | sprite lists (§5). Omnispeak `rf_spriteTable`, `rf_firstSpriteTableEntry`; the 32-byte DOS record size is kept for save-game offsets (id_rf.c:1330-1338) |
| `eraselist[2][60]`, `eraselistptr[2]` | :218 | pending erase blocks per page. Omnispeak `rf_spriteErasers` |
| `allanims[]`, `numanimchains` | :196-197 | anim timers, pointed to from the info plane (§6). Omnispeak `rf_animTileTimers[180]` (offsets converted for saves, id_rf.c:113-121) |
| `animarray[90]`, `animhead`, `animfreeptr` | :214 | on-screen anim instances. Omnispeak `rf_onscreenAnimTiles` |
| `hscrollblocks, vscrollblocks, hscrolledge[6], vscrolledge[6]` | :220-221 | scroll blocks. Omnispeak `rf_vert/horzScrollBlocks` (**names swapped**: DOS `hscrolledge` = Omnispeak `rf_vertScrollBlocks`) |
| `refreshvector` | :199 | refresh hook. Omnispeak `rf_drawFunc` |

## 10. Differences vs Omnispeak (and what `rf_interp.c` bypasses)

Same behaviour, different structure:

- **No EGA pages.** `rf_tileBuffer` (336×224 surface) is the master. When the camera crosses a
  tile, `RF_SmoothScroll` shifts it by one tile (`VL_SurfaceToSelf`) and scrolls the screen buffers
  (`VL_ScrollScreen`) (id_rf.c:1073-1083). The dirty-block ring offset (:1086) replaces the floating
  `updatestart`. The number of pages is `VL_GetNumBuffers()`, not a fixed 2. Erase and dirty logic
  are otherwise identical (values 1/2/3, `updateCount`, erase on `RF_GetLastPage(0/1)`).
- Scroll bounding (`RFL_SmoothScrollLimit` :930, `RF_RepositionLimit` :864) reproduces both DOS
  `RFL_BoundNewOrigin` bugs (same `+10`/`+11..+20` and `+6`/`+7..+13` windows).
- Masked foreground tiles: `RFL_RenderForeTiles` (:784) scans the whole port for dirty==3,
  instead of `repne scasb`.

Real differences:

1. **Tile animation catch-up (fidelity candidate).** DOS uses `while (count < 1)` and can advance
   several frames per refresh (ID_RF.C:944). Omnispeak uses `if (timeToSwitch <= 0)` and advances at
   most one frame (id_rf.c:557). These differ only when a chain's SPEED is less than `tics` (SPEED 1
   at `tics=2`, SPEED ≤2 in demos). In that case Omnispeak's animation lags permanently. Animated
   tiles are written back into the map plane, and K6 plays sounds from them, so this could show up
   in a map-hashing trace or as K6 sound timing. Not yet checked against data: check the anim SPEED
   values (#60/#62).
2. Bounds check in `RFL_AnimateTiles`: DOS quits at `x >= 21 || y >= 14` (unsigned, ID_RF.C:993);
   Omnispeak quits only at `> 21`/`> 14` (id_rf.c:590). Only affects error paths.
3. Sprite redraw box: DOS uses `(portx + width - 1) >> 1` (ID_RF.C:2217); Omnispeak uses
   `PixelToTile(pixelX + sw)` with no `-1` (id_rf.c:1371-1372). Omnispeak can check and mark one
   extra column or row. This affects presentation only.
4. `RF_NewMap` in Omnispeak clears the pending erasers (id_rf.c:648-649); DOS does not. This affects
   presentation only.
5. `RF_MarkTileGraphics`: DOS skips tiles `>= 0x8000` (`tile>=0` on an `int`, ID_RF.C:543) and
   allows 65/80 unique anims; Omnispeak allows 180. Both keep the stale `i` used for K6 sound
   marking (id_rf.c:270-272).
6. **Present/vsync.** DOS `VW_SetScreen` first calls `SyncVBL` (ID_VW_AE.ASM:1079). From the asm
   (ID_VW_AE.ASM:1027-1066): it waits until the display is blanked with vertical retrace not yet
   active, held for 5 consecutive status reads (10 with `jerk`). It gives up after `TimeCount`
   advances 3. The page flip is therefore tear-free, but the frame rate still comes from the PIT
   (§7). Omnispeak `VL_Present` depends on the backend's vsync.
7. `RF_ClearBlock`/`RF_RedrawBlock`/`RF_FixOfs`/`RF_FindFreeBuffer` have no live Omnispeak
   equivalent (unused or replaced by surfaces).

**`rf_interp.c` (our fork, #89/#92; built with `WITH_RF_INTERP`, off unless `interpolation` /
`/INTERP`)** has three hooks into RF:

- `RF_SpriteDrawEntry` gains `curUX/curUY/prevUX/prevUY` (id_rf.h:37-39).
- `RF_AddSpriteDraw` calls `RFI_NoteSprite` with the **unquantised** object position, and sets
  prev=cur for new sprites (id_rf.c:1248-1250, :1323-1325; rf_interp.c:171-180).
- `RF_Refresh` runs the whole vanilla pipeline as usual, then returns into `RFI_Frame` in place of
  `VL_Present` + `RFL_CalcTics` (id_rf.c:1461-1468).

`RFI_Frame` (rf_interp.c:367-428) presents frames at display rate for the frame's wall-clock
duration (`tics/70` s). It lerps the camera and each sprite from prev to cur, then quantises:
`grid` uses 32 units (2 px) for X and 16 for Y, which reproduces the vanilla grid; `pixel` uses 16;
`subpixel` uses 4 (rf_interp.c:278-306). It composes each frame **directly from the map planes and
the unshifted sprite data** (`BuildLayers`, `BlitSprite`, :191-276). This bypasses the presented
vanilla image (tile buffer, dirty blocks, erasers, pre-shifts, the `& 0xef` scroll, the VL screen)
and anything a `rf_drawFunc` hook draws onto the screen. From reading the code, the status window's
scroll-down therefore seems not to appear in interp mode; this has not been run. Clock: in demos
it still calls `RFL_CalcTics` (:399-404), so traces are unchanged. Outside demos it **bypasses
the adaptive loop**: `tics` is forced to 2 and `TimeCount = lasttimecount += 2` (:405-411). There is
no MINTICS spin and no MAXTICS path; pacing comes from `frameStart += dur`, which resets after a
0.25 s stall (:376, :388).

## Port notes

- **Inline asm and port I/O:** `RF_Refresh` clears the update array with `rep stosw`
  (ID_RF.C:2372-2378). `EGAWRITEMODE`/`EGAMAPMASK` (:2340-2350) program the Graphics Controller and
  Sequencer: write mode 1 is a latched 4-plane byte copy, used for all master→view copies. ID_RF_A.ASM
  does all tile drawing and scanning. The masked-tile paths run with interrupts off and SS borrowed
  as a segment register (ID_RF_A.ASM:285-289). `VW_SetScreen`/`SyncVBL` write the CRTC start address and
  the attribute controller pel pan, and poll `STATUS_REGISTER_1` (ID_VW_AE.ASM:1027-1107).
- **16-bit wrap is load-bearing:** `screenstart[]`, `bufferofs`, `masterofs` and `displayofs` are
  `unsigned` offsets into the 64 KB EGA plane that wrap as the view scrolls (ID_RF.C:1792-1815).
  `originxglobal` is `unsigned` (global units, so the map is at most 256 tiles across).
  `RFL_CalcOriginStuff` takes `long` so callers can pass values that would overflow (:1045).
- **Near pointers in data:** the info plane stores near pointers to `allanims[]` (ID_RF.C:565-579,
  :650-664), which go into saved games with the map. Object `sprite` fields are near pointers into
  `spritearray` (32-byte records). Omnispeak converts both with exe-offset tables
  (id_rf.c:113-121, :1330-1338).
- **Far/segment pointers:** `animtiletype.mapplane` is a `far` pointer into a movable map segment,
  flagged BUG: it can be invalid after a memory-manager sort (ID_RF.C:129). Sprites are `_seg` blocks
  in `grsegs[]` (ID_RF.C:1967).
- **Two builds in one file:** `#if GRMODE == EGAGR` / `CGAGR` duplicate `RF_NewPosition`,
  `RF_Scroll`, `RF_PlaceSprite`, `RF_RemoveSprite`, `RFL_EraseBlocks`, `RFL_UpdateSprites` and `RF_Refresh`
  (ID_RF.C:1687-2391 EGA, :2401-3012 CGA). The Keen 4/5 targets we verify are EGA.
- Keen 6 adds anim sound tiles and per-chain `visible` counts (ID_RF.C:117-121, :464-501).

## Timing relevance

- **Reads `tics`:** `RFL_AnimateTiles` (ID_RF.C:943). It uses the *previous* frame's `tics`, because
  `RF_CalcTics` runs at the end of `RF_Refresh`.
- **Writes `tics`, `lasttimecount`, `TimeCount`:** `RF_CalcTics` (ID_RF.C:1595-1643, §7) and
  `RF_NewMap` (:458-459). Callers also write them: `FadeAndUnhook` (CK_GAME.C:427) and `PlayLoop`
  (see ACTORS.md).
- **Waits:** `RF_CalcTics` busy-waits on `TimeCount` (MINTICS, or `2*DEMOTICS` in demos).
  `VW_SetScreen` → `SyncVBL` waits for vertical blank, at most 3 `TimeCount` ticks, on every
  `RF_Refresh` (ID_RF.C:2363). Each frame is thus "render → wait for vblank → flip → wait for the
  PIT to reach 2 tics". The flip is vblank-aligned. The frame rate is PIT-driven (35 Hz, or 23.3 Hz
  in demos), not vblank-locked. On VGA's 70 Hz 200-line mode, each frame is normally shown for 2
  refreshes.
- **Every `RF_Refresh` is a clock step.** Inner loops (`RF_ForceRefresh`, status window,
  teleports, high scores) advance `lasttimecount` without a `PlayLoop` iteration.
- **For E8/E9:** the jitter comes from (a) the 2-px X quantisation of camera and sprites (§2,
  presentation-only; Omnispeak reproduces it with `& 0xef` and `(x&7)/2`) and (b) the 35 Hz
  `tics=2` sim step (§7). Interpolation may replace (a) and the presentation of (b). It must not
  change what `RFL_CalcTics` gives the sim in vanilla or demo mode, the `RFL_AnimateTiles` map
  writes, or the order of sprite and anim list operations, or traces will diverge.

## Animation speeds in the shipped data

*Tracker #95, first acceptance item. Reproduce: `python3 tools/tileanim.py --levels` (needs `$KEEN_DATA`).*

**Where tileinfo comes from.** The DOS EXE links `MAPHEAD.CK?` statically (`MAPHEADERLINKED`,
ID_CA.C:928-931); `tinf` points at it, and the tile tables follow the 402-byte header (RLEW tag + 100
level offsets): `SPEED=402`, `ANIM=SPEED+NUMTILE16` (ID_CA.H:61-73). Tables, in order: bg speed (N bytes),
bg anim (N, signed delta to the next frame), then M-sized foreground tables (top, right, bottom, left,
anim `MANIM`, `INTILE`, speed `MSPEED`). N/M = 1296/2916 (K4), 1512/2952 (K5). The tool rips the block out of
`KEEN?.EXE` with the `static/ripck?.pat` offsets and checks it is byte-identical to the
`data/keen?/TILEINFO.CK?` that Omnispeak loads (`id_ca.c:1105-1115`, `id_ti.c`). It is. A chain
is a start tile with speed and anim both non-zero; the timer starts at the start tile's speed and each advance
adds the speed of the *new* tile (ID_RF.C:943-957, same in Omnispeak `id_rf.c:545-575`).

**Condition.** `count -= tics; while (count < 1)` advances twice in one refresh only if a frame's speed is
below `tics` (strictly, speed <= tics-1, and then depends on phase). With speed 2 and tics 3 it happens every
other refresh in steady state (DOS 1.5 frames/refresh, Omnispeak `if` 1 frame/refresh and a timer that drifts
ever further negative). Speed >= tics can never double-step.

Frames per speed (all chain frames; "min" = chains whose slowest-to-fire frame is that speed):

| | speed 2 | 4 | 6 | 8 | 10-16 | >=20 |
|---|---|---|---|---|---|---|
| K4 bg (79 chains) | 0 | 0 | 0 | 109 fr / 43 ch | 16: 2 | 32..220 |
| K4 fg (246 chains) | **32 fr / 8 ch** | 12 fr / 4 ch | 96 fr / 24 ch | 385 fr / 127 ch | 16: 1 ch | 20..64 |
| K5 bg (123 chains) | **87 fr / 30 ch** | 210 fr / 69 ch | 0 | 0 | 10: 2 ch, 16: 16 ch | 32: 6 ch |
| K5 fg (414 chains) | 0 | 192 fr / 56 ch | 183 fr / 61 ch | 1124 fr / 132 ch | 10-16: 127 ch | 20..32 |

Verdict (chains with any frame speed < tics; "in levels" = start tile occurs in some map of the episode):

| episode | tics=2 (normal) | tics=3 (demos) | tics=5 (load) |
|---|---|---|---|
| Keen 4 | none | 8 fg chains, all in levels | 12 fg (9 in levels); no bg |
| Keen 5 | none | 30 bg chains, **none in any level** | 99 bg (23 in levels) + 56 fg (28 in levels) |

So: **at tics=2 nothing can double-step in either episode** (shortest shipped speed is 2, and 2 is not < 2).
The fidelity difference is therefore real but confined to:

- **Keen 4, tics=3 (demos and the fixed 3-tic case): 8 foreground chains of speed 2**, tiles 981-984
  (HillVille, map 7) and 1482-1485 (Sand Yego map 8, Miragia map 9, Isle of Fire map 16). All frames of each chain share the same `INTILE` (3) and the same wall bytes, so
  **gameplay is identical; only the visual frame phase differs** (DOS cycles 1.5x faster than the fork).
  Tile 976 chain (maps 7, 8, 9) has speed 4 and a 255 frame: not < 3, but < 5.
- **tics=5 (heavy load, up to 5 per frame)**: Keen 4 speed-4 foreground chains (976-977/975, maps 7-9) and the
  Keen 5 speed-4 chains: bg 1296-1371 in Ion Vent System (map 1) and fg 514-570 (map 0 Armageddon Machine),
  1126-1133 (many reactor levels, maps 1-13), 1930/1948 (maps 4, 6, 8, 10, 13), 2178-2197 (map 4). Purely visual.
- **Keen 5 bg speed-2 chains** (tiles 149-153, 167-171, 185-194, 203-212) exist in tileinfo but no map plane
  contains their start tiles.

Keen 6 was not checked (no data, #90). One K4 fg start tile (1851, speed 16) has a chain that never returns to its
start or ends cleanly (frames of speed 255); it is not a speed < tics case.
