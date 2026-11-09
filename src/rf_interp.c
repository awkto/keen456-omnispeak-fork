/*
Omnispeak: A Commander Keen Reimplementation
Copyright (C) 2026 keen456 contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 * rf_interp.c -- display-rate interpolation over an untouched 35 Hz simulation
 * (keen456-decomp #89 spike, productised in #92).
 *
 * The simulation is not modified. After each sim frame we hold (prev, cur)
 * camera + sprite positions and, for the wall-clock duration of that sim frame,
 * present frames at the display's rate, lerping prev->cur. Costs one sim frame
 * (28.6 ms) of latency.
 *
 * Off by default. Enabled with the 'interpolation' config option or /INTERP
 * on the command line (see README). Modes, cycled live with F12 when enabled:
 *   grid      alpha=1, camera X and sprite X on the 2-px EGA grid -- what Keen draws
 *   pixel     interpolated, positions snapped to whole 320x200 pixels
 *   subpixel  interpolated, positions snapped to 1/4 pixel of a 1280x800 frame
 *
 * DEBUG TOOLING (not a user feature; read only when interpolation is enabled):
 *   OMNI_DEBUG_INTERP_SPRITES=<file.csv>  per display frame, per sprite: interpolated screen
 *       position, prev/cur object position and Keen's action (keen456-decomp #76 snap audit).
 *   OMNI_DEBUG_INTERP_CAPTURE=<file>  headless, deterministic capture: raw RGB24
 *       1280x800 frames on a virtual display, time advancing exactly 1/Hz per
 *       frame; RNG seeding is made deterministic.
 *   OMNI_DEBUG_INTERP_HZ=<n>          virtual display rate for the capture (60); in realtime,
 *       caps the present rate to <n> Hz (as when there is no vsync).
 *   OMNI_DEBUG_INTERP_SKIP / _FRAMES  bound the recording; exits when done.
 *   OMNI_DEBUG_INTERP_LOG=<file.csv>  per-display-frame camera/Keen trace (capture only).
 *   OMNI_DEBUG_INTERP_SCRIPT="right:0-600,jump:80-90,..."  inject input by sim frame.
 *   OMNI_DEBUG_INTERP_WARP="sim,tx,ty"  put Keen at a tile at that sim frame (reach map edges, #103).
 *   OMNI_DEBUG_INTERP_STATS=1         frame-time/compose stats to stderr + window title.
 *   OMNI_DEBUG_INTERP_PRESENTS=<file.csv>  one line per presented frame, for the cadence
 *       bench (keen456-decomp #30): host time after the present returned (us since the
 *       first present; virtual time in a capture), the gap to the previous present, the
 *       sim frame shown (interpolating from sim-1 to sim) and alpha.
 *
 * PACING: presents block on vsync where the backend has it. Without vsync
 * (swapInterval 0, or presents that don't block or outrun the refresh rate), presents are paced to
 * the display mode's refresh rate (60 if unknown) by sleeping, never spinning.
 * After a stall (window drag, breakpoint, slow frame) the sim catches up by at
 * most rf_maxTics tics, like vanilla's RFL_CalcTics; anything longer is dropped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "id_ca.h"
#include "id_in.h"
#include "id_rf.h"
#include "id_sd.h"
#include "id_ti.h"
#include "id_us.h"
#include "id_vh.h"
#include "id_vl.h"
#include "id_vl_private.h"
#include "ck_cross.h"
#include "ck_fix.h"
#include "rf_interp.h"
#include "id_vl_filter.h"
#include "ck_act.h"
#include "ck_def.h"
#include "ck_play.h"
#include "ck_phys.h"

#define S 4 /* output scale: sub-pixel quantum is 1/S px = 16/S units */
#define RF_NUM_SPRITE_Z_LAYERS 4
/* #103: frames are viewW game pixels wide (320, or wider in widescreen); buffers fit the widest */
#define OUT_MAX_W (VL_WIDE_MAX_W * S)
#define OUT_H (200 * S)
#define LAYER_TW (VL_WIDE_MAX_W / 16 + 3)
#define LAYER_TH 16
#define LAYER_W (LAYER_TW * 16)
#define LAYER_H (LAYER_TH * 16)
#define KEY 0xFF
#define SNAP_UNITS (48 * 16) /* larger jumps are teleports: never interpolated */

extern int rf_scrollXUnit, rf_scrollYUnit;
extern RF_SpriteDrawEntry *rf_firstSpriteTableEntry[RF_NUM_SPRITE_Z_LAYERS];
extern int rf_demoTics;
extern int rf_maxTics;
void RFL_CalcTics(void);
void VL_PresentARGB(const uint32_t *argb, int w, int h, const char *title);
void VL_FlipPageNoPresent(void);
#ifdef __EMSCRIPTEN__
void VL_WaitPresentSlot(void);
double VL_WaitRAF(void);
#endif
/* filled in by the renderer's VL_PresentARGB where it can (id_vl_sdl2.c), for the present log */
double vl_presentWorkS, vl_presentWaitS, vl_presentRafMs;
int vl_presentInRaf;

static int mode = RFI_Mode_Off, hz = 60;
static bool enabled, debugStats, hzForced, fullyStarted;
static FILE *presentLog;
static bool logsAtexit;
static double presentT0 = -1, nextPresent;
static int fastPresents;
static bool noVsync;
static FILE *capFile;
static FILE *debugFrameFile; /* keen456-decomp #75: OMNI_DEBUG_FRAMES, interpolated */
static bool debugFrameWarned;
static long capSkip, capFrames = -1, dispFrame, simFrame;
static double frameStart = -1.0, vnow;
static int prevCamX, prevCamY;
/* keen456-decomp #86: camera-smoothing state (displayed camera D, velocity V, last target T; units) */
static bool smoothValid;
static int smoothMode = -1;
static double smoothD[2], smoothV[2], smoothT[2], smoothTime;
#define SMOOTH_TAU 0.050
#define SMOOTH_PULL 0.100
#define SMOOTH_MAX_DEV (6 * 16)
static bool havePrev;
static const CK_action *lastKeenAct; /* #76: Keen's action at the previous sim frame */
static uint8_t layerBack[LAYER_W * LAYER_H], layerFore[LAYER_W * LAYER_H];
static uint8_t out8[OUT_MAX_W * OUT_H];
static uint32_t out32[OUT_MAX_W * OUT_H];
static uint8_t out24[OUT_MAX_W * OUT_H * 3];
/* keen456-omnispeak-fork #104: X-ray. Where a hidden-passage tile (RF_IsXrayTile) would cover the
 * picture, out8 keeps what is beneath it (background, sprites below the fore layer) and outX holds
 * XRAY_PX | the tile's colour; the palette expansion blends the two and adds a faint tint. A sprite
 * drawn over the fore layer clears outX. outX is only read while xrayFrame is set. */
#define XRAY_PX 0x10
#define XRAY_LAYER 0x20 /* layerFore: XRAY_LAYER | colour marks a see-through pixel (KEY is 0xFF) */
static uint8_t outX[OUT_MAX_W * OUT_H];
static bool xrayFrame;
/* #103: width of the frame being composed (OUT_W) and where the vanilla 320-px camera window
 * lies in it (game pixels from its left edge; 0 when not wide). */
static int outW = 320 * S, viewW = 320, viewOffX;
#define OUT_W outW
/* #103: widescreen scene state for presents outside RFI_Frame (RFI_PresentWideStill):
 * wideValid while the last game frame was wide and the screen still shows the game;
 * sbX0..sbX1/sbY0..sbY1 = the scorebox's rectangle in out8 (empty if none). */
static bool wideValid;
static int sbX0, sbX1, sbY0, sbY1;
const char *rfi_modeStrings[] = {"off", "grid", "pixel", "subpixel", NULL};

/* stats */
static double lastPresent = -1, statSum, statMin = 1e9, statMax, statT0;
static int statN;
static double composeSum;
static FILE *logFile;
static FILE *spriteLog; /* #76: OMNI_DEBUG_INTERP_SPRITES, per display frame per sprite */
static int logCamY;
static int logCamX, logKeenX, logKeenWorldX, logKeenY, logPlatX, logPlatY;
static bool logPlat;
static int logDevX, logDevY;
static int logSBX, logSBY; /* scorebox screen position (quarter pixels); -99999 if not drawn */
static double logAlpha;
static double logComposeS, logGapS, lastPresentEnd = -1; /* OMNI_DEBUG_INTERP_PRESENTS timing */

typedef struct { int key, from, to; } ScriptEv;
static ScriptEv script[64];
static int scriptN;

static double Now(void)
{
	if (capFile)
		return vnow;
#ifdef __EMSCRIPTEN__
	/* SDL2's emscripten port has no usable monotonic clock (CLOCK_MONOTONIC_RAW is
	 * rejected), so SDL_GetPerformanceCounter falls back to gettimeofday: Date.now(),
	 * whole milliseconds of wall-clock time. Use performance.now(), the clock rAF
	 * timestamps and the game's TimeCount (id_sd.c) use too (keen456-decomp #98). */
	return emscripten_get_now() / 1000.0;
#endif
	return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void ParseScript(const char *s)
{
	while (s && *s && scriptN < 64)
	{
		char name[16]; int a, b, n = 0;
		if (sscanf(s, "%15[a-z]:%d-%d%n", name, &a, &b, &n) < 3) break;
		int key = !strcmp(name, "right") ? IN_SC_RightArrow : !strcmp(name, "left") ? IN_SC_LeftArrow :
			!strcmp(name, "up") ? IN_SC_UpArrow : !strcmp(name, "down") ? IN_SC_DownArrow :
			!strcmp(name, "jump") ? IN_SC_Control : !strcmp(name, "pogo") ? IN_SC_Alt :
			!strcmp(name, "fire") ? IN_SC_Space : !strcmp(name, "enter") ? IN_SC_Enter : 0;
		if (key) script[scriptN++] = (ScriptEv){key, a, b};
		s += n; if (*s == ',') s++;
	}
}

/* Close the debug logs at exit, so they are flushed and closed on every path
 * out of the program, not only when a capture runs to completion.
 * keen456-decomp #74 */
static void RFI_CloseLogs(void)
{
	if (logFile) { fclose(logFile); logFile = NULL; }
	if (presentLog) { fclose(presentLog); presentLog = NULL; }
	if (spriteLog) { fclose(spriteLog); spriteLog = NULL; }
	if (debugFrameFile) { fclose(debugFrameFile); debugFrameFile = NULL; }
}

void RFI_Startup(RFI_Mode m)
{
	const char *e;
	mode = m;
	enabled = (m != RFI_Mode_Off);
	if (!enabled)
		return;
	fullyStarted = true;
	if ((e = getenv("OMNI_DEBUG_INTERP_HZ")) && atoi(e) > 0)
	{
		hz = atoi(e);
		hzForced = true;
	}
	else if (!getenv("OMNI_DEBUG_INTERP_CAPTURE"))
	{
		/* pace to the display when there is no vsync; a capture stays at 60 */
		SDL_DisplayMode dm;
		if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.refresh_rate > 0)
			hz = dm.refresh_rate;
	}
	if ((e = getenv("OMNI_DEBUG_INTERP_SKIP")))
		capSkip = atol(e);
	if ((e = getenv("OMNI_DEBUG_INTERP_FRAMES")))
		capFrames = atol(e);
	debugStats = (e = getenv("OMNI_DEBUG_INTERP_STATS")) && atoi(e);
	ParseScript(getenv("OMNI_DEBUG_INTERP_SCRIPT"));
	if ((e = getenv("OMNI_DEBUG_INTERP_LOG")))
	{
		logFile = fopen(e, "w");
		if (logFile)
		{
			fprintf(logFile, "disp,sim,alpha,cam_x_qpx,keen_world_x_qpx,keen_screen_x_qpx,keen_screen_y,plat_screen_x,plat_screen_y,cam_y_qpx,sim_cam_x_u,sim_cam_y_u,cam_dev_x_u,cam_dev_y_u,scorebox_x_qpx,scorebox_y_qpx\n");
			if (!logsAtexit) { atexit(RFI_CloseLogs); logsAtexit = true; }
		}
	}
	if ((e = getenv("OMNI_DEBUG_INTERP_SPRITES")))
	{
		spriteLog = fopen(e, "w");
		if (spriteLog)
		{
			fprintf(spriteLog, "disp,sim,alpha,cam_qpx_x,cam_qpx_y,sde,z,chunk,prev_ux,prev_uy,cur_ux,cur_uy,scr_qpx_x,scr_qpx_y,keen_act\n");
			if (!logsAtexit) { atexit(RFI_CloseLogs); logsAtexit = true; }
		}
	}
	if ((e = getenv("OMNI_DEBUG_INTERP_PRESENTS")))
	{
		presentLog = fopen(e, "w");
		if (presentLog)
		{
			fprintf(presentLog, "present,host_us,dt_us,sim,alpha,compose_us,work_us,wait_us,gap_us,raf_lag_us,in_raf\n");
			if (!logsAtexit) { atexit(RFI_CloseLogs); logsAtexit = true; }
		}
	}
	if ((e = getenv("OMNI_DEBUG_INTERP_CAPTURE")))
	{
		capFile = fopen(e, "wb");
		if (!capFile)
			QuitF("Couldn't open interpolation capture file \"%s\"", e);
	}
	/* keen456-decomp #75: matching per-frame dump for the interpolated renderer. */
	if ((e = getenv("OMNI_DEBUG_FRAMES")) && e[0])
	{
		debugFrameFile = fopen(e, "wb");
		if (debugFrameFile && !logsAtexit) { atexit(RFI_CloseLogs); logsAtexit = true; }
	}
	CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Interpolation: %s (%s, hz=%d, script events=%d)\n", rfi_modeStrings[mode],
		capFile ? "debug capture" : "realtime", hz, scriptN);
}

bool RFI_Enabled(void)
{
	return enabled;
}

RFI_Mode RFI_GetMode(void)
{
	return (RFI_Mode)mode;
}

void RFI_SetMode(RFI_Mode m)
{
	if (m == RFI_Mode_Off)
	{
		enabled = false;
		mode = RFI_Mode_Off;
		return;
	}
	if (!fullyStarted)
		RFI_Startup(m); // first time on: full init (display rate etc.)
	enabled = true;
	mode = m;
	RFI_Reset();
}

// Debug capture only: make level RNG seeding deterministic.
bool RFI_DebugDeterministic(void)
{
	return enabled && capFile;
}

void RFI_NoteSprite(RF_SpriteDrawEntry *sde, int unitX, int unitY, bool fresh)
{
	/* keen456-decomp #76: sprite-frame (chunk) changes are NOT a reason to snap:
	 * we interpolate the object's position and apply each chunk's own origin
	 * at compose time, so walk cycles stay smooth. */
	sde->curUX = unitX;
	sde->curUY = unitY;
	if (fresh)
	{
		sde->prevUX = unitX;
		sde->prevUY = unitY;
	}
}

/* keen456-decomp #76: called when a new map/level is set up for rendering.
 * Drop the interpolation history so the next composed frame snaps the camera
 * and every sprite into place instead of interpolating across the level load. */
void RFI_Reset(void)
{
	havePrev = false;
	smoothValid = false;
	lastKeenAct = NULL;
	for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
		for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
		{
			sde->prevUX = sde->curUX;
			sde->prevUY = sde->curUY;
		}
}

/* keen456-decomp #76: leaving the door state (CK_ACT_keenEnterDoor4). The think function warps Keen to
 * the door's destination and recentres the camera. Warps over SNAP_UNITS already snap by distance;
 * this also snaps short ones, for the camera and every sprite together (a short warp would otherwise
 * show the whole view sliding for a frame or two). Called once per sim frame after the sim, so cur
 * is already the new position.
 *
 * Deliberately NOT snapped (audited, see docs/INTERP.md): pole grabs, ledge grabs/pulls, and sprite
 * frame changes. The camera follows Keen, so a lerped position change is absorbed by the lerped
 * camera and Keen stays put on screen; snapping Keen alone would make him slide against the view. */
static void SnapOnKeenTransition(void)
{
	const CK_action *a = (ck_keenObj && ck_keenObj->sde) ? ck_keenObj->currentAction : NULL;
	const CK_action *was = lastKeenAct;
	lastKeenAct = a;
	if (!a || !was || a == was || was != CK_ACTION(CK_ACT_keenEnterDoor4))
		return;
	prevCamX = rf_scrollXUnit; prevCamY = rf_scrollYUnit;
	smoothValid = false;
	for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
		for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
		{
			sde->prevUX = sde->curUX;
			sde->prevUY = sde->curUY;
		}
	if (spriteLog) fprintf(spriteLog, "# snap door-exit sim %ld\n", simFrame);
}

static inline int FloorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static inline int Quant(int u, int q) { return FloorDiv(u, q) * q; }
static inline int Lerp(int a, int b, double t)
{
	if (abs(b - a) > SNAP_UNITS) return b;
	return a + (int)floor((double)(b - a) * t + 0.5);
}

static long layersSim = -1; static int layersTX, layersTY; static bool layersXray;
static void BuildLayers(int originTX, int originTY)
{
	if (layersSim == simFrame && layersTX == originTX && layersTY == originTY && layersXray == xrayFrame) return;
	layersSim = simFrame; layersTX = originTX; layersTY = originTY; layersXray = xrayFrame;
	int mw = CA_GetMapWidth(), mh = CA_GetMapHeight();
	memset(layerBack, 0, sizeof(layerBack));
	memset(layerFore, KEY, sizeof(layerFore));
	for (int ty = 0; ty < LAYER_TH; ++ty)
		for (int tx = 0; tx < LAYER_TW; ++tx)
		{
			int mx = originTX + tx, my = originTY + ty;
			if (mx < 0 || my < 0 || mx >= mw || my >= mh) continue;
			/* #103: animated tiles outside RF's port animate too (identical inside it) */
			int bg = RF_DisplayTileAt(mx, my, 0), fg = RF_DisplayTileAt(mx, my, 1);
			void *src = CA_GetGrChunk(ca_gfxInfoE.offTiles16, bg, "Tile16", false);
			if (src) VL_UnmaskedToPAL8(src, layerBack, tx * 16, ty * 16, LAYER_W, 16, 16);
			if (!fg) continue;
			uint8_t *m = (uint8_t *)CA_GetGrChunk(ca_gfxInfoE.offTiles16m, fg, "Tile16m", false);
			if (!m) continue;
			bool pri = (TI_ForeMisc(fg) & 0x80) != 0;
			bool xr = xrayFrame && RF_IsXrayTile(fg); /* #104: only in the fore layer, see-through */
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
				{
					int off = (y * 16 + x) >> 3, bit = 1 << (7 - ((y * 16 + x) & 7));
					if (m[off] & bit) continue; /* mask set = transparent */
					int px = ((m[off + 128] & bit) ? 8 : 0) | ((m[off + 96] & bit) ? 4 : 0) |
						((m[off + 64] & bit) ? 2 : 0) | ((m[off + 32] & bit) ? 1 : 0);
					int di = (ty * 16 + y) * LAYER_W + tx * 16 + x;
					if (xr) { layerFore[di] = XRAY_LAYER | px; continue; }
					layerBack[di] = px;
					if (pri) layerFore[di] = px;
				}
		}
}

/* Sample a 1x layer into the SxS output; (offX, offY) = camera minus layer origin, in output pixels. */
static void BlitLayer(const uint8_t *layer, int offX, int offY, bool keyed)
{
	static int lxTab[OUT_MAX_W];
	for (int x = 0; x < OUT_W; ++x)
	{
		int lx = (x + offX) / S;
		lxTab[x] = (lx < 0 || lx >= LAYER_W) ? -1 : lx;
	}
	for (int y = 0; y < OUT_H; ++y)
	{
		int ly = (y + offY) / S;
		if (ly < 0 || ly >= LAYER_H) continue;
		uint8_t *dst = out8 + y * OUT_W;
		if (y > 0 && (y - 1 + offY) / S == ly && !keyed)
		{
			memcpy(dst, dst - OUT_W, OUT_W); /* same source row as the line above */
			continue;
		}
		const uint8_t *row = layer + ly * LAYER_W;
		if (!keyed)
			for (int x = 0; x < OUT_W; ++x) { if (lxTab[x] >= 0) dst[x] = row[lxTab[x]]; }
		else if (!xrayFrame)
			for (int x = 0; x < OUT_W; ++x) { if (lxTab[x] >= 0 && row[lxTab[x]] != KEY) dst[x] = row[lxTab[x]]; }
		else
		{
			uint8_t *dx = outX + y * OUT_W;
			for (int x = 0; x < OUT_W; ++x)
			{
				if (lxTab[x] < 0) continue;
				uint8_t v = row[lxTab[x]];
				if (v == KEY) continue;
				if (v & XRAY_LAYER) dx[x] = XRAY_PX | (v & 15);
				else dst[x] = v;
			}
		}
	}
}

static void BlitSprite(RF_SpriteDrawEntry *sde, int ox, int oy)
{
	int spriteNumber = sde->chunk - ca_gfxInfoE.offSprites;
	VH_SpriteTableEntry ste = VH_GetSpriteTableEntry(spriteNumber);
	VH_ShiftedSprite *sh = VH_GetShiftedSprite(sde->chunk);
	if (!sh) return;
	uint8_t *d = &sh->data[sh->sprShiftOffset[0]];
	int w = sh->sprShiftByteWidths[0] * 8, h = ste.height, plane = (w / 8) * h;
	for (int sy = 0; sy < h; ++sy)
	{
		int y0 = oy + sy * S;
		if (y0 + S <= 0 || y0 >= OUT_H) continue;
		for (int sx = 0; sx < w; ++sx)
		{
			int off = (sy * w + sx) >> 3, bit = 1 << (7 - ((sy * w + sx) & 7));
			if (d[off] & bit) continue;
			int x0 = ox + sx * S;
			if (x0 + S <= 0 || x0 >= OUT_W) continue;
			int px = sde->maskOnly ? 15 : (((d[off + 4 * plane] & bit) ? 8 : 0) | ((d[off + 3 * plane] & bit) ? 4 : 0) |
				((d[off + 2 * plane] & bit) ? 2 : 0) | ((d[off + plane] & bit) ? 1 : 0));
			for (int yy = CK_Cross_max(y0, 0); yy < CK_Cross_min(y0 + S, OUT_H); ++yy)
				for (int xx = CK_Cross_max(x0, 0); xx < CK_Cross_min(x0 + S, OUT_W); ++xx)
				{
					out8[yy * OUT_W + xx] = px;
					outX[yy * OUT_W + xx] = 0; /* #104: drawn over the fore layer */
				}
		}
	}
}

/* keen456-decomp #75: interpolated counterpart of RF_DebugDumpFrame
 * (src/id_vl.c). Appends one 64000-byte 320x200 frame of EGA palette indices
 * per sim frame, downsampling the SxS composited out8 by taking pixel (x*S, y*S)
 * for x<320, y<200 and masking to 0-15.
 *
 * Only meaningful in grid mode, where alpha is forced to 1 so the composed image
 * is exactly the current sim frame; in pixel/subpixel modes it is a blend, so
 * warn once on stderr and dump nothing. Called at the end of the first Compose()
 * of each RFI_Frame call, i.e. after out8 is fully composed for that display
 * frame (sprites included). That is one dump per sim frame, matching vanilla's
 * one dump per RF_Refresh.
 *
 * OFF-BY-ONE RISK: the dump is tagged with the simFrame counter value *before*
 * RFI_Frame increments it, so the first dumped image carries simFrame 0 while
 * vanilla's first dump is frame 1; compare the two files from a common starting
 * frame, not by absolute index. After a level load RFI_Reset() drops the history
 * and the first frame has prev==cur, so it still matches vanilla, but a skipped
 * present at a transition can shift the two runs by one frame. */
static void RFI_DebugDumpFrame(void)
{
	if (!debugFrameFile)
		return;
	if (mode != RFI_Mode_Grid)
	{
		if (!debugFrameWarned)
		{
			debugFrameWarned = true;
			fprintf(stderr, "[interp] OMNI_DEBUG_FRAMES needs grid mode (alpha=1); mode is \"%s\", dumping nothing\n",
				rfi_modeStrings[mode]);
		}
		return;
	}
	uint8_t row[320];
	for (int y = 0; y < 200; ++y)
	{
		for (int x = 0; x < 320; ++x)
			row[x] = (uint8_t)(out8[(y * S) * OUT_W + (viewOffX + x) * S] & 0xF);
		fwrite(row, 1, 320, debugFrameFile);
	}
}

/* keen456-decomp #86: opt-in presentation-only camera smoothing. Returns the displayed camera
 * for one axis pair from the raw interpolated target T. See docs/INTERP.md. */
static void SmoothCamera(double tx, double ty, int *outX, int *outY)
{
	double T[2] = {tx, ty};
	bool on = CK_FIX(CameraSmoothing) && mode != RFI_Mode_Grid;
	double t = Now();
	if (!on || smoothMode != mode)
		smoothValid = false;
	smoothMode = on ? mode : -1;
	if (!on) { logDevX = logDevY = 0; *outX = (int)floor(tx + 0.5); *outY = (int)floor(ty + 0.5); return; }
	double dt = t - smoothTime;
	smoothTime = t;
	if (smoothValid && (dt <= 0 || dt > 0.25 || fabs(T[0] - smoothT[0]) > SNAP_UNITS || fabs(T[1] - smoothT[1]) > SNAP_UNITS))
		smoothValid = false;
	if (!smoothValid)
	{
		for (int i = 0; i < 2; ++i) { smoothD[i] = T[i]; smoothV[i] = 0; smoothT[i] = T[i]; }
		smoothValid = true;
	}
	else
		for (int i = 0; i < 2; ++i)
		{
			double tv = (T[i] - smoothT[i]) / dt;
			smoothT[i] = T[i];
			double k = 1.0 - exp(-dt / SMOOTH_TAU);
			smoothV[i] += (tv - smoothV[i]) * k;
			smoothD[i] += smoothV[i] * dt;
			double pull = dt / SMOOTH_PULL;
			smoothD[i] += (T[i] - smoothD[i]) * (pull > 1.0 ? 1.0 : pull);
			if (smoothD[i] - T[i] > SMOOTH_MAX_DEV) smoothD[i] = T[i] + SMOOTH_MAX_DEV;
			if (T[i] - smoothD[i] > SMOOTH_MAX_DEV) smoothD[i] = T[i] - SMOOTH_MAX_DEV;
		}
	logDevX = (int)floor(smoothD[0] - T[0] + 0.5); logDevY = (int)floor(smoothD[1] - T[1] + 0.5);
	*outX = (int)floor(smoothD[0] + 0.5); *outY = (int)floor(smoothD[1] + 0.5);
}

static bool composeStill; /* #103: draw sprites where vanilla RF placed them (grid, alpha 1) */

static void Compose(double alpha)
{
	/* #103: frame width; the view and the sim's wake range use the same RF_ViewWidth */
	viewW = RF_ViewWidth();
	outW = viewW * S;
	bool wide = viewW > 320;
	int qx = (mode == RFI_Mode_Grid) ? 32 : (mode == RFI_Mode_Pixel) ? 16 : 16 / S;
	int qy = (mode == RFI_Mode_Grid) ? 16 : qx;
	if (mode == RFI_Mode_Grid) alpha = 1.0;

	int camX, camY;
	if (mode != RFI_Mode_Grid && CK_FIX(CameraSmoothing))
	{
		/* keen456-decomp #86: target T = unrounded Lerp; draw everything relative to the smoothed D */
		double tx = rf_scrollXUnit, ty = rf_scrollYUnit;
		if (abs(rf_scrollXUnit - prevCamX) <= SNAP_UNITS) tx = prevCamX + (double)(rf_scrollXUnit - prevCamX) * alpha;
		if (abs(rf_scrollYUnit - prevCamY) <= SNAP_UNITS) ty = prevCamY + (double)(rf_scrollYUnit - prevCamY) * alpha;
		SmoothCamera(tx, ty, &camX, &camY);
		camX = Quant(camX, qx); camY = Quant(camY, qy);
	}
	else
	{
		smoothValid = false;
		logDevX = logDevY = 0;
		camX = Quant(Lerp(prevCamX, rf_scrollXUnit, alpha), qx);
		camY = Quant(Lerp(prevCamY, rf_scrollYUnit, alpha), qy);
	}
	/* #103: the view's left edge (vx) and the band of map it may show. The band is taken from the
	 * sim camera; the displayed camera (lerped, or overshooting with camera-smoothing) is only
	 * clamped into it, on both axes, so no frame shows past a scroll block or the map border.
	 * The sim camera is always inside, so grid mode (= vanilla) is unchanged. */
	int bandL = 0, bandR = 0;
	int vx = RF_WideViewLeft(camX, viewW, rf_scrollXUnit, &bandL, &bandR);
	camY = RF_ClampViewTop(camY, rf_scrollYUnit);
	if (!wide)
		camX = vx; /* the 320 view is the camera window itself */
	viewOffX = (camX - vx) / 16;
	int originTX = FloorDiv(vx, 256) - 1, originTY = FloorDiv(camY, 256) - 1;
	int offX = (vx - originTX * 256) * S / 16, offY = (camY - originTY * 256) * S / 16;
	RF_SpriteDrawEntry *hud = NULL; /* the scorebox, drawn last in wide frames */
	int hudX = 0, hudY = 0;
	sbX0 = sbX1 = sbY0 = sbY1 = 0;

	logCamX = camX * S / 16; logCamY = camY * S / 16; logAlpha = alpha;
	logPlat = false;
	logSBX = logSBY = -99999;

	/* keen456-decomp #85: pixel/subpixel only. A riding Keen is drawn at the platform's quantised
	 * position plus his own interpolated+quantised offset from it, so independent rounding of the two
	 * cannot make him jitter on the platform. Find the platform's entry (it must be drawn this frame). */
	RF_SpriteDrawEntry *platSde = NULL;
	int platDX = 0, platDY = 0, platOX = 0, platOY = 0;
	if (mode != RFI_Mode_Grid && ck_keenObj && ck_keenObj->sde && ck_keenState.platform && ck_keenState.platform->sde)
	{
		RF_SpriteDrawEntry *p = ck_keenState.platform->sde, *k = ck_keenObj->sde;
		bool pFound = false, kFound = false;
		for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
			for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
			{
				if (sde == p) pFound = true;
				if (sde == k) kFound = true;
			}
		/* either sprite snapping (teleport, level reset) -> normal path */
		if (pFound && kFound && p != k &&
			abs(p->curUX - p->prevUX) <= SNAP_UNITS && abs(p->curUY - p->prevUY) <= SNAP_UNITS &&
			abs(k->curUX - k->prevUX) <= SNAP_UNITS && abs(k->curUY - k->prevUY) <= SNAP_UNITS &&
			abs((k->curUX - p->curUX) - (k->prevUX - p->prevUX)) <= SNAP_UNITS &&
			abs((k->curUY - p->curUY) - (k->prevUY - p->prevUY)) <= SNAP_UNITS)
		{
			VH_SpriteTableEntry pe = VH_GetSpriteTableEntry(p->chunk - ca_gfxInfoE.offSprites);
			platOX = pe.originX; platOY = pe.originY;
			platDX = Quant(Lerp(p->prevUX, p->curUX, alpha) + platOX, qx);
			platDY = Quant(Lerp(p->prevUY, p->curUY, alpha) + platOY, qy);
			platSde = p;
		}
	}
	xrayFrame = RF_XrayOn(); /* #104 */
	if (xrayFrame)
		memset(outX, 0, (size_t)OUT_W * OUT_H);
	BuildLayers(originTX, originTY);
	BlitLayer(layerBack, offX, offY, false);
	for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
	{
		if (z == 3) BlitLayer(layerFore, offX, offY, true);
		for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
		{
			VH_SpriteTableEntry ste = VH_GetSpriteTableEntry(sde->chunk - ca_gfxInfoE.offSprites);
			int ux = Lerp(sde->prevUX, sde->curUX, alpha) + ste.originX;
			int uy = Lerp(sde->prevUY, sde->curUY, alpha) + ste.originY;
			if (composeStill)
			{
				/* #103: vanilla's own placement (prev/cur are not kept when smoothing is off) */
				ux = (sde->x + sde->shift * 2) << 4;
				uy = sde->y << 4;
			}
			/* world-locked snapping: sprites sit on the same grid as the tiles */
			ux = Quant(ux, qx); uy = Quant(uy, qy);
			if (platSde && sde == ck_keenObj->sde)
			{
				int rx = Lerp(sde->prevUX - platSde->prevUX, sde->curUX - platSde->curUX, alpha) + ste.originX - platOX;
				int ry = Lerp(sde->prevUY - platSde->prevUY, sde->curUY - platSde->curUY, alpha) + ste.originY - platOY;
				ux = platDX + Quant(rx, qx); uy = platDY + Quant(ry, qy);
			}
			if (mode != RFI_Mode_Grid && ck_scoreBoxObj && sde == ck_scoreBoxObj->sde)
			{
				/* keen456-decomp #77: the scorebox/demo sign is a HUD: pinned to the screen, so draw it at
				 * the displayed camera plus its fixed screen offset. Without this it follows the plain lerp
				 * while the camera follows the smoothed D (camera-smoothing), and shimmers. */
				ux = camX + Quant(sde->curUX - rf_scrollXUnit + ste.originX, qx);
				uy = camY + Quant(sde->curUY - rf_scrollYUnit + ste.originY, qy);
			}
			/* #103: in a wide frame the scorebox stays in the top-left corner of the whole view */
			bool isHud = wide && ck_scoreBoxObj && sde == ck_scoreBoxObj->sde;
			if (isHud)
				ux += vx - camX;
			if (mode == RFI_Mode_Grid)
			{
				/* Vanilla (RF_AddSpriteDraw) only has as many pre-shifted copies as the
				 * sprite's 'shifts' field: 1 = always the unshifted copy, on the 8-px
				 * byte boundary; 2 = shift 0 or 4 px; 4 = 0/2/4/6 px. Match that. */
				int px = ux >> 4, sh = (px & 7) / 2;
				if (vl_noPan || ste.shifts == 1) sh = 0;
				else if (ste.shifts == 2) sh = (sh >= 2) ? 2 : 0;
				ux = ((px & ~7) + sh * 2) << 4;
			}
			if (ck_keenObj && sde == ck_keenObj->sde) { logKeenX = (ux - camX) * S / 16; logKeenWorldX = ux * S / 16; logKeenY = (uy - camY) * S / 16; }
			if (ck_keenObj && ck_keenState.platform && sde == ck_keenState.platform->sde) { logPlat = true; logPlatX = (ux - camX) * S / 16; logPlatY = (uy - camY) * S / 16; }
			if (ck_scoreBoxObj && sde == ck_scoreBoxObj->sde) { logSBX = (ux - camX) * S / 16; logSBY = (uy - camY) * S / 16; }
			if (spriteLog)
				fprintf(spriteLog, "%ld,%ld,%.3f,%d,%d,%lx,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", dispFrame, simFrame, alpha, camX * S / 16, camY * S / 16,
					(unsigned long)(uintptr_t)sde, z, sde->chunk, sde->prevUX, sde->prevUY, sde->curUX, sde->curUY,
					(ux - camX) * S / 16, (uy - camY) * S / 16,
					(ck_keenObj && sde == ck_keenObj->sde && ck_keenObj->currentAction) ? (int)ck_keenObj->currentAction->compatDosPointer : -1);
			if (isHud)
			{
				hud = sde; hudX = (ux - vx) * S / 16; hudY = (uy - camY) * S / 16;
				continue;
			}
			BlitSprite(sde, (ux - vx) * S / 16, (uy - camY) * S / 16);
		}
	}
	if (!wide)
		return;
	/* #103: black outside the band the camera may scroll to (beyond a scroll block or the map) */
	int bx0 = CK_Cross_max(0, CK_Cross_min(OUT_W, (bandL - vx) * S / 16));
	int bx1 = CK_Cross_max(0, CK_Cross_min(OUT_W, (bandR - vx) * S / 16));
	for (int y = 0; y < OUT_H; ++y)
	{
		uint8_t *row = out8 + y * OUT_W, *rowX = outX + y * OUT_W;
		if (bx0 > 0) { memset(row, 0, bx0); memset(rowX, 0, bx0); }
		if (bx1 < OUT_W) { memset(row + bx1, 0, OUT_W - bx1); memset(rowX + bx1, 0, OUT_W - bx1); }
	}
	if (hud)
	{
		BlitSprite(hud, hudX, hudY);
		VH_SpriteTableEntry ste = VH_GetSpriteTableEntry(hud->chunk - ca_gfxInfoE.offSprites);
		sbX0 = hudX - 8 * S; sbX1 = hudX + (hud->sw + 8) * S;
		sbY0 = hudY; sbY1 = hudY + ste.height * S;
	}
}

/* #103: copy the 320x200 vanilla screen (sx, sy = its view origin on the screen surface) into
 * out8 where the camera window lies, so anything drawn straight onto the screen (dialogs, the
 * status window) shows over the wide view. Where the scorebox is, in the wide view and in the
 * vanilla window, out8 is kept: the wide frame has moved the scorebox to its own corner. */
static void OverlayVanilla(int sx, int sy)
{
	void *screen = vl_emuegavgaadapter.screen;
	int vbX0 = sbX0 + viewOffX * S, vbX1 = sbX1 + viewOffX * S;
	for (int y = 0; y < 200; ++y)
	{
		int oy = y * S;
		bool sbRow = oy >= sbY0 && oy < sbY1;
		uint8_t *dst = out8 + oy * OUT_W;
		for (int x = 0; x < 320; ++x)
		{
			int ox = (viewOffX + x) * S;
			if (ox < 0 || ox + S > OUT_W) continue;
			if (sbRow && ((ox + S > sbX0 && ox < sbX1) || (ox + S > vbX0 && ox < vbX1))) continue;
			uint8_t c = (uint8_t)(VL_SurfacePGet(screen, sx + x, sy + y) & 15);
			if (xrayFrame && outX[oy * OUT_W + ox])
			{
				/* #104: the vanilla screen has the dithered X-ray there (RF_RenderTile16m): what is
				 * beneath, the tile, or its tinted colour. Keep the blend unless something else
				 * (a dialog) was drawn over it. */
				int fg = outX[oy * OUT_W + ox] & 15;
				if (c == dst[ox] || c == fg || c == RF_XrayTint(fg))
					continue;
				for (int i = 0; i < S; ++i) outX[oy * OUT_W + ox + i] = 0;
			}
			for (int i = 0; i < S; ++i) dst[ox + i] = c;
		}
		for (int i = 1; i < S; ++i)
		{
			memcpy(out8 + (oy + i) * OUT_W, dst, OUT_W);
			if (xrayFrame)
				memcpy(outX + (oy + i) * OUT_W, outX + oy * OUT_W, OUT_W);
		}
	}
}

/* #104: out8 (+ outX) -> out32 through the 16-colour palette pal. An X-ray pixel is half what is
 * beneath and half the tile, pulled a fifth of the way to cyan. */
static void ExpandOut(const uint32_t *pal)
{
	if (!xrayFrame)
	{
		for (int i = 0; i < OUT_W * OUT_H; ++i)
			out32[i] = pal[out8[i] & 15];
		return;
	}
	uint32_t mix[16][16]; /* [tile][beneath] */
	for (int f = 0; f < 16; ++f)
		for (int u = 0; u < 16; ++u)
		{
			uint32_t r = 0xFF000000u;
			for (int sh = 0; sh <= 16; sh += 8)
			{
				int a = (pal[f] >> sh) & 0xFF, b = (pal[u] >> sh) & 0xFF;
				int t = (sh == 16) ? 0x00 : 0xFF; /* cyan */
				r |= (uint32_t)(((a + b) * 2 + t * 1) / 5) << sh;
			}
			mix[f][u] = r;
		}
	for (int i = 0; i < OUT_W * OUT_H; ++i)
		out32[i] = outX[i] ? mix[outX[i] & 15][out8[i] & 15] : pal[out8[i] & 15];
}

static void ExpandPalette(void)
{
	uint32_t pal[16];
	for (int i = 0; i < 16; ++i)
	{
		const uint8_t *c = VL_EGARGBColorTable[vl_emuegavgaadapter.palette[i]];
		pal[i] = 0xFF000000u | (c[0] << 16) | (c[1] << 8) | c[2];
	}
	ExpandOut(pal);
}

void RFI_WideSceneEnd(void)
{
	wideValid = false;
}

/* #103: VL_Present during widescreen play (fades, dialogs, the Enter-key status window, and every
 * frame when smoothing is off). Composes the wide view of the current sim frame on the vanilla
 * 2-px grid, lays the vanilla screen over its centre and presents that. After the play loop has
 * ended (death, level exit, the next level's loading box) the last wide frame is reused, since
 * the map may already be the next one. Returns false when the frame is not wide: the caller then
 * presents the 320-wide screen as usual (pillarboxed in a wide region). */
bool RFI_PresentWideStill(void)
{
	int sx, sy;
	if (rf_widePlay)
	{
		if (RF_ViewWidth() <= 320)
		{
			wideValid = false;
			return false;
		}
		int m = mode;
		mode = RFI_Mode_Grid;
		composeStill = true;
		layersSim = -1; /* tiles may have changed without a sim frame passing */
		Compose(1.0);
		composeStill = false;
		mode = m;
		layersSim = -1;
		wideValid = true;
		/* RF_Refresh sets the scroll after its draw hook; the screen is already drawn for this */
		sx = RF_UnitToPixel(rf_scrollXUnit & 0xef);
		sy = RF_UnitToPixel(rf_scrollYUnit & 0xff);
	}
	else if (wideValid)
	{
		sx = VL_GetScrollX();
		sy = VL_GetScrollY();
	}
	else
		return false;
	OverlayVanilla(sx, sy);
	ExpandPalette();
	VL_PresentARGB(out32, OUT_W, OUT_H, NULL);
#ifdef __EMSCRIPTEN__
	/* like VL_SDL2_Present: callers that loop on VL_Present rely on it yielding to the browser */
	VL_WaitRAF();
	CK_Cross_NoteYield();
#endif
	return true;
}

static void LogPresent(double t)
{
	static double last;
	if (!presentLog)
		return;
	if (presentT0 < 0)
		presentT0 = last = t;
	/* compose: Compose(); work: palette expand + texture upload + draw calls; wait: blocked in the
	 * present (vsync / rAF); gap: previous present's return to this compose (sim, game loop, yields);
	 * raf_lag: browser only, Now() after the rAF resumed minus the rAF callback timestamp */
	double rafLag = 0;
#ifdef __EMSCRIPTEN__
	if (!capFile)
		rafLag = t * 1e6 - vl_presentRafMs * 1e3;
#endif
	fprintf(presentLog, "%ld,%.0f,%.0f,%ld,%.3f,%.0f,%.0f,%.0f,%.0f,%.0f,%d\n", dispFrame, (t - presentT0) * 1e6, (t - last) * 1e6, simFrame, logAlpha,
		logComposeS * 1e6, vl_presentWorkS * 1e6, vl_presentWaitS * 1e6, logGapS * 1e6, rafLag, vl_presentInRaf);
	last = t;
}

static void Present(void)
{
	uint32_t pal[256];
	for (int i = 0; i < 16; ++i)
	{
		const uint8_t *c = VL_EGARGBColorTable[vl_emuegavgaadapter.palette[i]];
		pal[i] = 0xFF000000u | (c[0] << 16) | (c[1] << 8) | c[2];
	}
	if (capFile)
	{
		if (logFile) {
			fprintf(logFile, "%ld,%ld,%.3f,%d,%d,%d,%d,", dispFrame, simFrame, logAlpha, logCamX, logKeenWorldX, logKeenX, logKeenY);
			if (logPlat && ck_keenState.platform) fprintf(logFile, "%d,%d", logPlatX, logPlatY);
			else fprintf(logFile, ",");
			fprintf(logFile, ",%d,%d,%d,%d,%d,%d,%d\n", logCamY, rf_scrollXUnit, rf_scrollYUnit, logDevX, logDevY, logSBX, logSBY);
		}
		LogPresent(vnow);
		if (dispFrame >= capSkip)
		{
			ExpandOut(pal);
			for (int i = 0; i < OUT_W * OUT_H; ++i)
			{
				uint32_t c = out32[i];
				out24[i * 3] = c >> 16; out24[i * 3 + 1] = c >> 8; out24[i * 3 + 2] = c;
			}
			fwrite(out24, 1, (size_t)OUT_W * OUT_H * 3, capFile);
			if (capFrames > 0 && dispFrame - capSkip + 1 >= capFrames)
			{
				fclose(capFile);
				capFile = NULL;
				if (logFile) fclose(logFile);
				logFile = NULL;
				if (presentLog) fclose(presentLog);
				presentLog = NULL;
				CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Interpolation: debug capture done: %ld display frames, %ld sim frames\n", dispFrame + 1, simFrame);
				exit(0);
			}
		}
		vnow += 1.0 / hz;
		dispFrame++;
		return;
	}
	double e0 = Now();
	ExpandOut(pal);

	double t = Now();
	double expandS = t - e0;
	char title[128] = "";
	if (lastPresent > 0)
	{
		double dt = (t - lastPresent) * 1000.0;
		statSum += dt; statN++;
		if (dt < statMin) statMin = dt;
		if (dt > statMax) statMax = dt;
	}
	lastPresent = t;
	if (debugStats && t - statT0 > 1.0 && statN)
	{
		snprintf(title, sizeof(title), "Omnispeak interpolation [F12] %s | %.1f fps  frame %.2f ms (min %.2f max %.2f)  compose %.2f ms  filter %.2f ms",
			rfi_modeStrings[mode], 1000.0 * statN / statSum, statSum / statN, statMin, statMax, 1000.0 * composeSum / statN, 1000.0 * vl_filterWorkS / statN);
		vl_filterWorkS = 0;
		composeSum = 0;
		fprintf(stderr, "[interp] %s\n", title);
		statT0 = t; statSum = 0; statN = 0; statMin = 1e9; statMax = 0;
	}
	/* No vsync to block on: sleep until the next refresh slot instead of spinning. */
	/* Never force pacing on top of vsync: only pace when vsync is off or missing (keen456-decomp #74). */
#ifndef __EMSCRIPTEN__ /* browser: RFI_Frame awaits requestAnimationFrame (VL_WaitPresentSlot) */
	if (!vl_swapInterval || noVsync)
	{
		double wait = nextPresent - Now();
		if (wait > 0.0005)
			SDL_Delay((Uint32)(wait * 1000.0 + 0.5)); /* ms granularity: +-0.5 ms */
		double now = Now();
		nextPresent = CK_Cross_max(nextPresent + 1.0 / hz, now);
	}
#endif
	double before = Now();
	VL_PresentARGB(out32, OUT_W, OUT_H, title[0] ? title : NULL);
	double after = Now();
	vl_presentWorkS += expandS; /* debug log: palette expand counts as present work */
	/* Only probe for missing vsync when the user actually requested vsync (keen456-decomp #74). */
#ifdef __EMSCRIPTEN__
	if (0) /* the browser paces in RFI_Frame (VL_WaitPresentSlot); presents never block */
#else
	if (vl_swapInterval)
#endif
	{
		/* Presents that never block mean vsync isn't pacing us: from then on, pace by sleeping. */
		fastPresents = (after - before < 0.0005) ? fastPresents + 1 : 0; /* only non-blocking presents count (keen456-decomp #74) */
		if (fastPresents >= 8)
			noVsync = true;
	}
	LogPresent(after);
	dispFrame++;
}

/* Called from RF_Refresh in place of VL_Present + RFL_CalcTics. */
void RFI_Frame(void)
{
	static bool f12Was;
	bool inDemo = (in_demoState != IN_Demo_Off);
	int tics = inDemo ? rf_demoTics : 2;
	double dur = tics / 70.0;
	double now = Now();

	SnapOnKeenTransition();
	if (!havePrev) { prevCamX = rf_scrollXUnit; prevCamY = rf_scrollYUnit; havePrev = true; }
	/* First frame, or resumed after a stall: like RFL_CalcTics, catch up at
	 * most rf_maxTics tics (dropping presents) and let the game run slow for
	 * the rest, instead of a burst of sim frames. */
	if (frameStart < 0 || now - frameStart > rf_maxTics / 70.0)
		frameStart = now;

	/* keen456-decomp #75: OMNI_DEBUG_FRAMES: compose + dump exactly once per sim
	 * frame (alpha forced to 1 in grid mode), independent of wall-clock, and skip
	 * the realtime present loop so no sim frame can be missed. */
	if (debugFrameFile)
	{
		Compose(1.0);
		RFI_DebugDumpFrame();
	}
	for (; !debugFrameFile;)
	{
		double g0 = Now();
#ifdef __EMSCRIPTEN__
		/* browser: wait for the refresh first, then pick alpha and draw inside the
		 * rAF callback (see VL_WaitPresentSlot); returns at once if this callback
		 * has not drawn yet, e.g. right after the sim frame ran in it */
		if (!capFile)
			VL_WaitPresentSlot();
#endif
		double alpha = (Now() - frameStart) / dur;
		if (alpha >= 1.0) break;
		if (alpha < 0) alpha = 0;
		double c0 = Now();
		Compose(alpha);
		wideValid = viewW > 320;
		logComposeS = Now() - c0;
		logGapS = lastPresentEnd < 0 ? 0 : g0 - lastPresentEnd;
		composeSum += logComposeS;
		Present();
		lastPresentEnd = Now();
	}
	frameStart += dur;
	/* the page flip VL_Present would have done, so the renderer's buffers
	 * (and RF's per-buffer eraser bookkeeping) advance once per sim frame */
	VL_FlipPageNoPresent();

	/* this sim frame becomes "prev" for the next one */
	prevCamX = rf_scrollXUnit; prevCamY = rf_scrollYUnit;
	for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
		for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
		{
			sde->prevUX = sde->curUX;
			sde->prevUY = sde->curUY;
		}

	if (inDemo)
	{
		/* demos: use the vanilla clock (wait for the tick, TimeCount one frame
		 * ahead) so the playloop dump, which records TimeCount, is unchanged */
#ifdef __EMSCRIPTEN__
		/* Browser: the presents above already paced this frame to rf_demoTics
		 * (frameStart+dur). RFL_CalcTics would then wait a further full demo
		 * frame since its previous return (the clock phase is reset every
		 * frame), stretching each sim frame to ~46 ms and dropping presents.
		 * Do the same clock bookkeeping, same values, without the wait. */
		{
			uint32_t newTime = (uint32_t)SD_GetLastTimeCount();
			if (newTime > SD_GetTimeCount())
				SD_SetTimeCount(newTime);
			SD_SetLastTimeCount(newTime + rf_demoTics);
			SD_SetTimeCount(newTime + rf_demoTics * 2);
			SD_SetSpriteSync(rf_demoTics);
		}
#else
		RFL_CalcTics();
#endif
	}
	else
	{
		/* keep the game's own clock coherent: exactly `tics` elapsed */
		SD_SetSpriteSync(tics);
		SD_SetLastTimeCount(SD_GetLastTimeCount() + tics);
		SD_SetTimeCount(SD_GetLastTimeCount());
	}

	simFrame++;
	if (presentLog)
		fflush(presentLog);
	if (spriteLog)
		fflush(spriteLog);
	if (debugStats && simFrame % 25 == 0) fprintf(stderr, "[interp] sim %ld cam %d,%d\n", simFrame, rf_scrollXUnit, rf_scrollYUnit);
	/* OMNI_DEBUG_INTERP_WARP="sim,tx,ty": debug only, put Keen at tile (tx, ty) at that sim frame
	 * and recentre the camera, to reach map edges in tests (#103). */
	{
		static int warpSim = -2, warpTX, warpTY;
		if (warpSim == -2)
		{
			const char *w = getenv("OMNI_DEBUG_INTERP_WARP");
			if (!w || sscanf(w, "%d,%d,%d", &warpSim, &warpTX, &warpTY) != 3) warpSim = -1;
		}
		if (warpSim >= 0 && simFrame == warpSim && ck_keenObj)
		{
			ck_keenObj->posX = warpTX << 8;
			ck_keenObj->posY = warpTY << 8;
			CK_ResetClipRects(ck_keenObj);
			CK_CentreCamera(ck_keenObj);
			RFI_Reset();
		}
	}
	for (int i = 0; i < scriptN; ++i)
	{
		/* re-assert every frame: level start clears the key table */
		if (simFrame >= script[i].from && simFrame < script[i].to) IN_HandleKeyDown((IN_ScanCode)script[i].key, false);
		if (simFrame == script[i].to) IN_HandleKeyUp((IN_ScanCode)script[i].key, false);
	}
	bool f12 = IN_GetKeyState(IN_SC_F12);
	if (f12 && !f12Was)
	{
		mode = (mode == RFI_Mode_Subpixel) ? RFI_Mode_Grid : mode + 1;
		CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Interpolation: mode -> %s\n", rfi_modeStrings[mode]);
	}
	f12Was = f12;
}
