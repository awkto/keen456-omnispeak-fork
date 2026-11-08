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

#ifndef RF_INTERP_H
#define RF_INTERP_H

#include <stdbool.h>
#include "id_rf.h"

// Display-rate interpolation (presentation only; the simulation is untouched).
// Only built with the SDL2 renderer backends (WITH_RF_INTERP).
typedef enum RFI_Mode
{
	RFI_Mode_Off,      // Vanilla: present once per sim frame (default).
	RFI_Mode_Grid,     // Display-rate presenter, no interpolation, vanilla 2px grid (A/B comparison).
	RFI_Mode_Pixel,    // Interpolated, snapped to whole 320x200 pixels.
	RFI_Mode_Subpixel, // Interpolated, snapped to 1/4 pixel.
} RFI_Mode;

// Config/command-line strings, indexed by RFI_Mode.
extern const char *rfi_modeStrings[];

void RFI_Startup(RFI_Mode mode);
bool RFI_Enabled(void);
RFI_Mode RFI_GetMode(void);
void RFI_SetMode(RFI_Mode mode); // runtime change (menu); Off disables
bool RFI_DebugDeterministic(void);
void RFI_NoteSprite(RF_SpriteDrawEntry *sde, int unitX, int unitY, bool fresh);
void RFI_Reset(void); // keen456-decomp #76
void RFI_Frame(void);
// keen456-omnispeak-fork #103: widescreen presents outside RFI_Frame (see rf_interp.c)
bool RFI_PresentWideStill(void);
void RFI_WideSceneEnd(void);

#endif
