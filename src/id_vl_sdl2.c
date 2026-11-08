#include "assert.h"
#include <SDL.h>
#include <string.h>
#include "id_vl.h"
#include "id_vl_private.h"
#include "ck_cross.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* Block (asyncify) until the next browser animation frame: vsync pacing. */
/* Module.__inRaf is 1 from a rAF callback until the end of that task (the timeout queued
 * in the callback runs before any later yield resumes us): a draw made while it is set
 * reaches the screen at this refresh; any other draw only at the next one (keen456-decomp #98). */
EM_ASYNC_JS(double, VL_WaitRAF, (void), {
	return await new Promise(r => requestAnimationFrame(t => {
		Module.__inRaf = 1;
		setTimeout(() => { Module.__inRaf = 0; }, 0);
		r(t);
	}));
});
EM_JS(int, VL_InRAF, (void), { return Module.__inRaf | 0; });
EM_JS(void, VL_ConsumeRAF, (void), { Module.__inRaf = 0; });
#endif
/* debug timing of the last VL_PresentARGB (rf_interp OMNI_DEBUG_INTERP_PRESENTS):
 * seconds spent uploading/drawing, seconds waiting for the display, rAF timestamp (ms, browser only),
 * whether the draw was made inside a rAF callback (browser only) */
extern double vl_presentWorkS, vl_presentWaitS, vl_presentRafMs; /* defined in rf_interp.c */
extern int vl_presentInRaf;
static inline double VL_DebugNowS(void)
{
#ifdef __EMSCRIPTEN__
	return emscripten_get_now() / 1000.0; /* SDL's counter is Date.now() (whole ms) there */
#else
	return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
#endif
}

static SDL_Window *vl_sdl2_window;
static SDL_Renderer *vl_sdl2_renderer;
static SDL_Texture *vl_sdl2_texture;
static SDL_Palette *vl_sdl2_palette;
static SDL_Surface *vl_sdl2_stagingSurface;
static SDL_Texture *vl_sdl2_scaledTarget;
static bool vl_sdl2_bilinearSupport = true;

static void VL_SDL2_ResizeWindow()
{
	int realWinH, realWinW, curW, curH;
	SDL_GetRendererOutputSize(vl_sdl2_renderer, &realWinW, &realWinH);
	VL_CalculateRenderRegions(realWinW, realWinH);

	if (vl_sdl2_scaledTarget)
	{
		// Check if the scaled render target is the correct size.
		SDL_QueryTexture(vl_sdl2_scaledTarget, 0, 0, &curW, &curH);
		if (curW == vl_integerWidth && curH == vl_integerHeight)
		{
			return;
		}

		// We need to recreate the scaled render target. Do so.
		SDL_DestroyTexture(vl_sdl2_scaledTarget);
		vl_sdl2_scaledTarget = 0;
	}

	if (!vl_isIntegerScaled)
	{
#if !SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#endif
		vl_sdl2_scaledTarget = SDL_CreateTexture(vl_sdl2_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, vl_integerWidth, vl_integerHeight);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(vl_sdl2_scaledTarget, SDL_ScaleModeLinear);
#endif

	}
}

static void VL_SDL2_SetVideoMode(int mode)
{
	if (mode == 0x0D)
	{
		SDL_Rect desktopBounds;
#if SDL_VERSION_ATLEAST(2, 0, 5)
		SDL_GetDisplayUsableBounds(0, &desktopBounds);
#else
		SDL_GetDisplayBounds(0, &desktopBounds);
#endif
		int scale = VL_CalculateDefaultWindowScale(desktopBounds.w, desktopBounds.h);
		vl_sdl2_window = SDL_CreateWindow(VL_WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			VL_DEFAULT_WINDOW_WIDTH(scale), VL_DEFAULT_WINDOW_HEIGHT(scale),
			SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | (vl_isFullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));

		SDL_SetWindowMinimumSize(vl_sdl2_window, VL_VGA_GFX_SCALED_WIDTH_PLUS_BORDER / VL_VGA_GFX_WIDTH_SCALEFACTOR, VL_VGA_GFX_SCALED_HEIGHT_PLUS_BORDER / VL_VGA_GFX_HEIGHT_SCALEFACTOR);

		//VL_SDL_SetIcon(vl_sdl2_window);

		// This should be SDL_RendererFlags, but it doesn't work with the gcw0 toolchain,
		// which is absurdly strict in not counting integers as valid enum values (breaking
		// both the 0 here and the |= below). *sigh*
		int flags = vl_swapInterval ? SDL_RENDERER_PRESENTVSYNC : 0;
#ifdef VL_SDL2_REQUEST_SOFTWARE
		flags |= SDL_RENDERER_SOFTWARE;
#endif
		vl_sdl2_renderer = SDL_CreateRenderer(vl_sdl2_window, -1, flags);

		SDL_RendererInfo info;
		SDL_GetRendererInfo(vl_sdl2_renderer, &info);
		if (info.flags & SDL_RENDERER_SOFTWARE)
			vl_sdl2_bilinearSupport = false;

		if (!vl_isIntegerScaled && !vl_sdl2_bilinearSupport)
			CK_Cross_LogMessage(CK_LOG_MSG_WARNING, "Using SDL2 software renderer without integer scaling. Pixel size may be inconsistent.\n");

#if !SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
		vl_sdl2_texture = SDL_CreateTexture(vl_sdl2_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, VL_EGAVGA_GFX_WIDTH, VL_EGAVGA_GFX_HEIGHT);
#if SDL_VERSION_ATLEAST(2, 0, 12)
		SDL_SetTextureScaleMode(vl_sdl2_texture, SDL_ScaleModeNearest);
#endif

		vl_sdl2_palette = SDL_AllocPalette(256);

		// As we can't do on-GPU palette conversions with SDL2,
		// we do a PAL8->RGBA conversion of the visible area to this surface each frame.
		vl_sdl2_stagingSurface = SDL_CreateRGBSurface(0, VL_EGAVGA_GFX_WIDTH, VL_EGAVGA_GFX_HEIGHT, 32, 0, 0, 0, 0);

		VL_SDL2_ResizeWindow();
		SDL_ShowCursor(0);
	}
	else
	{
		SDL_ShowCursor(1);
		SDL_DestroyTexture(vl_sdl2_scaledTarget);
		SDL_DestroyTexture(vl_sdl2_texture);
		SDL_DestroyRenderer(vl_sdl2_renderer);
		SDL_DestroyWindow(vl_sdl2_window);
	}
}

static void VL_SDL2_SurfaceRect(void *dst_surface, int x, int y, int w, int h, int colour);
static void *VL_SDL2_CreateSurface(int w, int h, VL_SurfaceUsage usage)
{
	SDL_Surface *s;
	s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	return s;
}

static void VL_SDL2_DestroySurface(void *surface)
{
	//TODO: Implement
}

static long VL_SDL2_GetSurfaceMemUse(void *surface)
{
	SDL_Surface *surf = (SDL_Surface *)surface;
	if (!surf)
		return 0;
	return surf->pitch * surf->h;
}

static void VL_SDL2_GetSurfaceDimensions(void *surface, int *w, int *h)
{
	SDL_Surface *surf = (SDL_Surface *)surface;
	if (!surf)
		return;
	if (w)
		*w = surf->w;
	if (h)
		*h = surf->h;
}

static void VL_SDL2_RefreshPaletteAndBorderColor(void *screen)
{
	SDL_Surface *surf = (SDL_Surface *)screen;
	static SDL_Color sdl2_palette[16];

	for (int i = 0; i < 16; i++)
	{
		sdl2_palette[i].r = VL_EGARGBColorTable[vl_emuegavgaadapter.palette[i]][0];
		sdl2_palette[i].g = VL_EGARGBColorTable[vl_emuegavgaadapter.palette[i]][1];
		sdl2_palette[i].b = VL_EGARGBColorTable[vl_emuegavgaadapter.palette[i]][2];
	}
	SDL_SetPaletteColors(vl_sdl2_palette, sdl2_palette, 0, 16);
	SDL_SetSurfacePalette(surf, vl_sdl2_palette);
}

static int VL_SDL2_SurfacePGet(void *surface, int x, int y)
{
	SDL_Surface *surf = (SDL_Surface *)surface;
	SDL_LockSurface(surf);
	int col = ((uint8_t *)surf->pixels)[y * surf->pitch + x];
	SDL_UnlockSurface(surf);
	return col;
}

static void VL_SDL2_SurfaceRect(void *dst_surface, int x, int y, int w, int h, int colour)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_Rect rect = {(Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h};
	SDL_FillRect(surf, &rect, colour);
}

static void VL_SDL2_SurfaceRect_PM(void *dst_surface, int x, int y, int w, int h, int colour, int mapmask)
{
	mapmask &= 0xF;
	colour &= mapmask;

	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	for (int py = y; py < y + h; py++)
		for (int px = x; px < x + w; px++)
		{
			uint8_t *p = ((uint8_t *)surf->pixels) + py * surf->w + px;
			*p &= ~mapmask;
			*p |= colour;
		}
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_SurfaceToSurface(void *src_surface, void *dst_surface, int x, int y, int sx, int sy, int sw, int sh)
{
	// Sadly we cannot naively use SDL_BlitSurface, since the surfaces may
	// both be 8-bit but with different palettes, which we ignore.
	SDL_Surface *surf = (SDL_Surface *)src_surface;
	SDL_Surface *dest = (SDL_Surface *)dst_surface;

	SDL_LockSurface(surf);
	SDL_LockSurface(dest);

	for (int _y = sy; _y < sy + sh; ++_y)
	{
		memcpy(((uint8_t *)dest->pixels) + (_y - sy + y) * dest->w + x, ((uint8_t *)surf->pixels) + _y * surf->w + sx, sw);
	}

	SDL_UnlockSurface(dest);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_SurfaceToSelf(void *surface, int x, int y, int sx, int sy, int sw, int sh)
{
	SDL_Surface *srf = (SDL_Surface *)surface;
	SDL_LockSurface(srf);
	bool directionY = sy > y;

	if (directionY)
	{
		for (int yi = 0; yi < sh; ++yi)
		{
			memmove((uint8_t *)srf->pixels + ((yi + y) * srf->pitch + x), (uint8_t *)srf->pixels + ((sy + yi) * srf->pitch + sx), sw);
		}
	}
	else
	{
		for (int yi = sh - 1; yi >= 0; --yi)
		{
			memmove((uint8_t *)srf->pixels + ((yi + y) * srf->pitch + x), (uint8_t *)srf->pixels + ((sy + yi) * srf->pitch + sx), sw);
		}
	}

	SDL_UnlockSurface(srf);
}

static void VL_SDL2_UnmaskedToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_UnmaskedToPAL8(src, surf->pixels, x, y, surf->pitch, w, h);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_UnmaskedToSurface_PM(void *src, void *dst_surface, int x, int y, int w, int h, int mapmask)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_UnmaskedToPAL8_PM(src, surf->pixels, x, y, surf->pitch, w, h, mapmask);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_MaskedToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_MaskedToPAL8(src, surf->pixels, x, y, surf->pitch, w, h);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_MaskedBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_MaskedBlitClipToPAL8(src, surf->pixels, x, y, surf->pitch, w, h, surf->w, surf->h);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_BitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_1bppToPAL8(src, surf->pixels, x, y, surf->pitch, w, h, colour);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_BitToSurface_PM(void *src, void *dst_surface, int x, int y, int w, int h, int colour, int mapmask)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_1bppToPAL8_PM(src, surf->pixels, x, y, surf->pitch, w, h, colour, mapmask);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_BitXorWithSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_1bppXorWithPAL8(src, surf->pixels, x, y, surf->pitch, w, h, colour);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_BitBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_1bppBlitToPAL8(src, surf->pixels, x, y, surf->pitch, w, h, colour);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_BitInvBlitToSurface(void *src, void *dst_surface, int x, int y, int w, int h, int colour)
{
	SDL_Surface *surf = (SDL_Surface *)dst_surface;
	SDL_LockSurface(surf);
	VL_1bppInvBlitClipToPAL8(src, surf->pixels, x, y, surf->pitch, w, h, surf->w, surf->h, colour);
	SDL_UnlockSurface(surf);
}

static void VL_SDL2_ScrollSurface(void *surface, int x, int y)
{
	SDL_Surface *surf = (SDL_Surface *)surface;
	int dx = 0, dy = 0, sx = 0, sy = 0;
	int w = surf->w - CK_Cross_max(x, -x), h = surf->h - CK_Cross_max(y, -y);
	if (x > 0)
	{
		dx = 0;
		sx = x;
	}
	else
	{
		dx = -x;
		sx = 0;
	}
	if (y > 0)
	{
		dy = 0;
		sy = y;
	}
	else
	{
		dy = -y;
		sy = 0;
	}
	VL_SDL2_SurfaceToSelf(surface, dx, dy, sx, sy, w, h);
}

static void VL_SDL2_Present(void *surface, int scrlX, int scrlY, bool singleBuffered)
{
	// TODO: Verify this is a VL_SurfaceUsage_FrontBuffer
	VL_SDL2_ResizeWindow();
	SDL_Surface *surf = (SDL_Surface *)surface;
	SDL_Rect srcr = {(Sint16)scrlX, (Sint16)scrlY, VL_EGAVGA_GFX_WIDTH, VL_EGAVGA_GFX_HEIGHT};
	SDL_Rect renderRect = {(Sint16)vl_renderRgn_x, (Sint16)vl_renderRgn_y, vl_renderRgn_w, vl_renderRgn_h};
	SDL_Rect fullRect = {(Sint16)vl_fullRgn_x, (Sint16)vl_fullRgn_y, vl_fullRgn_w, vl_fullRgn_h};

	SDL_BlitSurface(surf, &srcr, vl_sdl2_stagingSurface, 0);
	SDL_LockSurface(vl_sdl2_stagingSurface);
	SDL_UpdateTexture(vl_sdl2_texture, 0, vl_sdl2_stagingSurface->pixels, vl_sdl2_stagingSurface->pitch);
	SDL_UnlockSurface(vl_sdl2_stagingSurface);
	if (vl_sdl2_scaledTarget)
	{
		SDL_SetRenderTarget(vl_sdl2_renderer, vl_sdl2_scaledTarget);
		SDL_SetRenderDrawColor(vl_sdl2_renderer,
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][0],
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][1],
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][2],
			255);
		SDL_RenderClear(vl_sdl2_renderer);
		SDL_RenderCopy(vl_sdl2_renderer, vl_sdl2_texture, 0, &renderRect);
		SDL_SetRenderTarget(vl_sdl2_renderer, 0);
		SDL_SetRenderDrawColor(vl_sdl2_renderer, 0, 0, 0, 255);
		SDL_RenderClear(vl_sdl2_renderer);

		SDL_RenderSetViewport(vl_sdl2_renderer, 0);
		SDL_RenderCopy(vl_sdl2_renderer, vl_sdl2_scaledTarget, 0, &fullRect);
	}
	else
	{
		SDL_SetRenderTarget(vl_sdl2_renderer, 0);
		SDL_RenderSetViewport(vl_sdl2_renderer, 0);
		SDL_SetRenderDrawColor(vl_sdl2_renderer, 0, 0, 0, 255);
		SDL_RenderClear(vl_sdl2_renderer);

		SDL_RenderSetViewport(vl_sdl2_renderer, 0);
		SDL_RenderSetViewport(vl_sdl2_renderer, &fullRect);
		SDL_SetRenderDrawColor(vl_sdl2_renderer,
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][0],
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][1],
			VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor][2],
			255);
		SDL_RenderFillRect(vl_sdl2_renderer, 0);
		SDL_RenderCopy(vl_sdl2_renderer, vl_sdl2_texture, 0, &renderRect);

	}

	SDL_RenderPresent(vl_sdl2_renderer);
#ifdef __EMSCRIPTEN__
	VL_WaitRAF();
	CK_Cross_NoteYield();
#endif
}

static int VL_SDL2_GetActiveBufferId(void *surface)
{
	(void)surface;
	return 0;
}

static int VL_SDL2_GetNumBuffers(void *surface)
{
	(void)surface;
	return 1;
}

static void VL_SDL2_SyncBuffers(void *surface)
{
	(void)surface;
}

static void VL_SDL2_UpdateRect(void *surface, int x, int y, int w, int h)
{
	(void)surface;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void VL_SDL2_FlushParams()
{
	SDL_SetWindowFullscreen(vl_sdl2_window, vl_isFullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	SDL_SetWindowMinimumSize(vl_sdl2_window, VL_VGA_GFX_SCALED_WIDTH_PLUS_BORDER / VL_VGA_GFX_WIDTH_SCALEFACTOR, VL_VGA_GFX_SCALED_HEIGHT_PLUS_BORDER / VL_VGA_GFX_HEIGHT_SCALEFACTOR);
#if SDL_VERSION_ATLEAST(2, 0, 18)
	SDL_RenderSetVSync(vl_sdl2_renderer, vl_swapInterval);
#endif
	VL_SDL2_ResizeWindow();
}

static void VL_SDL2_WaitVBLs(int vbls)
{
#ifdef __EMSCRIPTEN__
	double end = emscripten_get_now() + vbls * 1000.0 / 70;
	do
		CK_Cross_YieldNow();
	while (emscripten_get_now() < end);
#else
	SDL_Delay(vbls * 1000 / 70);
#endif
}

// Unfortunately, we can't take advantage of designated initializers in C++.
VL_Backend vl_sdl2_backend =
	{
		/*.setVideoMode =*/&VL_SDL2_SetVideoMode,
		/*.createSurface =*/&VL_SDL2_CreateSurface,
		/*.destroySurface =*/&VL_SDL2_DestroySurface,
		/*.getSurfaceMemUse =*/&VL_SDL2_GetSurfaceMemUse,
		/*.getSurfaceDimensions =*/&VL_SDL2_GetSurfaceDimensions,
		/*.refreshPaletteAndBorderColor =*/&VL_SDL2_RefreshPaletteAndBorderColor,
		/*.surfacePGet =*/&VL_SDL2_SurfacePGet,
		/*.surfaceRect =*/&VL_SDL2_SurfaceRect,
		/*.surfaceRect_PM =*/&VL_SDL2_SurfaceRect_PM,
		/*.surfaceToSurface =*/&VL_SDL2_SurfaceToSurface,
		/*.surfaceToSelf =*/&VL_SDL2_SurfaceToSelf,
		/*.unmaskedToSurface =*/&VL_SDL2_UnmaskedToSurface,
		/*.unmaskedToSurface_PM =*/&VL_SDL2_UnmaskedToSurface_PM,
		/*.maskedToSurface =*/&VL_SDL2_MaskedToSurface,
		/*.maskedBlitToSurface =*/&VL_SDL2_MaskedBlitToSurface,
		/*.bitToSurface =*/&VL_SDL2_BitToSurface,
		/*.bitToSurface_PM =*/&VL_SDL2_BitToSurface_PM,
		/*.bitXorWithSurface =*/&VL_SDL2_BitXorWithSurface,
		/*.bitBlitToSurface =*/&VL_SDL2_BitBlitToSurface,
		/*.bitInvBlitToSurface =*/&VL_SDL2_BitInvBlitToSurface,
		/*.scrollSurface =*/&VL_SDL2_ScrollSurface,
		/*.present =*/&VL_SDL2_Present,
		/*.getActiveBufferId =*/&VL_SDL2_GetActiveBufferId,
		/*.getNumBuffers =*/&VL_SDL2_GetNumBuffers,
		/*.syncBuffers =*/&VL_SDL2_SyncBuffers,
		/*.updateRect =*/&VL_SDL2_UpdateRect,
		/*.flushParams =*/&VL_SDL2_FlushParams,
		/*.waitVBLs =*/&VL_SDL2_WaitVBLs,
};

VL_Backend *VL_Impl_GetBackend()
{
	SDL_Init(SDL_INIT_VIDEO);
	return &vl_sdl2_backend;
}

#ifdef WITH_RF_INTERP
// Present a pre-composited ARGB frame of arbitrary size (used by rf_interp.c).
void VL_PresentARGB(const uint32_t *argb, int w, int h, const char *title)
{
	static SDL_Texture *tex;
	static int tw, th;
	VL_SDL2_ResizeWindow();
	if (!tex || tw != w || th != h)
	{
		if (tex) SDL_DestroyTexture(tex);
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		tex = SDL_CreateTexture(vl_sdl2_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		tw = w; th = h;
	}
	if (title) SDL_SetWindowTitle(vl_sdl2_window, title);
	SDL_Rect renderRect = {(Sint16)vl_renderRgn_x, (Sint16)vl_renderRgn_y, vl_renderRgn_w, vl_renderRgn_h};
	SDL_Rect fullRect = {(Sint16)vl_fullRgn_x, (Sint16)vl_fullRgn_y, vl_fullRgn_w, vl_fullRgn_h};
	double pc0 = VL_DebugNowS();
	SDL_UpdateTexture(tex, 0, argb, w * 4);
	// renderRect is in the integer-scaled space (vl_integerWidth x vl_integerHeight),
	// which VL_SDL2_Present renders into vl_sdl2_scaledTarget and stretches to
	// fullRect. The interpolated frame is already 4x, so map renderRect straight
	// into fullRect instead: same picture, no render-target round trip per frame
	// (that, plus glCheckFramebufferStatus, was a large share of a WebGL frame).
	int iw = vl_integerWidth > 0 ? vl_integerWidth : vl_fullRgn_w;
	int ih = vl_integerHeight > 0 ? vl_integerHeight : vl_fullRgn_h;
	SDL_Rect dst = {
		vl_fullRgn_x + (int)((int64_t)vl_renderRgn_x * vl_fullRgn_w / iw),
		vl_fullRgn_y + (int)((int64_t)vl_renderRgn_y * vl_fullRgn_h / ih),
		(int)((int64_t)vl_renderRgn_w * vl_fullRgn_w / iw),
		(int)((int64_t)vl_renderRgn_h * vl_fullRgn_h / ih)};
	(void)renderRect;
	const Uint8 *bc = VL_EGARGBColorTable[vl_emuegavgaadapter.bordercolor];
	SDL_SetRenderTarget(vl_sdl2_renderer, 0);
	SDL_RenderSetViewport(vl_sdl2_renderer, 0);
	SDL_SetRenderDrawColor(vl_sdl2_renderer, 0, 0, 0, 255);
	SDL_RenderClear(vl_sdl2_renderer);
	SDL_SetRenderDrawColor(vl_sdl2_renderer, bc[0], bc[1], bc[2], 255);
	SDL_RenderFillRect(vl_sdl2_renderer, &fullRect);
	SDL_RenderCopy(vl_sdl2_renderer, tex, 0, &dst);
	double pc1 = VL_DebugNowS();
#ifdef __EMSCRIPTEN__
	vl_presentInRaf = VL_InRAF();
	VL_ConsumeRAF(); /* this refresh has its frame; the next one needs a new rAF */
#endif
	SDL_RenderPresent(vl_sdl2_renderer);
	double pc2 = VL_DebugNowS();
	vl_presentWorkS = pc1 - pc0;
#ifdef __EMSCRIPTEN__
	vl_presentWaitS += pc2 - pc1; /* SDL's swap yields (emscripten_sleep(0)) */
#else
	vl_presentWaitS = pc2 - pc1;
#endif
}

#ifdef __EMSCRIPTEN__
/* Browser pacing for rf_interp (keen456-decomp #98). A WebGL draw reaches the screen at
 * this refresh only if it is made inside the requestAnimationFrame callback; a draw made
 * in any later task (after an emscripten_sleep, say) shows a refresh later and is then
 * overdrawn by the next rAF's frame, so that refresh repeats the previous picture.
 * rf_interp therefore waits here *before* choosing alpha and composing, and VL_PresentARGB
 * does not wait after drawing. If we are still inside a rAF callback that has not drawn
 * yet (the sim frame ran in it), return at once and let it draw. */
void VL_WaitPresentSlot(void)
{
	vl_presentWaitS = 0;
	if (VL_InRAF())
		return;
	double t0 = VL_DebugNowS();
	vl_presentRafMs = VL_WaitRAF();
	CK_Cross_NoteYield();
	vl_presentWaitS = VL_DebugNowS() - t0;
}
#endif

// Single-buffered: nothing to flip.
void VL_FlipPageNoPresent(void)
{
}
#endif
