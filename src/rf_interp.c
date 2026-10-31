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
 *   OMNI_DEBUG_INTERP_CAPTURE=<file>  headless, deterministic capture: raw RGB24
 *       1280x800 frames on a virtual display, time advancing exactly 1/Hz per
 *       frame; RNG seeding is made deterministic.
 *   OMNI_DEBUG_INTERP_HZ=<n>          virtual display rate for the capture (60).
 *   OMNI_DEBUG_INTERP_SKIP / _FRAMES  bound the recording; exits when done.
 *   OMNI_DEBUG_INTERP_LOG=<file.csv>  per-display-frame camera/Keen trace (capture only).
 *   OMNI_DEBUG_INTERP_SCRIPT="right:0-600,jump:80-90,..."  inject input by sim frame.
 *   OMNI_DEBUG_INTERP_STATS=1         frame-time/compose stats to stderr + window title.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>

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
#include "rf_interp.h"
#include "ck_def.h"
#include "ck_play.h"

#define S 4 /* output scale: sub-pixel quantum is 1/S px = 16/S units */
#define RF_NUM_SPRITE_Z_LAYERS 4
#define OUT_W (320 * S)
#define OUT_H (200 * S)
#define LAYER_TW 23
#define LAYER_TH 16
#define LAYER_W (LAYER_TW * 16)
#define LAYER_H (LAYER_TH * 16)
#define KEY 0xFF
#define SNAP_UNITS (48 * 16) /* larger jumps are teleports: never interpolated */

extern int rf_scrollXUnit, rf_scrollYUnit;
extern RF_SpriteDrawEntry *rf_firstSpriteTableEntry[RF_NUM_SPRITE_Z_LAYERS];
void VL_SDL2_PresentARGB(const uint32_t *argb, int w, int h, const char *title);

static int mode = RFI_Mode_Off, hz = 60;
static bool enabled, debugStats;
static FILE *capFile;
static long capSkip, capFrames = -1, dispFrame, simFrame;
static double frameStart = -1.0, vnow;
static int prevCamX, prevCamY;
static bool havePrev;
static uint8_t layerBack[LAYER_W * LAYER_H], layerFore[LAYER_W * LAYER_H];
static uint8_t out8[OUT_W * OUT_H];
static uint32_t out32[OUT_W * OUT_H];
static uint8_t out24[OUT_W * OUT_H * 3];
const char *rfi_modeStrings[] = {"off", "grid", "pixel", "subpixel", NULL};

/* stats */
static double lastPresent = -1, statSum, statMin = 1e9, statMax, statT0;
static int statN;
static double composeSum;
static FILE *logFile;
static int logCamX, logKeenX, logKeenWorldX;
static double logAlpha;

typedef struct { int key, from, to; } ScriptEv;
static ScriptEv script[64];
static int scriptN;

static double Now(void)
{
	if (capFile)
		return vnow;
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

void RFI_Startup(RFI_Mode m)
{
	const char *e;
	mode = m;
	enabled = (m != RFI_Mode_Off);
	if (!enabled)
		return;
	if ((e = getenv("OMNI_DEBUG_INTERP_HZ")))
		hz = atoi(e);
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
			fprintf(logFile, "disp,sim,alpha,cam_x_qpx,keen_world_x_qpx,keen_screen_x_qpx\n");
	}
	if ((e = getenv("OMNI_DEBUG_INTERP_CAPTURE")))
	{
		capFile = fopen(e, "wb");
		if (!capFile)
			QuitF("Couldn't open interpolation capture file \"%s\"", e);
	}
	CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Interpolation: %s (%s, hz=%d, script events=%d)\n", rfi_modeStrings[mode],
		capFile ? "debug capture" : "realtime", hz, scriptN);
}

bool RFI_Enabled(void)
{
	return enabled;
}

// Debug capture only: make level RNG seeding deterministic.
bool RFI_DebugDeterministic(void)
{
	return enabled && capFile;
}

void RFI_NoteSprite(RF_SpriteDrawEntry *sde, int unitX, int unitY, bool fresh)
{
	sde->curUX = unitX;
	sde->curUY = unitY;
	if (fresh)
	{
		sde->prevUX = unitX;
		sde->prevUY = unitY;
	}
}

static inline int FloorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static inline int Quant(int u, int q) { return FloorDiv(u, q) * q; }
static inline int Lerp(int a, int b, double t)
{
	if (abs(b - a) > SNAP_UNITS) return b;
	return a + (int)floor((double)(b - a) * t + 0.5);
}

static long layersSim = -1; static int layersTX, layersTY;
static void BuildLayers(int originTX, int originTY)
{
	if (layersSim == simFrame && layersTX == originTX && layersTY == originTY) return;
	layersSim = simFrame; layersTX = originTX; layersTY = originTY;
	int mw = CA_GetMapWidth(), mh = CA_GetMapHeight();
	memset(layerBack, 0, sizeof(layerBack));
	memset(layerFore, KEY, sizeof(layerFore));
	for (int ty = 0; ty < LAYER_TH; ++ty)
		for (int tx = 0; tx < LAYER_TW; ++tx)
		{
			int mx = originTX + tx, my = originTY + ty;
			if (mx < 0 || my < 0 || mx >= mw || my >= mh) continue;
			int bg = CA_TileAtPos(mx, my, 0), fg = CA_TileAtPos(mx, my, 1);
			void *src = CA_GetGrChunk(ca_gfxInfoE.offTiles16, bg, "Tile16", false);
			if (src) VL_UnmaskedToPAL8(src, layerBack, tx * 16, ty * 16, LAYER_W, 16, 16);
			if (!fg) continue;
			uint8_t *m = (uint8_t *)CA_GetGrChunk(ca_gfxInfoE.offTiles16m, fg, "Tile16m", false);
			if (!m) continue;
			bool pri = (TI_ForeMisc(fg) & 0x80) != 0;
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
				{
					int off = (y * 16 + x) >> 3, bit = 1 << (7 - ((y * 16 + x) & 7));
					if (m[off] & bit) continue; /* mask set = transparent */
					int px = ((m[off + 128] & bit) ? 8 : 0) | ((m[off + 96] & bit) ? 4 : 0) |
						((m[off + 64] & bit) ? 2 : 0) | ((m[off + 32] & bit) ? 1 : 0);
					int di = (ty * 16 + y) * LAYER_W + tx * 16 + x;
					layerBack[di] = px;
					if (pri) layerFore[di] = px;
				}
		}
}

/* Sample a 1x layer into the SxS output; (offX, offY) = camera minus layer origin, in output pixels. */
static void BlitLayer(const uint8_t *layer, int offX, int offY, bool keyed)
{
	static int lxTab[OUT_W];
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
		else
			for (int x = 0; x < OUT_W; ++x) { if (lxTab[x] >= 0 && row[lxTab[x]] != KEY) dst[x] = row[lxTab[x]]; }
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
					out8[yy * OUT_W + xx] = px;
		}
	}
}

static void Compose(double alpha)
{
	int qx = (mode == RFI_Mode_Grid) ? 32 : (mode == RFI_Mode_Pixel) ? 16 : 16 / S;
	int qy = (mode == RFI_Mode_Grid) ? 16 : qx;
	if (mode == RFI_Mode_Grid) alpha = 1.0;

	int camX = Quant(Lerp(prevCamX, rf_scrollXUnit, alpha), qx);
	int camY = Quant(Lerp(prevCamY, rf_scrollYUnit, alpha), qy);
	int originTX = FloorDiv(camX, 256) - 1, originTY = FloorDiv(camY, 256) - 1;
	int offX = (camX - originTX * 256) * S / 16, offY = (camY - originTY * 256) * S / 16;

	logCamX = camX * S / 16; logAlpha = alpha;
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
			/* world-locked snapping: sprites sit on the same grid as the tiles */
			ux = Quant(ux, qx); uy = Quant(uy, qy);
			if (ck_keenObj && sde == ck_keenObj->sde) { logKeenX = (ux - camX) * S / 16; logKeenWorldX = ux * S / 16; }
			BlitSprite(sde, (ux - camX) * S / 16, (uy - camY) * S / 16);
		}
	}
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
		if (logFile) fprintf(logFile, "%ld,%ld,%.3f,%d,%d,%d\n", dispFrame, simFrame, logAlpha, logCamX, logKeenWorldX, logKeenX);
		if (dispFrame >= capSkip)
		{
			for (int i = 0; i < OUT_W * OUT_H; ++i)
			{
				uint32_t c = pal[out8[i] & 15];
				out24[i * 3] = c >> 16; out24[i * 3 + 1] = c >> 8; out24[i * 3 + 2] = c;
			}
			fwrite(out24, 1, sizeof(out24), capFile);
			if (capFrames > 0 && dispFrame - capSkip + 1 >= capFrames)
			{
				fclose(capFile);
				if (logFile) fclose(logFile);
				CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Interpolation: debug capture done: %ld display frames, %ld sim frames\n", dispFrame + 1, simFrame);
				exit(0);
			}
		}
		vnow += 1.0 / hz;
		dispFrame++;
		return;
	}
	for (int i = 0; i < OUT_W * OUT_H; ++i)
		out32[i] = pal[out8[i] & 15];

	double t = Now();
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
		snprintf(title, sizeof(title), "Omnispeak interpolation [F12] %s | %.1f fps  frame %.2f ms (min %.2f max %.2f)  compose %.2f ms",
			rfi_modeStrings[mode], 1000.0 * statN / statSum, statSum / statN, statMin, statMax, 1000.0 * composeSum / statN);
		composeSum = 0;
		fprintf(stderr, "[interp] %s\n", title);
		statT0 = t; statSum = 0; statN = 0; statMin = 1e9; statMax = 0;
	}
	double before = Now();
	VL_SDL2_PresentARGB(out32, OUT_W, OUT_H, title[0] ? title : NULL);
	if (Now() - before < 0.001) SDL_Delay(1); /* no vsync (Xvfb etc.): don't spin */
	dispFrame++;
}

/* Called from RF_Refresh in place of VL_Present + RFL_CalcTics. */
void RFI_Frame(void)
{
	static bool f12Was;
	int tics = (in_demoState != IN_Demo_Off) ? 3 : 2;
	double dur = tics / 70.0;
	double now = Now();

	if (!havePrev) { prevCamX = rf_scrollXUnit; prevCamY = rf_scrollYUnit; havePrev = true; }
	if (frameStart < 0 || now - frameStart > 0.25) frameStart = now; /* first frame, or resumed after a stall */

	for (;;)
	{
		double alpha = (Now() - frameStart) / dur;
		if (alpha >= 1.0) break;
		if (alpha < 0) alpha = 0;
		double c0 = Now();
		Compose(alpha);
		composeSum += Now() - c0;
		Present();
	}
	frameStart += dur;

	/* this sim frame becomes "prev" for the next one */
	prevCamX = rf_scrollXUnit; prevCamY = rf_scrollYUnit;
	for (int z = 0; z < RF_NUM_SPRITE_Z_LAYERS; ++z)
		for (RF_SpriteDrawEntry *sde = rf_firstSpriteTableEntry[z]; sde; sde = sde->next)
		{
			sde->prevUX = sde->curUX;
			sde->prevUY = sde->curUY;
		}

	/* keep the game's own clock coherent: exactly `tics` elapsed */
	SD_SetSpriteSync(tics);
	SD_SetLastTimeCount(SD_GetLastTimeCount() + tics);
	SD_SetTimeCount(SD_GetLastTimeCount());

	simFrame++;
	if (debugStats && simFrame % 25 == 0) fprintf(stderr, "[interp] sim %ld cam %d,%d\n", simFrame, rf_scrollXUnit, rf_scrollYUnit);
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
