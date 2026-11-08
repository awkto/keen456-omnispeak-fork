/* Reconstructed Commander Keen 4-6 Source Code
 * Copyright (C) 2021-2026 K1n9_Duk3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __AUDIO_H__
#define __AUDIO_H__

//#include "VERSION.H"

/////////////////////////////////////////////////
//
// MUSE Header for .CK6
//
/////////////////////////////////////////////////

#define NUMSOUNDS     LASTSOUND
#define NUMSNDCHUNKS  ((3*LASTSOUND)+LASTMUSIC)

//
// Sound names & indexes
//
typedef enum {
	WORLDWALK1SND,        // 0
	WORLDWALK2SND,        // 1
	JUMPSND,              // 2
	LANDSND,              // 3
	KEENFIRESND,          // 4
	DROPKEYSND,           // 5
	BLORBBOUNCESND,       // 6
	POGOBOUNCESND,        // 7
	GETPOINTSSND,         // 8
	GETAMMOSND,           // 9
	GETDROPSND,           // 10
	GETDROP2SND,          // 11 (never used)
	ENTERLEVELSND,        // 12
	LEVELDONESND,         // 13
	NOWAYSND,             // 14
	HITHEADSND,           // 15
	SND_16,               // 16
	EXTRAKEENSND,         // 17
	OPENDOORSND,          // 18
	GETKEYSND,            // 19
	PLUMMETSND,           // 20
	USESWITCHSND,         // 21
	BIPSQUISHSND,         // 22
	KEENDEADSND,          // 23
	BIPSHIPEXPLODESND,    // 24
	SHOTEXPLODESND,       // 25
	BOBBAJUMPSND,         // 26
	BOBBALANDSND,         // 27
	BOUNCE1SND,           // 28 (unused in Keen 6)
	ENEMYSHOTSND,         // 29
	ENEMYSHOTEXPLODESND,  // 30
	BOBBASHOTSND,         // 31
	SND_32,               // 32
	GRABSATELLITESND,     // 33
	SHOWSTATUSSND,        // 34
	HIDESTATUSSND,        // 35
	GIKJUMPSND,           // 36
	GIKLANDSND,           // 37
	ORBATRIXBOUNCESND,    // 38
	SND_39,               // 39
	SND_40,               // 40
	TELEPORTSND,          // 41
	SHOTBOUNCESND,        // 42
	FLAGSPINSND,          // 43
	FLAGLANDSND,          // 44
	QUESTITEMSND,         // 45
	KEENPADDLESND,        // 46
	BALLBOUNCESND,        // 47
	COMPPADDLESND,        // 48
	COMPSCOREDSND,        // 49
	KEENSCOREDSND,        // 50
	CEILICKATTACKSND,     // 51
	SMASHSND,             // 52
	THROWROPESND,         // 53
	ROCKETFLYSND,         // 54
	CEILICKLAUGHSND,      // 55
	ROCKETSTARTSND,       // 56
	GRABBITERSND,         // 57
	STOMPSND,             // 58
	FLAMESND,             // 59
	LASTSOUND
} soundnames;

//
// Base offsets
//
#define STARTPCSOUNDS     0
#define STARTADLIBSOUNDS  (STARTPCSOUNDS+NUMSOUNDS)
#define STARTDIGISOUNDS   (STARTADLIBSOUNDS+NUMSOUNDS)
#define STARTMUSIC        (STARTDIGISOUNDS+NUMSOUNDS)

//
// Music names & indexes
//
typedef enum {
	WONDER_MUS,
	BRERTAR_MUS,
	TOFUTURE_MUS,
	FASTER_MUS,
	SPACFUNK_MUS,
	ALIENATE_MUS,
	OMINOUS_MUS,
	METAL_MUS,
	MAMSNAKE_MUS,
	LASTMUSIC
} musicnames;

/////////////////////////////////////////////////
//
// Thanks for playing with MUSE!
//
/////////////////////////////////////////////////

#endif