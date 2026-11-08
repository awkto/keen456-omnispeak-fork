/*
Omnispeak: A Commander Keen Reimplementation
Copyright (C) 2012 David Gow <david@ingeniumdigital.com>

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

#ifndef ID_VL_H
#define ID_VL_H

#include <stdbool.h>
#include <stdint.h>

/* Util Functions */

extern bool vl_screenFaded;
extern bool vl_isFullScreen;
extern bool vl_isAspectCorrected;
/* Aspect ratio of the picture (keen456-omnispeak-fork #101). Auto keeps the old behaviour:
 * 4:3 when vl_isAspectCorrected, else stretch to the window. As-is is square pixels (16:10)
 * at whole-number scale. 16:10, 16:9 and Fit are widescreen (#103): the render region is
 * vl_displayGameW game pixels wide (Fit: as wide as the window allows), and 320-wide frames
 * (menus, text screens, vanilla play) are pillarboxed in it, never stretched. */
typedef enum { VL_Aspect_Auto, VL_Aspect_4x3, VL_Aspect_AsIs, VL_Aspect_16x10, VL_Aspect_16x9, VL_Aspect_Fit, VL_Aspect_Count } VL_AspectMode;
extern int vl_aspectMode;
extern const char *vl_aspectStrings[];
/* #103: game pixels (at 200 lines, EGA pixels 1.2 tall) across the render region: 320, or
 * more in the widescreen aspect modes. Only rf_interp.c composes frames that wide. */
#define VL_WIDE_MAX_W 560
extern int vl_displayGameW;
extern bool vl_isIntegerScaled;
extern bool vl_hasOverscanBorder;
extern int vl_swapInterval;
extern bool vl_hiddenCard; //TODO: Use this to enable even unwise fallbacks.
extern bool vl_noPan;

// EGA signal palettes (the 17th entry of each row is the overscan border color)
// NOTE: Vanilla Keen can modify some of these (e.g. the border color)
extern uint8_t vl_palette[6][17];
extern uint16_t vl_border_color;

#if 0
extern VL_EGAPaletteEntry VL_EGAPalette[16];
void VL_SetPalEntry(int id, uint8_t r, uint8_t g, uint8_t b);
#endif

void VL_SetPaletteByID(int id);
void VL_SetPalette(uint8_t *palette);
void VL_SetPaletteFromArray(intptr_t *palArray);
void VL_SetPaletteAndBorderColor(uint8_t *palette);
void VL_ColorBorder(uint16_t color);
void VL_SetDefaultPalette(void);
void VL_FadeToBlack(void);
void VL_FadeFromBlack(void);
//void VL_FadeToWhite(void); // Unused in vanilla Keen 5
//void VL_FadeFromWhite(void); // Unused in vanilla Keen 5
void VL_Clip(int *src_w, int *src_h, int *dst_x, int *dst_y, int dst_w, int dst_h);
void VL_UnmaskedToRGB(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_MaskedToRGBA(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_MaskedBlitToRGB(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_MaskedBlitClipToRGB(void *src, void *dest, int x, int y, int pitch, int w, int h, int dw, int dh);
void VL_1bppToRGBA(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppXorWithRGB(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppBlitToRGB(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppInvBlitToRGB(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
//TODO: 1bppInvBlitClipToRGB

void VL_UnmaskedToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_UnmaskedToPAL8_PM(void *src, void *dest, int x, int y, int pitch, int w, int h, int mapmask);
void VL_MaskedToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_MaskedBlitToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h);
void VL_MaskedBlitClipToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int dw, int dh);
void VL_1bppToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppToPAL8_PM(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour, int mapmask);
void VL_1bppXorWithPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppBlitToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppInvBlitToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int colour);
void VL_1bppInvBlitClipToPAL8(void *src, void *dest, int x, int y, int pitch, int w, int h, int dw, int dh, int colour);

int VL_MemUsed();
int VL_NumSurfaces();

typedef enum VL_SurfaceUsage
{
	VL_SurfaceUsage_Default,
	VL_SurfaceUsage_FrameBuffer,
	VL_SurfaceUsage_FrontBuffer,
	VL_SurfaceUsage_Sprite
} VL_SurfaceUsage;

typedef struct VL_Backend
{
	void (*setVideoMode)(int mode);
	void *(*createSurface)(int w, int h, VL_SurfaceUsage usage);
	void (*destroySurface)(void *surface);
	long (*getSurfaceMemUse)(void *surface);
	void (*getSurfaceDimensions)(void *surface, int *w, int *h);
	void (*refreshPaletteAndBorderColor)(void *screen);
	int (*surfacePGet)(void *screen, int x, int y);
	void (*surfaceRect)(void *dst_surface, int x, int y, int w, int h, int colour);
	void (*surfaceRect_PM)(void *dst_surface, int x, int y, int w, int h, int colour, int mapmask);
	void (*surfaceToSurface)(void *src_surface, void *dst_surface, int x, int y, int sx, int sy, int sw, int sh);
	void (*surfaceToSelf)(void *surface, int x, int y, int sx, int sy, int sw, int sh);
	void (*unmaskedToSurface)(void *src, void *dst_surface, int x, int y, int w, int h);
	void (*unmaskedToSurface_PM)(void *src, void *dst_surface, int x, int y, int w, int h, int mapmask);
	void (*maskedToSurface)(void *src, void *dst_surface, int x, int y, int w, int h);
	void (*maskedBlitToSurface)(void *src, void *dst_surface, int x, int y, int w, int h);
	void (*bitToSurface)(void *src, void *dst_surface, int x, int y, int w, int h, int colour);
	void (*bitToSurface_PM)(void *src, void *dst_surface, int x, int y, int w, int h, int colour, int mapmask);
	void (*bitXorWithSurface)(void *src, void *dst_surface, int x, int y, int w, int h, int colour);
	void (*bitBlitToSurface)(void *src, void *dst_surface, int x, int y, int w, int h, int colour);
	void (*bitInvBlitToSurface)(void *src, void *dst_surface, int x, int y, int w, int h, int colour);
	void (*scrollSurface)(void *surface, int x, int y);
	void (*present)(void *surface, int scrollXpx, int scrollYpx, bool singleBuffered);
	int (*getActiveBufferId)(void *surface);
	int (*getNumBuffers)(void *surface);
	void (*syncBuffers)(void *surface);
	void (*updateRect)(void *surface, int x, int y, int w, int h);
	void (*flushParams)();
	void (*waitVBLs)(int vbls);
} VL_Backend;

void VL_InitScreen(void);
void VL_Startup();
void VL_Shutdown();
void VL_ResizeScreen(int w, int h);
void VL_SetParams(bool isFullScreen, bool isAspectCorrected, bool hasOverscan, bool isIntegerScaled);
void VL_ToggleFullscreen();
void VL_ToggleAspect();
void VL_ToggleBorder();
void VL_ToggleInteger();
void VL_ToggleVSync();
void *VL_CreateSurface(int w, int h);
void VL_DestroySurface(void *surf);
void *VL_SetScreen(void *surf);
int VL_SurfacePGet(void *surf, int x, int y);
void VL_SurfaceRect(void *dst, int x, int y, int w, int h, int colour);
void VL_ScreenRect(int x, int y, int w, int h, int colour);
void VL_ScreenRect_PM(int x, int y, int w, int h, int colour);
void VL_ScreenToScreen(int x, int y, int sx, int sy, int sw, int sh);
void VL_SurfaceToSurface(void *src, void *dst, int x, int y, int sx, int sy, int sw, int sh);
void VL_SurfaceToScreen(void *src, int x, int y, int sx, int sy, int sw, int sh);
void VL_SurfaceToSelf(void *surf, int x, int y, int sx, int sy, int sw, int sh);
void VL_UnmaskedToSurface(void *src, void *dest, int x, int y, int w, int h);
void VL_UnmaskedToScreen(void *src, int x, int y, int w, int h);
void VL_UnmaskedToScreen_PM(void *src, int x, int y, int w, int h);
void VL_MaskedToSurface(void *src, void *dest, int x, int y, int w, int h);
void VL_MaskedBlitToSurface(void *src, void *dest, int x, int y, int w, int h);
void VL_MaskedToScreen(void *src, int x, int y, int w, int h);
void VL_MaskedBlitToScreen(void *src, int x, int y, int w, int h);
void VL_1bppToScreen(void *src, int x, int y, int w, int h, int colour);
void VL_1bppToScreen_PM(void *src, int x, int y, int w, int h, int colour);
void VL_1bppXorWithScreen(void *src, int x, int y, int w, int h, int colour);
void VL_1bppBlitToScreen(void *src, int x, int y, int w, int h, int colour);
void VL_1bppInvBlitToScreen(void *src, int x, int y, int w, int h, int colour);
void VL_ScrollScreen(int x, int y);

void VL_DelayTics(int tics);
int VL_GetTics(int wait);
void VL_Yield();
void VL_SetScrollCoords(int x, int y);
int VL_GetScrollX(void);
int VL_GetScrollY(void);
void VL_ClearScreen(int colour);
void VL_SetMapMask(int mapmask);
int VL_GetActiveBuffer();
int VL_GetNumBuffers();
void VL_FixRefreshBuffer();
void VL_UpdateRect(int x, int y, int w, int h);
void VL_SwapOnNextPresent();
void VL_Present();

// keen456-decomp #75
void RF_DebugDumpFrame(void);

VL_Backend *VL_Impl_GetBackend(void);

#endif //ID_VL_H
