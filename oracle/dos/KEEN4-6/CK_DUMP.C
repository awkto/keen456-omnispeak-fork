/* Reconstructed Commander Keen 4-6 Source Code: KEENDUMP instrumentation
 * Copyright (C) 2026 keen456-decomp contributors
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

/*
 * CK_DUMP.C: per-frame state trace (keen456-decomp #57, docs/TRACE_FORMAT.md).
 *
 * Compiled only with -DKEENDUMP, as part of CK_PLAY.C (#include at its end), so
 * the normal build does not change and no link list has to be edited.
 *
 * Command line (KEENDUMP builds only; same names as Omnispeak's debug build):
 *   /DUMPFILE <file>  write one record per PlayLoop iteration to <file>
 *   /PLAYDEMO <n>     after InitGame, play built-in demo n (0-4) once, then quit
 * Use with /NOWAIT, or the start-up text screen waits for a key.
 *
 * Record = Omnispeak's playloop dumper (ck_play.c, CK_ENABLE_PLAYLOOP_DUMPER):
 *   longword TimeCount, gametype gamestate, objtype objarray[MAXACTORS]
 * written raw: the DOS structs are exactly the DOS save-game layout that
 * Omnispeak's CK_SaveGameState/CK_SaveObject reproduce field by field.
 *
 * DGROUP RULE: the trace records near pointers (state, next/prev, riding), so
 * this file must not move anything in DGROUP. No near globals or statics, no
 * string literals (both live in DGROUP and would shift every _BSS/_COMDEF_
 * variable, e.g. objarray). Persistent data is "far" (its own segment); text
 * is built on the stack. tools/keendump.sh checks the .MAP against the normal
 * build's and fails if any DGROUP symbol moved.
 */

static char far kd_dumpfile[] = "DUMPFILE";
static char far kd_playdemo[] = "PLAYDEMO";
static char far kd_eopen[] = "KEENDUMP: cannot create dump file";
static char far kd_ewrite[] = "KEENDUMP: write to dump file failed";
static char far kd_edemo[] = "KEENDUMP: /PLAYDEMO needs 0-4";
static Sint16 far kd_handle = -1;

/* case-insensitive whole-word match of "/NAME" or "-NAME" (name in upper case) */
static boolean KD_IsParm(char *arg, char far *name)
{
	char a, n;

	if (*arg == '/' || *arg == '-')
		arg++;
	do
	{
		a = *arg++;
		n = *name++;
		if (a >= 'a' && a <= 'z')
			a -= 'a' - 'A';
		if (a != n)
			return false;
	} while (n);
	return true;
}

/* Quit() with a far message: copy it to the stack (near, SS == DS) first */
static void KD_Fail(char far *msg)
{
	char buf[48];
	Sint16 i;

	for (i = 0; i < sizeof(buf)-1 && msg[i]; i++)
		buf[i] = msg[i];
	buf[i] = 0;
	Quit(buf);
}

static void KD_Close(void)
{
	if (kd_handle != -1)
	{
		CA_FarWrite(kd_handle, (byte far *)&kd_handle, 0);	// 0-byte write = truncate here
		close(kd_handle);
		kd_handle = -1;
	}
}

/*
==================
=
= KD_Frame: one trace record. Called from PlayLoop after the think pass,
= before HandleRiding (the point where Omnispeak's dumper writes).
=
==================
*/

void KD_Frame(void)
{
	longword tc;

	if (kd_handle == -1)
		return;
	asm	pushf
	asm	cli
	tc = TimeCount;		// the timer ISR writes it; read both words at once
	asm	popf
	if (!CA_FarWrite(kd_handle, (byte far *)&tc, sizeof(tc))
		|| !CA_FarWrite(kd_handle, (byte far *)&gamestate, sizeof(gamestate))
		|| !CA_FarWrite(kd_handle, (byte far *)objarray, sizeof(objarray)))
	{
		close(kd_handle);
		kd_handle = -1;
		KD_Fail(kd_ewrite);
	}
}

/*
==================
=
= KD_Main: called from main() after InitGame, before DemoLoop
=
==================
*/

void KD_Main(void)
{
	Sint16 i, demo;
	char *file;

	demo = -1;
	file = NULL;
	for (i = 1; i < _argc; i++)
	{
		if (i+1 < _argc && KD_IsParm(_argv[i], kd_dumpfile))
			file = _argv[++i];
		else if (i+1 < _argc && KD_IsParm(_argv[i], kd_playdemo))
		{
			demo = atoi(_argv[++i]);
			if (demo < 0 || demo > 4)
				KD_Fail(kd_edemo);
		}
	}
	if (file)
	{
		kd_handle = open(file, O_BINARY|O_WRONLY|O_CREAT, S_IFREG|S_IREAD|S_IWRITE);
		if (kd_handle == -1)
			KD_Fail(kd_eopen);
	}
	if (demo == -1)
		return;		// attract loop / game as usual, traced if /DUMPFILE

	RunDemo(demo);
	KD_Close();
	Quit(NULL);
}
