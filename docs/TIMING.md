# Timing: the complete model (PIT clock, adaptive tics, vblank, demos)

*Tracker #19 (absorbs the timer half of #16). Keen 4 and Keen 5 v1.4; Keen 6 only where it differs.*
Ground truth for E8 (high refresh, #74-#79) and E9 (jitter fixes, #84-#88). DOS citations are
`oracle/dos/KEEN4-6/<file>:<line>` shortened to `<file>:<line>`. Omnispeak citations are the fork's
`src/<file>:<line>` on branch `main` (commit `f7a452d`), written `src/<file>:<line>`.

This doc does not repeat what is already written:

- `RF_CalcTics`, the MAXTICS clip and its charge to the next frame, the vblank-aligned flip, and the
  2-px grid are in [ID_RF.md](ID_RF.md) §2, §7 and §10.
- What changes in the sim between `tics=2` and `tics>2`, the odd-tic loops, and the inner refresh
  loops are in [ACTORS.md](ACTORS.md) §5, §6 and §8.
- Demo determinism and why the trace excludes `TimeCount` are in
  [TRACE_FORMAT.md](TRACE_FORMAT.md) ("Demo playback runs on fixed tics").
- The motion-trace baselines are in [HARNESS.md](HARNESS.md) "Motion trace".

**Measurements (cited, not repeated here).** The keen456 project measured the DOS build under
DOSBox-X with `native/bench-cadence.sh`. The display ran at 70.09 Hz, and 91-99% of presented frames were
exactly 2 vblanks apart. The emulated CPU speed (cycles) made no difference to the cadence (AGENTS.md
"Timing facts"; issue #19 text). The #89 spike measured the 2-px scroll grid as the main visible
jitter (PLAN.md).

---

## 1. The clock: `TimeCount` and the PIT

`ID_SD` reprograms PIT channel 0 and installs its own IRQ 0 handler `SDL_t0Service`
(ID_SD.C:759-840, installed at :1040). `TimeCount` is a `longword` (ID_SD.C:74, ID_SD.H:184)
that only this ISR increments:

| music mode | ISR rate requested | PIT divisor | real ISR rate | `TimeCount` ++ every | real `TimeCount` rate |
|---|---|---|---|---|---|
| AdLib music on | `TickBase*8` = 560 Hz (ID_SD.C:897) | 1192030/560 = **2128** | 560.706 Hz | 8th call (`!(++count & 7)`, :779-782) | **70.088 Hz** |
| music off | `TickBase*2` = 140 Hz (:899) | 1192030/140 = **8514** | 140.144 Hz | 2nd call (`!(++count & 1)`, :801-804) | **70.072 Hz** |

`SDL_SetIntsPerSec` divides by 1192030 instead of the PIT's 1193182 Hz (ID_SD.C:154-157), and the
division truncates. **The game clock is therefore never exactly 70 Hz.** It runs 0.10-0.13% fast, and
its rate depends on the music setting. `TickBase = 70` (ID_SD.H:41). `SDL_SetTimerSpeed`
(ID_SD.C:891-901) is re-run by `SD_SetSoundMode`/`SD_SetMusicMode` (:954, :994). Those are menu and
config actions, so the rate is constant during play. The `static count` (:762) is shared by both
modes, so the first tic after a mode switch can be short. `LocalTime` (:95) is incremented next to
`TimeCount` and nothing in the game reads it. The ISR chains to the BIOS handler every 65536 PIT counts
(:819-827).

The VGA 320×200 (mode 0Dh, double-scanned 400 lines) refresh rate is 25.175 MHz / (800 × 449) =
**70.086 Hz**. The clock and the display therefore beat slowly: once every **~519 s** with AdLib music
on, and once every **~69 s** with music off. This slow phase drift is why the measured cadence is
"exactly 2 vblanks" for 91-99% of frames and not 100% (§3). EGA 200-line mode on an EGA monitor runs
at ~60 Hz (§5).

**Omnispeak** (`src/id_sd.c`) emulates the same thing. `SDL_t0Service` (:757-802) has the same
`count & 7` / `count & 1` logic. The divisors are computed with the same 1192030 constant
(:57-65, `SD_SetIntsPerSecond` :109-115, `SD_SetTimerSpeed` :117-122), and the backend converts
them back to time with the real 1193182 Hz. The rates above therefore carry over exactly. By default
(`sd_sdl_audioSync` false, `src/id_sd_sdl.c:502`) the ISR is driven by a timer thread on
`SDL_GetPerformanceCounter` (`SD_SDL_t0InterruptThread`, id_sd_sdl.c:405-479). That thread resyncs
if it falls more than 5 s behind (:469-473). Optionally it is driven by the audio callback instead
(:348-360). `TimeCount`, `lasttimecount` and `tics` become `sd_timeCount` (uint32),
`sd_lastTimeCount` (int32) and `sd_SpriteSync` (uint16) (id_sd.c:68-72), behind
`SD_Get/Set{TimeCount,LastTimeCount,SpriteSync}` (:77-106). `SD_WaitTick` (:805-809; SDL:
id_sd_sdl.c:638-645, a 2 ms condition-variable wait) replaces the DOS busy-spins.

**16-bit hazard (DOS only).** `TimeCount` is 32 bits on a 16-bit CPU. A main-line read or write
(`TimeCount = lasttimecount`, `TimeCount -= …`) takes two word accesses. If the ISR's carry into
the high word lands between them, the access is torn. This can happen only once per 65536 tics
(15.6 min) and has not been observed. The fork does not have this hazard.

## 2. The three variables: every reader and writer

Semantics:

- **`TimeCount`** is wall-clock tics. The game also rewrites it (clamp, MAXTICS charge, demo
  lock-step, resets).
- **`lasttimecount`** (`long`, ID_RF.C:142) is the **game clock**: the `TimeCount` value at the end
  of the last `RF_CalcTics`. Sim code uses it as "now".
- **`tics`** (`unsigned`, ID_RF.C:141) is the step size of the sim frame: the duration of the
  previous frame (adaptive timing). It is computed at the end of `RF_Refresh` and used by the next
  `PlayLoop` iteration.

### 2.1 `TimeCount`

| DOS | role | Omnispeak |
|---|---|---|
| ID_SD.C:782, :804 | **W** ISR `++` | id_sd.c:766, :788 |
| ID_SD.C:1041 | **W** `= 0` at `SD_Startup` | id_sd.c:579 |
| ID_RF.C:1602-1603 | **R/W** clamp `if (lasttimecount > TimeCount) TimeCount = lasttimecount` | id_rf.c:992-993 |
| ID_RF.C:1611, :1614 | **R/W** demo wait to `L+6`, then `= L+3` | id_rf.c:999, :1006 |
| ID_RF.C:1624, :1639 | **R/W** normal wait; MAXTICS charge `-= tics-5` | id_rf.c:1012, :1027 |
| ID_RF.C:458 | **R** `RF_NewMap` copies it into `lasttimecount` | id_rf.c:657 |
| ID_VW_AE.ASM:1029-1035 | **R** `SyncVBL` timeout (low word, +3) | n/a (backend vsync); DOS backend id_vl_dos.c:819-870, :931-941 |
| CK_PLAY.C:2202 | **W** `TimeCount = lasttimecount = tics = 3` at `PlayLoop` start | ck_play.c:2232-2234 |
| CK_GAME.C:427 | **W** `FadeAndUnhook`: `= lasttimecount` ("don't adaptively time the fade") | ck_game.c:578 |
| CK_PLAY.C:779, :859, :876, :897 | **R** the flagged lines (§6) | ck_play.c:1118, :1259, :1275 (no boss key) |
| CK_PLAY.C:2376 | **R** single-step: `lasttimecount = TimeCount` after `VW_WaitVBL(14)` | ck_play.c:2419 (`ck_slowMotionEnabled`) |
| CK_KEEN2.C:762, :765, :815, :818 | **R** world-map `Teleport`: Keen frame `(TimeCount>>3)%3`; teleporter tile `(TimeCount>>2)&mask` **written into the map** (`RF_MemToMap` :766) | ck_map.c:488, :491, :544, :546 |
| CK_GAME.C:737 | **R** `HandleDeath` selection-bar blink | ck_game.c:800 |
| CK_TEXT.C:195-196 | **W/R** `TimedPicCommand`: `TimeCount = 0`, spin to `picdelay` | ck_text.c:111 (commented out) |
| CK_DEMO.C:437-439, :545, :996, :1810, :1851 | **R/W** title/intro loops (`SlideLetters`, `ScaleDown`, `ScrollSWText`) | ck_inter.c:372-376, :461-466, :805-814, :1368-1426 |
| K5_SPEC.C:933, :937, :1099-1107 | **R** K5 `GameOver` star-field pacing (4 tics/step) | ck5_misc.c:792-807 |
| ID_IN.C:422, :431 | **R** adaptive mouse scaling | n/a |
| ID_IN.C:477-478 | **R** joystick button debounce (2 tics) | n/a |
| ID_IN.C:1232, :1241 | **R** `IN_UserInput` timeout | id_in.c:915, :929 |
| ID_US_1.C:1203, :1329-1335 | **R** line-input cursor blink | id_us_1.c:426, :569-575 |
| ID_US_2.C:847/856, :980/983, :2022/2026, :1459-1465 | **R** control-panel blinks, Pong pacing | ck_us_2.c:792-806, :995-1009, :1134-1150; id_us_2.c:1092-1097, :1179, :1222 |
| n/a | **R/W** fork only: `VL_GetTics`/`VL_Present` frame stamp | id_vl.c:955, :1131 |
| n/a | **W** fork only: null video backend advances time on `WaitVBL` (headless) | id_vl_null.c:250, :259 |
| n/a | **R** fork only: playloop dumper records it | ck_play.c:2291 |
| n/a | **W** fork only: `RFI_Frame` sets `TimeCount = lasttimecount` (§8) | rf_interp.c:410 |

### 2.2 `lasttimecount`

| DOS | role | Omnispeak |
|---|---|---|
| ID_RF.C:458 | **W** `RF_NewMap`: `= TimeCount` (and `tics = 1`, :459) | id_rf.c:657-658 |
| ID_RF.C:1610, :1613 | **R/W** demo: `L += 3` | id_rf.c:998, :1005 |
| ID_RF.C:1625, :1627 | **R/W** normal: `= newtime` (**unclipped**, even when MAXTICS clips) | id_rf.c:1013, :1021 |
| CK_PLAY.C:2202 | **W** `= 3` at level start | ck_play.c:2233 |
| CK_PLAY.C:779, :859, :876, :897 | **W** `= TimeCount` after status/menu/boss/debug (§6) | ck_play.c:1118, :1259, :1275 |
| CK_PLAY.C:2376 | **W** single-step | ck_play.c:2419 |
| CK_STATE.C:1366, :1401, :1436, :1473, :1514, :1548, :1597, :1640 | **R** per-tic loops `for (i = lasttimecount-tics; i < lasttimecount; i++)`: odd-tic gravity/accel/friction (ACTORS.md §5) | ck_phys.c:849-997 (7 loops) |
| K4_SPEC.C:1132 (`T_KeenSwimSlow`), K5_ACT3.C:917 (`T_Spindread`), :1933 (`T_Sphereful`), K6_ACT2.C:477 (`T_GikSlide`) | **R** actor-specific per-tic loops | ck4_misc.c:781-783, ck5_obj3.c:699-700, :1387-1389, ck6_obj2.c:251-252 |
| CK_KEEN.C:295, :299 | **R** `CheckGrabPole`: no re-grab within **19 tics of game clock** of leaving a pole | ck_keen.c:668-670 |
| CK_KEEN.C:1278 | **R** stores `leavepoletime = lasttimecount` | ck_keen.c:1762 |
| K4_ACT2.C:732-734 | **R** `InchContact`: `(Sint16)lasttimecount` as a per-frame ID to reset a counter | ck4_obj2.c:375-384 |
| K4_ACT2.C:1252 | **R** platform thruster frame `(lasttimecount>>2)&1` | ck4_obj2.c:659 |
| K5_ACT3.C:2003 | **R** Sphereful orbit sprite phase `(lasttimecount/8)&0xF` | ck5_obj3.c:1447 |
| CK_DEMO.C:437-439, :546-548, :997-998, :1810, :1852-1853 | **R/W** intro loops (reuse the variable) | ck_inter.c (see 2.1) |
| K5_SPEC.C:933, :1099, :1104 | **W** K5 `GameOver` pacing | ck5_misc.c:792, :801 |
| n/a | **W** fork only: `RFI_Frame` `+= 2` outside demos (§8) | rf_interp.c:409 |

### 2.3 `tics`

**Writers:** `RF_NewMap` `= 1` (ID_RF.C:459; id_rf.c:658), `RF_CalcTics` (ID_RF.C:1615, :1625,
:1640; id_rf.c:1007, :1013, :1028), `PlayLoop` `= 3` (CK_PLAY.C:2202; ck_play.c:2232), the intro
loops (CK_DEMO.C:546, :997-1000 capped at 8, :1810, :1852-1858 capped at 20; ck_inter.c:462,
:806-809, :1370, :1419-1426), and in the fork only `RFI_Frame` (rf_interp.c:408).

**Readers.** Every DOS site is listed below. Omnispeak reads it as `SD_GetSpriteSync()`: 100+ sites, one-for-one
with DOS (`git grep -n SD_GetSpriteSync main -- src`). Their behaviour at `tics=2` versus `tics>2` is analysed in ACTORS.md §8.

- **Engine:** `RFL_AnimateTiles` `anim->count -= tics` (ID_RF.C:943, *previous* frame's tics;
  id_rf.c:555). `StateMachine` → `DoActor(ob, tics)` (CK_STATE.C:1156; ck_play.c:628). The
  per-tic loops of §2.2. `T_Projectile`/`T_Velocity`-style `xtry = xspeed*tics` (CK_STATE.C:1691,
  :1706, :1720-1721). `R_Stunned` star timer (:1975).
- **PlayLoop:** inactive-object removal `US_RndT() < tics*2` (CK_PLAY.C:2234; ck_play.c:2272),
  `invincible -= tics` (:2359; ck_play.c:2403), K6 `groundslam -= tics` (:2366; ck_play.c:2411),
  fire repeat `oldfirecount += tics` (:1959; ck_play.c:1371), status-window scroll `vislines ±=
  tics*8` (:1310, :1330; ck_play.c:1888, :1912), look up/down `centerlevel ± tics` (:1571-1590;
  ck_play.c:2073-2099), vertical scroll speed `speed *= tics` (:1637; ck_play.c:2143).
- **Keen:** CK_KEEN.C:490, :579, :701, :812, :853, :859, :1040-1049, :1165-1171, :1196, :1640,
  :2184-2233. CK_KEEN2.C:637-639, :730-731, :812-813, :845-848, :961-962, :1298, :1544 (world map,
  teleport, elevator, flag).
- **Keen 4:** K4_ACT1.C:322, :1109-1115; K4_ACT2.C:277, :580, :1163-1164, :1335; K4_ACT3.C:408,
  :642-720, :766; K4_SPEC.C:1116, :1197, :1218-1229.
- **Keen 5:** K5_ACT1.C:583-584, :677-678, :799, :967, :1076, :1239; K5_ACT3.C:300, :405-519.
- **Keen 6:** K6_ACT1.C:623, :1310-1311, :1431, :1580; K6_ACT2.C:769, :841, :1034.

Probability-per-frame sites (`US_RndT() < tics*k`) are fair across frame rates only to first order.
Sites that count in frames rather than tics (`nothink`, per-frame thinks) are in ACTORS.md §8.

## 3. One frame at `tics=2` (VGA 70 Hz, fast machine)

`E` is the moment the previous `RF_CalcTics` returned. At that moment `TimeCount = lasttimecount = L`
and `tics = 2`. One PIT tic is 14.27 ms and one VGA refresh is 14.27 ms. The vblank phase `φ`
relative to the PIT edge drifts slowly (§1). The render time `w` is a few ms on anything from a 386
up, and in DOSBox-X.

```
 wall time ─►  0 (E)                    14.27 (E+1)                 28.54 (E+2)
 PIT/TimeCount │ L                         │ L+1                       │ L+2
 VGA vblank    │        ▼ v_k (φ)          │        ▼ v_k+1             │        ▼ v_k+2
               │                           │                            │
 CPU           ├─ PlayLoop tail of the previous iteration: invincible -= tics (:2357-2361),
               │  singlestep/extravbls (:2373-2384), CheckKeys (:2406)
               ├─ loop top: PollControls (one demo byte in demos)               CK_PLAY.C:2206
               ├─ sim with tics=2: think/move, odd-tic loops over [L-2, L)       :2211-2244
               │  riding, contacts, in-tiles, react (RF_PlaceSprite), camera, score   :2248-2345
               ├─ RF_Refresh: AnimateTiles(tics=2), UpdateTiles, EraseBlocks,
               │  UpdateSprites, refresh hook                       ID_RF.C:2335-2358
               ├─ VW_SetScreen → SyncVBL spin until "blanking, not yet retrace" ID_VW_AE.ASM:1027
               │        └─► CRTC start + pel pan written in the blank before v_k;
               │            latched at v_k: the new frame is first visible on refresh k+1
               ├─ clear update[], flip pages                       ID_RF.C:2370-2384
               └─ RF_CalcTics spin: while TimeCount - L < 2       ID_RF.C:1622-1626
                                                                            ▲
                                           returns at E+2: tics = 2, lasttimecount = L+2
```

Consequences:

- **The sim state shown at `v_k` is game time `L`.** It is computed after the fact over the two tics
  that have just elapsed, `[L-2, L)`, and stays on screen for 2 refreshes until the next flip at
  `v_k+2`. The state runs about 0.5-1 refresh behind real time.
- **The frame period is set by the PIT (2 tics). The flip only snaps to the next vblank.**
  The flip lands at the first vblank after `E + w`. While `φ` stays clear of `w`, every flip is exactly 2
  vblanks after the previous one. When the slow drift brings `φ` into the range that `w` jitters over,
  or the emulator's timing wobbles, a pair of frames is shown for 1 and 3 refreshes instead of 2 and 2.
  This matches the measured 91-99%.
- **`tics` measures the previous frame.** A slow frame (`w` over ~14 ms, past the vblank and the
  PIT edge) makes the *next* frame step 3 tics. This is the adaptive loop of ID_RF.md §7.
- `SyncVBL` gives up after `TimeCount` advances 3 (ID_VW_AE.ASM:1029-1036). If it never sees a
  qualifying blank, each frame costs up to 3 tics and `tics` rises to 3-5.

## 4. "Fix Jerky Motion" (`jerk`, `/JERK`)

**What it does:** it doubles the length of the vblank test in `SyncVBL`, and nothing else.

- The flag: `boolean jerk` (CK_MAIN.C:48). The `JERK` command-line parameter sets it (CK_MAIN.C:134-152),
  but `US_Startup` then overwrites it from `CONFIG.CK?` (ID_US_1.C:282, written at :349). The
  reconstruction's comment (CK_MAIN.C:140-144) notes that the parameter only works when there is no
  config file. The menu item toggles it (`USL_SmoothCustom`, ID_US_2.C:763-775; label :733).
- **The only reader is `SyncVBL` (ID_VW_AE.ASM:1054).** It reads Input Status 1 (`0x3DA`). Bit 0 is set
  while the display is blanked (horizontal *or* vertical blank). Bit 3 is set during vertical retrace.
  The routine waits for active display, then for the start of a blank. It then requires **5 consecutive
  reads** that show "blanked and not in vertical retrace" (:1046-1052), or **10 reads with `jerk`**
  (:1054-1063). Any failure restarts the wait. Interrupts are off while it polls, and are let in for
  one instruction per retry (:1032-1034).
- **Why it matters:** the CRTC start address latches at the start of vertical retrace, but pel panning
  takes effect immediately. `VW_SetScreen` writes both right after `SyncVBL` (ID_VW_AE.ASM:1079-1110).
  On the intended path, that is the vertical blank *before* retrace, so the start address and the fine
  pan change together on the next refresh. A **horizontal** blank lasts only a few microseconds. On a
  fast bus, 5 port reads can fit inside one, so `SyncVBL` mistakes it for vblank and flips mid-screen.
  The fine pan then changes on the next scanline while the start address waits for retrace. The lower
  part of that refresh is shown misaligned by up to 7 px: a one-refresh horizontal shake, the "jerky
  motion". Ten reads outlast an hblank. Interpretation from the asm; we did not reproduce it on
  hardware.
- **What it does not do:** it does not touch `tics`, `TimeCount`, MINTICS, the sim or demos. Its
  only timing effect is that a stricter test can wait longer, up to the 3-tic timeout (§3).
- **Omnispeak:** `ck_fixJerkyMotion` (ck_play.c:93-94) is a menu toggle (ck_us_2.c:68-75, :587,
  :606, :1382) that is saved in the config for DOS compatibility (id_us_1.c:754, :841). **Nothing reads
  it.** Presentation goes through the backend's vsync (`SDL_RENDERER_PRESENTVSYNC`,
  id_vl_sdl2.c:71, :418). The DOS backend id_vl_dos.c:819-941 has its own retrace waits.
- For our fork this is a no-op. Keep the config field for save/config compatibility. It does not belong in E9.

## 5. 60 Hz EGA versus 70 Hz VGA

The sim does not depend on the display: tics come from the PIT, so `tics=2` at 35.04 Hz on both. The
display cadence and every **vblank-counted** delay do depend on it.

| | VGA (mode 0Dh at 70.086 Hz) | EGA monitor (200-line, ~60 Hz) |
|---|---|---|
| refreshes per 2-tic sim frame | 2.0, so each frame is shown 2× (§3) | 1.71, a 2,2,1,2,2,2,1… pattern: 5 of 7 frames shown 2×, 2 of 7 shown 1× (33/17 ms) |
| demo frame (3 tics) | 3 refreshes | 2.57: 4 of 7 frames shown 3×, 3 of 7 shown 2× |
| `VW_WaitVBL(n)` (counts retrace starts, ID_VW_A.ASM:254-279) | n/70 s | n/60 s, **17% longer** |
| fades (`VW_FadeIn/Out/Up/Down`: 3-4 steps × `WaitVBL(6)`, ID_VW.C:281-350) | ~0.34 s | ~0.40 s |
| dialog pauses `WaitVBL(30/60)` (K4_SPEC.C:697-1002, K5_SPEC.C:1088-1178, K6_ACT1.C:437-852, K6_SPEC.C:814-882), teleport `WaitVBL(8)` (CK_KEEN2.C:917, :1012), fizzle (CK_DEMO.C:1185), text (CK_TEXT.C:188, :943, :956) | 70 Hz timing | 60 Hz timing |
| single-step `WaitVBL(14)` (CK_PLAY.C:2375) | 5 fps (the code comment says so) | 4.3 fps |
| `extravbls` debug (CK_PLAY.C:2381-2384, set by debug key :676, 0-8) | +n/70 s per frame; tics rise, game speed is kept | +n/60 s |

On EGA the 35 Hz sim beats against 60 Hz. The 2,2,1 hold pattern is a judder that the original game
always had on EGA monitors, and it adds to the 2-px grid. The fade at level start and the
`FadeAndUnhook` resync hide fade duration in both cases (CK_GAME.C:427). The WaitVBL waits are
outside `PlayLoop` timing, so they never change the sim.

`extravbls` works differently from single-step. Single-step resets `lasttimecount = TimeCount` after
its wait (CK_PLAY.C:2376), so the next `tics` excludes the wait and the game runs in slow motion.
`extravbls` does not reset it, so the wait inflates the next `tics`: the game keeps its speed at a
lower frame rate, which makes it a slow-machine simulator. **Omnispeak** implements every
`VL_DelayTics`/`WaitVBLs` as `SDL_Delay(vbls*1000/70)` (id_vl_sdl2.c:423-425), which is VGA timing
everywhere. The `ck_extraVBLs` debug key (ck_play.c:75, :1036-1045, :2422-2423) and slow motion
(:2416-2420) mirror DOS.

## 6. The flagged lines: `lasttimecount = TimeCount; // BUG: should be the other way around`

Real line numbers: **CK_PLAY.C:779, :859, :876, :897** (the issue's 777/857/874/895 are off by two).
All four are in `CheckKeys`, which `PlayLoop` calls after `RF_Refresh` (CK_PLAY.C:2406) and never in
demo playback (:2389-2403).

| line | trigger | what runs just before |
|---|---|---|
| :779 | Enter: status window | `StatusWindow` (its own refresh loops), `IN_ClearKeysDown`, **`RF_ForceRefresh`** |
| :859 | F2-F7/Esc: control panel | `US_ControlPanel`; **`RF_ForceRefresh`** unless a game was loaded or restarted (:842-849); then `playstate` may end the level |
| :876 | F9: boss key | text mode until Esc, mode reset, **`RF_ForceRefresh`** |
| :897 | F10+key: debug keys | `DebugKeys()` returned true, **`RF_ForceRefresh`** |

There is no such line after **pause** (:785-796) or **F1 help** (:802-815). Both end in
`RF_ForceRefresh` only.

**What actually happens after a menu of `M` tics (M > 5).** `RF_ForceRefresh` is `RF_NewPosition` +
two `RF_Refresh` calls (ID_RF.C:1278-1283). Each `RF_Refresh` ends in `RF_CalcTics`:

1. Refresh #1: `tics = M`. `lasttimecount` jumps to now (unclipped, ID_RF.C:1627). Then the MAXTICS
   clip sets `tics = 5` and moves `TimeCount` back by `M-5` (:1637-1641).
2. Refresh #2: `RFL_AnimateTiles` runs with **tics = 5**. Then `lasttimecount > TimeCount`, so the
   clamp lifts `TimeCount` back to `lasttimecount` (:1602-1603). The MINTICS spin waits 2 real tics,
   and `tics = 2`.
3. The flagged line runs microseconds later. `TimeCount - lasttimecount` is 0, or 1 if a PIT tick
   landed in between. **The line is effectively a no-op**, and so would be the literal swap
   `TimeCount = lasttimecount` in the same place.

So **there is no tic burst in the sim** after a menu, a pause or the status window. The next sim
frame steps `tics = 2`. The `StatusWindow` hide loop goes through the same clip and clamp: its first
hide refresh steps `vislines` by `5*8` (CK_PLAY.C:1327-1333). Level start has no burst either:
`PlayLoop` sets everything to 3 (CK_PLAY.C:2202), so the first live frame steps 3 tics. The level's
fade-in is excluded by `FadeAndUnhook` (CK_GAME.C:427), which runs inside the refresh, before that
refresh's `RF_CalcTics`. In-game dialogs (`PrincessLindsey` → CK_KEEN.C:1796, `RescueJanitor` →
:1769, K4_ACT3.C:1313, CK_KEEN2.C:1070, K6_ACT1.C:440-855) also end in `RF_ForceRefresh` and are
resynchronised the same way. **#84's premise ("tic burst after pause, menu, level start") does not
hold for the sim.**

**What is real (the visible and semantic residue):**

| effect | cause | visible symptom | sim-affecting? |
|---|---|---|---|
| **R1** background animations jump | refresh #2 animates with `tics=5` (and DOS's `while (count < 1)` catch-up, ID_RF.md §10.1) | the first two frames after any menu or dialog show animated tiles (water, lights, conveyors) skipping ~3 tics ahead | **marginally**: animated tile numbers are written into the map plane; K6 plays sounds from them |
| **R2** game clock jumps by the menu's wall time | `lasttimecount = newtime` unclipped (ID_RF.C:1627) | none directly; `lasttimecount`-phased sprite animations (K4 thrusters, K5 Sphereful) jump phase | **yes, in live play**: `CheckGrabPole`'s 19-tic lockout (CK_KEEN.C:295-302) expires during a pause, so Keen can re-grab a pole he just jumped off. Odd-tic loops are parity-safe at `tics=2` (exactly one odd tic per window) |
| **R3** a one-tic shift | the flagged line absorbs a PIT tick that landed after `CalcTics` | none | parity of `lasttimecount` can flip; harmless at `tics=2` |
| **R4** the MAXTICS double loss on genuine stalls | ID_RF.md §7 (clip, then charge) | after an in-play hitch of more than 5 tics, the game runs slow for up to 2 frames | yes, but only on slow or stalling machines; demos are immune (§7) |

**Proposed fix for #84** (enhanced/fix profile only; vanilla keeps all of this):

- **F1 (R1, R2): continuous game clock across interruptions.** Do this before every
  interruption-ending `RF_ForceRefresh`, in the fork's `CK_CheckKeys` (ck_play.c:1116-1118, :1149,
  :1168, :1246-1259, :1274-1275) and the dialog sites. Write
  `SD_SetTimeCount(SD_GetLastTimeCount())`, the `FadeAndUnhook` idiom (which is what the BUG comment
  means), and the two refreshes then step `tics=2` with no jump. A cleaner alternative is one
  "discontinuity" flag that the next `RFL_CalcTics` consumes: return `MINTICS` and set
  `TimeCount = lasttimecount + MINTICS`. **Sim-affecting** (R2's pole lockout, R1's map writes). It cannot change a demo
  trace, because `CheckKeys` does not run in playback and the demo branch of `CalcTics` ignores
  elapsed time. A dialog's `ForceRefresh` during a demo only changes how long the wait lasts.
- **F2 (R4): charge the clipped excess only once.** On a MAXTICS clip, pull `lasttimecount` back
  instead of pushing `TimeCount` back, so the next frame measures from the clipped point.
  KEEN70HZ made this change (§9). **Sim-affecting** on stalls only; it does not affect demos.
- The flagged lines themselves can stay. With F1 they become exact no-ops. The fork's own copy
  carries `// FIXME: Is this the right way to handle this?` (ck_play.c:1257-1259).
- Under interpolation (`RFI_Frame`) F1's behaviour is already what happens, because the clock is
  synthetic (§8). F1 is therefore mainly for enhanced *without* interpolation, and for making the
  difference explicit (§9 E9-1).

## 7. Demo timing

Covered in ID_RF.md §7 and TRACE_FORMAT.md. The facts that matter for timing:

- `DemoMode != demo_Off` (record, playback and `demo_PlayDone`) takes the lock-step branch
  (ID_RF.C:1605-1616). It waits for `TimeCount >= L+6`, then sets `L += 3`, `TimeCount = L+3` and
  `tics = DEMOTICS = 3` (ID_RF.H:41). A frame lasts `max(3 tics, render)`, so **23.36 fps**, with
  overruns discarded.
- The first frame: `PlayLoop` sets `TimeCount = lasttimecount = tics = 3`, the sim steps 3 tics, and the
  first `CalcTics` waits until `TimeCount = 9`. The first frame is therefore ~2× long in wall time and
  identical in sim time. `US_InitRndT(false)` (CK_PLAY.C:2196) fixes the RNG.
- `lasttimecount` advances 3 per frame, so its parity alternates and the odd-tic loops accelerate
  1,2,1,2… times per frame (ACTORS.md §5). The built-in demos therefore exercise `tics=3`, a
  different code path from live play at `tics=2` (ACTORS.md §8). Golden traces do not cover
  `tics=2` behaviour.
- Elapsed time never reaches the sim in demo mode: the clamp and the overrun discard absorb dumps,
  slow emulators and `ForceRefresh` waits. The demo-mode `TimeCount` read by world-map teleports
  (CK_KEEN2.C:762-818) is also deterministic at those points, because it is read straight after a
  `CalcTics`.
- The Omnispeak demo branch (id_rf.c:995-1009) is the same, with `rf_demoTics` (default 3, which
  must stay 3). `RFI_Frame` calls `RFL_CalcTics` in demos (rf_interp.c:399-404), so traces are
  unchanged with interpolation on.

## 8. The fork's enhanced path: how `rf_interp.c` times frames

`RF_Refresh` runs the whole vanilla pipeline, including the `rf_drawFunc` hook, then hands over to
`RFI_Frame` instead of `VL_Present` + `RFL_CalcTics` (id_rf.c:1461-1468; ID_RF.md §10).
`RFI_Frame` (rf_interp.c:367-428):

- `tics` is **2 outside demos, `rf_demoTics` in demos** (:371). The frame's wall duration is
  `dur = tics/70.0` s (:372), **exactly** 70 Hz, versus the PIT's 70.07-70.09 Hz (§1). The interpolated
  sim runs 0.10-0.13% slower than vanilla, which is imperceptible.
- **Pacing:** one master clock, `SDL_GetPerformanceCounter` (`Now()`, :107-112). It presents
  display-rate frames with `alpha = (Now-frameStart)/dur` until `alpha >= 1` (:378-387), then
  `frameStart += dur` (:388), so the cadence is drift-free. **Stall rule:** if more than **0.25 s** passed
  since `frameStart`, it resets to now (:376).
- **Clock outside demos** (:405-411): `tics = 2`, `lasttimecount += 2`, `TimeCount = lasttimecount`.
  `TimeCount` keeps running on the timer thread between frames, but is pulled back every frame.
  **No MINTICS spin, no MAXTICS clip, no wall-clock jumps.** `lasttimecount` is a synthetic game
  clock that advances exactly 2 per sim frame.
- **Clock in demos** (:399-404): vanilla `RFL_CalcTics` after the present loop. Two clocks pace that
  frame. Because `dur` (3/70 s) is longer than 3 PIT tics (3/70.088 s), `CalcTics` finds its target
  already reached and does not wait, so the perf-counter clock alone sets the pace and no drift builds up.
  With `sd_sdl_audioSync` the audio clock's tolerance (~100 ppm) is far smaller than this 1250 ppm
  margin, so the same holds.

What that means after interruptions, compared with vanilla (§6):

| situation | vanilla (DOS and fork with interp off) | `rf_interp` today |
|---|---|---|
| menu, pause, status window, dialog (≥ 0.25 s) | ForceRefresh: one `tics=5` refresh (R1), `lasttimecount` jumps by wall time (R2), then `tics=2` | every refresh `tics=2`, `lasttimecount` continuous (F1 semantics, implicitly); `frameStart` reset by the 0.25 s rule |
| status window hide | first step `vislines -= 40` | `-= 16` every frame (smooth) |
| hitch of 71-250 ms | one `tics=5` frame plus the next-frame charge (R4): time lost, one big step | **catch-up:** `alpha ≥ 1` at entry, so frames return **without presenting**; up to ~8 sim frames run back to back at `tics=2`; no time lost, the screen freezes and then jumps |
| hitch > 250 ms | as above (capped at 5 + charge) | time dropped (reset), no catch-up |
| render slower than 28.6 ms per frame | `tics` rises to 3-5, game speed kept | `tics` stays 2; lag accumulates until the 0.25 s reset, so presentation stutters in bursts and the game slows down |
| level start | `RF_NewMap` `tics=1`, `PlayLoop` `=3`, fade excluded | same sim values; the first frame steps 3 tics but presents for 2/70 s; `frameStart` reset by the 0.25 s rule (level load) |
| the flagged `SD_SetLastTimeCount(SD_GetTimeCount())` | no-op (§6) | `TimeCount` can be `lasttimecount`+0..1 by then, so it is +0..1 drift, harmless at `tics=2` |
| `TimeCount` readers in play (world-map teleport, `HandleDeath` blink) | wall clock | stepped game clock (moves +2 per sim frame) |

**Rule-3 caveat (AGENTS.md: "interpolation is presentation-only").** In demos `rf_interp` is exactly
vanilla. In live play its clock differs from vanilla wherever vanilla's `tics` would not be 2: R1, R2,
R4 and slow machines. On a fast machine with no interruptions the two are the same. None of this is
visible to the golden traces, but it is a sim difference and needs to be owned by a toggle (§9).

## 9. KEEN70HZ (juj/KEEN70HZ): what it changed, and its regressions

*Read for ideas only. The repository carries no licence file, so none of its code is copied here or in
the fork. What follows is our own description.* It is a 2023 mod of the same K1n9_Duk3
reconstruction, for Keen 4 v1.4 EGA shareware. Nearly all the work is one commit (`247a285`, Sep
2023). The changes:

1. **Clock locked to the display.** The ISR no longer increments `TimeCount` directly. It counts the raw
   PIT interrupts and derives `TimeCount` from them, rounded to the nearest whole tic. When the
   display was measured at ~70 Hz, every successful vblank wait snaps that fine counter to a tic
   boundary, so game tics are phase-locked to VGA refreshes and the slow beat of §1 cannot occur.
   On a CRT Terminator ISA card it reads the card's frame/vblank counter port and uses it as the
   clock directly. All writes to `TimeCount` go through a helper that disables interrupts, which fixes
   §1's torn-write hazard.
2. **Startup calibration.** At each graphics mode set it measures the refresh rate (10 retraces timed
   against a temporary 4 kHz PIT) and decides "70 Hz or not". It also measures the longest run of
   port reads that fits inside an hblank (+5 margin).
3. **`SyncVBL` and Fix Jerky Motion removed.** The vblank test uses the calibrated length instead of
   5 or 10 reads (§4), so the option is gone. The CRTC start and pel pan are no longer written before
   the flip. `VW_SetScreen` from `RF_Refresh` only *queues* them, and they are written inside the vblank
   wait. `VW_WaitVBL` became the frame's pacing primitive and keeps the 3-tic give-up.
4. **`RF_CalcTics` waits on vblanks, not the PIT spin.** It waits until 2 (35 Hz) or 1 (70 Hz) refreshes after
   the last frame, clamped to 1-2 waits, then measures `tics`. **`MINTICS` changed 2 → 1** globally.
   The demo branch also waits by vblank but still returns `tics=3`, so demos stay deterministic.
5. **MAXTICS clip moved to `lasttimecount`.** The excess is dropped once and not charged to the next
   frame (our F2).
6. **New options:** **Full Frame Rate** (70 Hz sim, `tics=1`) and **1 Pixel Panning**
   (`xpanmask` 6 → 7). The JERK parameter and the jerk config byte were replaced by these two, so the
   config file is not compatible. Intro, title and `TimedPicCommand` loops were moved to vblank waits.
   On a 60 Hz display it falls back to the old decoupled behaviour.

**Known regressions (from its README, with our analysis):**

- **Keen misses ledge grabs at Full Frame Rate.** Horizontal acceleration happens only on odd tics
  (ACTORS.md §5), and hitting a wall mid-jump zeroes `xspeed`. With `tics=1` a frame can have
  `xtry = 0`, so `hiteast`/`hitwest` are not set and the ledge test fails. Omnispeak already has
  K1n9_Duk3's fix behind `ck_fix70HzLedgeGrabbing`: force a 1-unit `xtry` when `tics < 2`
  (src/ck_keen.c:1125-1141). It is inert at `tics>=2`.
- **Keen gets stuck on Bounders (the red bouncing balls, first level) at Full Frame Rate.** Not
  analysed. Likely another frame-versus-tic effect in `ClipToSprite`/riding at `tics=1` (bounderobj
  contact, CK_KEEN.C:1778-1792; ACTORS.md §8).
- **1 Pixel Panning: Keen and the score box stutter.** The background pans at 1 px, but sprites still
  draw on the 2-px grid of absolute map pixels (ID_RF.md §2). The score box is a sprite placed in map
  coordinates every frame, so on odd pans it lands ±1 px from where it should be. Keen, followed by
  the camera, wobbles the same way.
- The README notes that Full Frame Rate needs a ≥70 Hz display in DOSBox-X to look smooth.

**Takeaways:** a 70 Hz *sim* breaks gameplay (this is why #80 was rejected). Locking the clock's phase
to vblank (1) and the single-charge clip (5) are worth having. They are presentation and robustness
changes, not sim-rate changes. We do not need (1) in the fork because `rf_interp` already uses one
master clock. 1-px panning without moving sprites to the same grid is the failure mode that our
camera/sprite interpolation (#75-#77) must avoid.

## 10. Implications for E8 and E9

**E8: presentation (#74-#79)**

- **E8-1: keep the sim at `tics=2`, 35 Hz, fixed.** `rf_interp` does this already. Do not reintroduce
  MINTICS=1 (§9). The vanilla profile must keep calling `RFL_CalcTics` untouched. The enhanced
  profile may own its clock, as `RFI_Frame` does.
- **E8-2: replace the 0.25 s heuristic with explicit discontinuity points.** At `RF_NewMap`,
  `PlayLoop` start, the end of each `CK_CheckKeys` interruption, dialog `ForceRefresh`es and
  `RF_Reposition`, reset `frameStart` and snap `prev = cur` for the camera and every sprite.
  Today only `SNAP_UNITS` (768 units = 48 px, rf_interp.c:74) and the 0.25 s rule cover these.
- **E8-3: define a catch-up budget for 71-250 ms hitches.** Today these hitches freeze the display and
  then fast-forward up to ~8 sim frames without presenting. Proposal: catch up at most
  `MAXTICS-MINTICS = 3` tics' worth (1 sim frame) and drop the rest, which is vanilla's own policy,
  and always present the final state of a catch-up. If rendering is persistently slower than the sim
  (§8 table), present at least one frame per sim frame. Do not let lag build up to the reset.
- **E8-4: `dur = tics/70.0` versus the PIT's 70.07-70.09 Hz.** Either is fine. If the enhanced
  sim must match the vanilla wall speed exactly (side-by-side A/B #51), use the active music
  mode's rate: `dur = tics × 2×8514/1193182` s (music off, 70.072 Hz) or `tics × 8×2128/1193182` s
  (AdLib music, 70.088 Hz).
- **E8-5: display rates without interpolation** (the vanilla presentation the user compares against).
  At 70 Hz the cadence is 2 refreshes per frame. At 60 Hz it is 2,2,1 (EGA-like, §5). At 120 Hz it is a
  3/4 mix (4 of 7 frames held 3 refreshes, 3 of 7 held 4), and at 144 Hz mostly 4 with a 5 about every
  9th frame. **On VRR, a "70 Hz VGA" mode** that presents each sim frame for
  exactly 2/70 s reproduces the original VGA look exactly. This is cheap and worth having as a
  reference in #79.
- **E8-6: everything vblank-counted in DOS is fixed at 70 Hz in the fork** (id_vl_sdl2.c:423-425):
  fades, dialog pauses, teleport `WaitVBL(8)`. #78 should keep those at their 70 Hz durations and
  render them at display rate. It should not count display vblanks.
- **E8-7: tile animation (#78).** `RFL_AnimateTiles` must keep running once per sim frame with the sim's
  `tics`, because it writes the map plane. Only its *presentation* can be smoothed. Under enhanced
  F1 it never sees `tics=5`.
- **E8-8: the status window** (`rf_drawFunc`) is composed onto the vanilla screen, which
  `rf_interp` bypasses (ID_RF.md §10). The 35 Hz `vislines` step should be interpolated like a sprite.

**E9: sim-side fixes (#84-#88), each its own toggle (#83)**

- **E9-1 (#84): re-scope.** There is no tic burst (§6). The real items are F1 (continuous clock
  across interruptions: R1 animated-tile jump, R2 pole lockout and animation phase) and F2
  (single-charge MAXTICS). Both are sim-affecting in live play and invisible to demo traces.
  `rf_interp` implicitly does F1 and goes further: it has no MAXTICS at all and always uses `tics=2`.
  When the #84 toggle is off and interpolation is on, either let `RFI_Frame` emulate vanilla's
  `CalcTics` on its synthetic clock, or document in #83 that "interpolation on" implies "#84 on" for
  live play.
- **E9-2 (#88, uneven animation durations):** with the enhanced clock fixed at `tics=2`, the
  `tics`-dependent state traversal differences (ACTORS.md §8) collapse to the single `tics=2` column.
  That makes them reproducible in live play. Demos still run `tics=3`, so verification of #88 needs a
  `tics=2` input-replay harness, not the built-in demos.
- **E9-3:** the R2 pole-lockout difference is the one place where a pause changes gameplay. Mention it in
  the #51 A/B notes so testers don't misread it as a fork bug.
- **E9-4:** `ck_fixJerkyMotion` is a no-op. Leave it alone and do not count it as a jitter fix.
