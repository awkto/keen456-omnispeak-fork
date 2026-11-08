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
// MUSE Header for .CK5
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
	MINEEXPLODESND,       // 5
	SLICESTARBOUNCESND,   // 6
	POGOBOUNCESND,        // 7
	GETPOINTSSND,         // 8
	GETAMMOSND,           // 9
	GETDROPSND,           // 10
	GETDROP2SND,          // 11 (never used)
	ENTERLEVELSND,        // 12
	LEVELDONESND,         // 13
	NOWAYSND,             // 14
	HITHEADSND,           // 15
	BOUNCESND,            // 16
	EXTRAKEENSND,         // 17
	OPENCARDDOORSND,      // 18
	GETKEYSND,            // 19
	PLUMMETSND,           // 20
	USESWITCHSND,         // 21
	SQUISHSND,            // 22
	KEENDEADSND,          // 23
	SND_24,               // 24
	SHOTEXPLODESND,       // 25
	SND_26,               // 26
	SPIROGRABSND,         // 27
	SPINDREDBOUNCESND,    // 28
	ENEMYSHOTSND,         // 29
	ENEMYSHOTEXPLODESND,  // 30
	AMPTONWALK1SND,       // 31
	AMPTONWALK2SND,       // 32
	AMPTONDIESND,         // 33
	SHOWSTATUSSND,        // 34
	HIDESTATUSSND,        // 35
	SHELLEYEXPLODESND,    // 36
	SPINDREDFLIPSND,      // 37
	MASTERATTACKSND,      // 38
	MASTERBLASTSND,       // 39
	SHIKADIATTACKSND,     // 40
	TELEPORTSND,          // 41
	SHOCKSHUNDBARKSND,    // 42
	FLAGSPINSND,          // 43
	FLAGLANDSND,          // 44
	SHOCKBALLEXPLODESND,  // 45
	KEENPADDLESND,        // 46
	BALLBOUNCESND,        // 47
	COMPPADDLESND,        // 48
	COMPSCOREDSND,        // 49
	KEENSCOREDSND,        // 50
	SND_51,               // 51
	BIGSPARKSND,          // 52
	GAMEOVER1SND,         // 53
	GAMEOVER2SND,         // 54
	GETKEYCARDSND,        // 55
	SND_56,               // 56
	LANDONFUSESND,        // 57
	SPARKYCHARGESND,      // 58
	SPHEREFULBOUNCESND,   // 59
	OPENDOORSND,          // 60
	SPIROFLYSND,          // 61
	SND_62,               // 62
	ELEVATORDOORSND,      // 63
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
	CAMEIN_MUS,
	HAVING_T_MUS,
	SKATING_MUS,
	SNOOPING_MUS,
	BAGPIPES_MUS,
	WEDNESDY_MUS,
	ROCK_ME_MUS,
	BREATHE_MUS,
	SHIKAIRE_MUS,
	SPHEREFUL_MUS,
	TIGHTER_MUS,
	ROBOROCK_MUS,
	FANFARE_MUS,
	FEARSOME_MUS,
	LASTMUSIC
} musicnames;

/////////////////////////////////////////////////
//
// Thanks for playing with MUSE!
//
/////////////////////////////////////////////////

#endif