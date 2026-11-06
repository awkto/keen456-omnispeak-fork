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

#ifndef CK_FIX_H
#define CK_FIX_H

#include <stdbool.h>
#include <stdio.h>

/*
 * Compatibility profiles and optional fixes / enhancements.
 *
 * The 'vanilla' profile (the default) behaves exactly like the DOS games.
 * Every deviation from that is a separate, named entry in the table below,
 * with a default for each profile, and is tested at its call site with
 * CK_FIX(Name).
 *
 * Entries marked as affecting the simulation (sim = true) change game state,
 * so they are forced off while a demo is being played back or recorded, and
 * while the playloop dumper (/DUMPFILE) is active, unless /FORCEFIXES is given.
 * Presentation-only entries (sim = false) never change game state, and stay
 * active everywhere.
 *
 * To add a fix: add one CK_FIX_ENTRY line below, then guard the change with
 * 'if (CK_FIX(Name))'. The source file using CK_FIX must #include "ck_fix.h".
 * It can then be toggled with /FIX name, /NOFIX name and
 * the 'fix_<name>' config option.
 */

//            Id,            name,            sim,   vanilla, enhanced, description
#define CK_FIX_TABLE \
	CK_FIX_ENTRY(Interpolation, "interpolation", false, false, true, "Display-rate interpolation (SDL2 renderers only, see /INTERP)") \
	CK_FIX_ENTRY(StallChargeOnce, "stall-charge-once", true, false, true, "After a stall longer than 5 tics, charge the clipped time once instead of twice (keen456-decomp #84 F2)") \
	CK_FIX_ENTRY(ClockContinuity, "clock-continuity", true, false, true, "Keep the game clock continuous across pauses, menus and dialogs (keen456-decomp #84 F1)") \
	CK_FIX_ENTRY(TimeBasedFades, "time-based-fades", false, false, true, "Palette fades take the vanilla 70 Hz duration whatever the refresh rate (keen456-decomp #78)") \
	CK_FIX_ENTRY(CameraSmoothing, "camera-smoothing", false, false, false, "Opt-in: velocity-smooth the displayed camera in pixel/subpixel interpolation (presentation only, keen456-decomp #86)") \
	CK_FIX_ENTRY(Example, "example", false, false, true, "Example entry: logs one line at startup. Copy it to add a fix.")

typedef enum CK_FixId
{
#define CK_FIX_ENTRY(id, name, sim, vanilla, enhanced, desc) CK_FIX_##id,
	CK_FIX_TABLE
#undef CK_FIX_ENTRY
	CK_FIX_COUNT
} CK_FixId;

typedef enum CK_FixProfile
{
	CK_FIX_PROFILE_VANILLA,
	CK_FIX_PROFILE_ENHANCED,
} CK_FixProfile;

typedef struct CK_FixDef
{
	const char *name;
	bool affectsSim;
	bool profileDefault[2]; // Indexed by CK_FixProfile
	const char *description;
} CK_FixDef;

extern const CK_FixDef ck_fixes[CK_FIX_COUNT];
extern const char *ck_fixProfileStrings[];

// Command-line handling, before CK_Fix_Startup(). These return false for an unknown name.
bool CK_Fix_SetProfile(const char *profileName);
bool CK_Fix_SetOverride(const char *fixName, bool enabled);
void CK_Fix_SetForce(bool force);
void CK_Fix_SetDumping(bool dumping);

// Resolves the profile, config ('profile', 'fix_<name>') and overrides.
void CK_Fix_Startup(void);
CK_FixProfile CK_Fix_GetProfile(void);
// Whether the fix is enabled by the profile and options, regardless of demos.
bool CK_Fix_Enabled(CK_FixId id);
void CK_Fix_SetEnabled(CK_FixId id, bool enabled); // runtime toggle (menu)
// Whether the fix should be applied right now.
bool CK_Fix_Active(CK_FixId id);
// Writes the profile and the state of each fix as text (dump sidecar file).
void CK_Fix_WriteInfo(FILE *f);

#define CK_FIX(id) CK_Fix_Active(CK_FIX_##id)

#endif
