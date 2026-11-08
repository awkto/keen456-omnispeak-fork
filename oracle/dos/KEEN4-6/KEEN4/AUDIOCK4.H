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
// MUSE Header for .CK4
//
/////////////////////////////////////////////////

#define NUMSOUNDS     LASTSOUND
#define NUMSNDCHUNKS  ((3*LASTSOUND)+LASTMUSIC)

//
// Sound names & indexes
//
typedef enum {
	WORLDWALK1SND,
	WORLDWALK2SND,
	JUMPSND,
	LANDSND,
	KEENFIRESND,
	WORMOUTHATTACKAND,
	SND_6,
	POGOBOUNCESND,
	GETPOINTSSND,
	GETAMMOSND,
	GETDROPSND,
	GETDROP2SND,	// never used
	ENTERLEVELSND,
	LEVELDONESND,
	NOWAYSND,
	HITHEADSND,
	BOUNCE2SND,
	EXTRAKEENSND,
	OPENDOORSND,
	GETKEYSND,
	PLUMMETSND,
	USESWITCHSND,
	SQUISHSND,
	KEENDEADSND,
	SND_24,
	SHOTEXPLODESND,
	SWIM1SND,
	SWIM2SND,
	BOUNCE1SND,
	EATBONUSSND,
	TREASUREEATERVANISHSND,
	LINDSEYSND,
	LICKATTACKSND,
	BERKELOIDATTACKSND,
	SHOWSTATUSSND,
	HIDESTATUSSND,
	BLUBSND,
	MINEEXPLODESND,
	SPRITEFIRESND,
	THUNDERSND,
	FIREBALLLANDSND,
	SHOOTDARTSND,
	BURPSND,
	FLAGSPINSND,
	FLAGLANDSND,
	MAKEFOOTSND,
	SLUGPOOSND,
	KEENPADDLESND,
	BALLBOUNCESND,
	COMPPADDLESND,
	COMPSCOREDSND,
	KEENSCOREDSND,
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
	SHADOWS_MUS,
	VEGGIES_MUS,
	TOOHOT_MUS,
	OASIS_MUS,
	KICKPANT_MUS,
	WONDER_MUS,
	LASTMUSIC
} musicnames;

/////////////////////////////////////////////////
//
// Thanks for playing with MUSE!
//
/////////////////////////////////////////////////

#endif