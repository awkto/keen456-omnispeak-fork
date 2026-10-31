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

#include "ck_fix.h"
#include "ck_cross.h"
#include "id_cfg.h"
#include "id_in.h"

#include <stdio.h>

const CK_FixDef ck_fixes[CK_FIX_COUNT] = {
#define CK_FIX_ENTRY(id, name, sim, vanilla, enhanced, desc) {name, sim, {vanilla, enhanced}, desc},
	CK_FIX_TABLE
#undef CK_FIX_ENTRY
};

const char *ck_fixProfileStrings[] = {"vanilla", "enhanced", NULL};

static int ck_fixProfile = -1;             // -1: not given on the command line
static int ck_fixOverride[CK_FIX_COUNT];   // 0: none, 1: on, 2: off
static bool ck_fixEnabled[CK_FIX_COUNT];
static bool ck_fixForce = false;
static bool ck_fixDumping = false;

static int CK_Fix_Find(const char *fixName)
{
	for (int i = 0; i < CK_FIX_COUNT; ++i)
		if (!CK_Cross_strcasecmp(fixName, ck_fixes[i].name))
			return i;
	return -1;
}

bool CK_Fix_SetProfile(const char *profileName)
{
	for (int i = 0; ck_fixProfileStrings[i]; ++i)
	{
		if (!CK_Cross_strcasecmp(profileName, ck_fixProfileStrings[i]))
		{
			ck_fixProfile = i;
			return true;
		}
	}
	return false;
}

bool CK_Fix_SetOverride(const char *fixName, bool enabled)
{
	int id = CK_Fix_Find(fixName);
	if (id < 0)
		return false;
	ck_fixOverride[id] = enabled ? 1 : 2;
	return true;
}

void CK_Fix_SetForce(bool force)
{
	ck_fixForce = force;
}

void CK_Fix_SetDumping(bool dumping)
{
	ck_fixDumping = dumping;
}

void CK_Fix_Startup(void)
{
	char cfgName[64];

	// The command line wins over the config file.
	if (ck_fixProfile < 0)
		ck_fixProfile = CFG_GetConfigEnum("profile", ck_fixProfileStrings, CK_FIX_PROFILE_VANILLA);

	for (int i = 0; i < CK_FIX_COUNT; ++i)
	{
		bool enabled = ck_fixes[i].profileDefault[ck_fixProfile];
		snprintf(cfgName, sizeof(cfgName), "fix_%s", ck_fixes[i].name);
		enabled = CFG_GetConfigBool(cfgName, enabled);
		if (ck_fixOverride[i])
			enabled = (ck_fixOverride[i] == 1);
		ck_fixEnabled[i] = enabled;
	}

	CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "Compat profile: %s\n", ck_fixProfileStrings[ck_fixProfile]);
	for (int i = 0; i < CK_FIX_COUNT; ++i)
		if (ck_fixEnabled[i])
			CK_Cross_LogMessage(CK_LOG_MSG_NORMAL, "  fix %s (%s)%s\n", ck_fixes[i].name,
				ck_fixes[i].affectsSim ? "sim" : "presentation",
				(ck_fixes[i].affectsSim && ck_fixDumping && !ck_fixForce) ? ": off while dumping" : "");
}

CK_FixProfile CK_Fix_GetProfile(void)
{
	return (CK_FixProfile)ck_fixProfile;
}

bool CK_Fix_Enabled(CK_FixId id)
{
	return ck_fixEnabled[id];
}

bool CK_Fix_Active(CK_FixId id)
{
	if (!ck_fixEnabled[id])
		return false;
	// Demos and dumps must match DOS: no fix may change the game state there.
	if (ck_fixes[id].affectsSim && !ck_fixForce)
		if (ck_fixDumping || IN_DemoGetMode() != IN_Demo_Off)
			return false;
	return true;
}

void CK_Fix_WriteInfo(FILE *f)
{
	fprintf(f, "profile %s\n", ck_fixProfileStrings[ck_fixProfile]);
	fprintf(f, "forcefixes %d\n", ck_fixForce ? 1 : 0);
	fprintf(f, "# fix <name> <sim|presentation> <enabled> <active-in-dump>\n");
	for (int i = 0; i < CK_FIX_COUNT; ++i)
	{
		bool inDump = ck_fixEnabled[i] && (!ck_fixes[i].affectsSim || ck_fixForce);
		fprintf(f, "fix %s %s %d %d\n", ck_fixes[i].name,
			ck_fixes[i].affectsSim ? "sim" : "presentation",
			ck_fixEnabled[i] ? 1 : 0, inDump ? 1 : 0);
	}
}
