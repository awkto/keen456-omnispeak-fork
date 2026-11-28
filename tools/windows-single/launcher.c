// SPDX-License-Identifier: GPL-2.0-or-later
// keen456.exe: small GUI-subsystem launcher for the Windows single-folder build. Mirrors
// tools/linux-single/runner.sh: friendly options -> omnispeak.exe engine args, K6 table choice by
// EGAGRAPH.CK6 size, saves in %APPDATA%\keen456\keen<N>, extra options in %APPDATA%\keen456\args.txt.
// Like runner.sh, it assembles tables + game files in a run dir (%APPDATA%\keen456\run\keen<N>) and starts
// the engine there (the engine wants both in one directory; copies are only a few MB).
// Usage: keen456.exe [4|5|6] [options] [/ENGINEARG ...]   (see README.txt)
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MAXA 256
#define PL 4096

static void die(const wchar_t *fmt, ...)
{
	wchar_t buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vswprintf(buf, 2048, fmt, ap);
	va_end(ap);
	MessageBoxW(NULL, buf, L"keen456", MB_OK | MB_ICONERROR);
	exit(1);
}

static const wchar_t *USAGE =
L"keen456 - Commander Keen 4/5/6, smooth, widescreen\n\n"
L"Usage: keen456.exe [4|5|6] [options] [/ENGINEARG ...]\n\n"
L"  --wide[=16:9|16:10|fit]   widescreen view (default 16:9)\n"
L"  --no-wide                 original 4:3\n"
L"  --smooth=off|grid|pixel|subpixel   (default subpixel)\n"
L"  --no-camera               turn off camera smoothing\n"
L"  --filter=NAME             off scanlines crt curved rgb soft\n"
L"  --pixels=NAME             crisp light low medium high\n"
L"  --classic-controls        no modern pogo\n"
L"  --vanilla                 original behaviour: 4:3, no smoothing, no fixes\n"
L"  --xray                    hidden passages see-through (F10+H in game)\n"
L"  --bright=0..4              filter brightness: 0 off, 1 low, 2 medium (default), 3 full, 4 bright\n"
L"  --scanstyle=0|1|2         scanline style: 0 fine (default), 1 strong, 2 thick (a band per two rows)\n"
L"  --zoom=0|1|2              zoom: 0 normal, 1 out 1 (default), 2 out 2\n"
L"  --camera=0|1|2|3          camera dead zone: 0 vanilla, 1 scaled with the zoom (default),\n"
L"                            2 loose, 3 very loose (Keen gets near the edge before it scrolls)\n"
L"  --fullscreen              start fullscreen (F11 toggles)\n"
L"  --border                  draw the original cyan EGA border\n"
L"  --data DIR                game files (DIR holding keen4/ keen5/ keen6/ or the .CKx files)\n\n"
L"In game: V cycles the screen filter, B its brightness, Z the zoom, C the camera dead zone, F10+H x-ray (V/B/Z/C remembered).\n"
L"Extra options: %APPDATA%\\keen456\\args.txt. Saves: %APPDATA%\\keen456\\keen<N>\\";

static int fexists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int isdir(const wchar_t *p)
{
	DWORD a = GetFileAttributesW(p);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_data(const wchar_t *dir, int ep)
{
	static const wchar_t *n[] = {L"EGAGRAPH", L"GAMEMAPS", L"AUDIO"};
	wchar_t p[PL];
	for (int i = 0; i < 3; i++)
	{
		swprintf(p, PL, L"%ls\\%ls.CK%d", dir, n[i], ep);
		if (!fexists(p))
			return 0;
	}
	return 1;
}

static int find_data(int ep, const wchar_t *bases[], int nb, wchar_t *out)
{
	for (int i = 0; i < nb; i++)
	{
		if (!bases[i] || !bases[i][0])
			continue;
		wchar_t d[PL];
		swprintf(d, PL, L"%ls\\keen%d", bases[i], ep);
		if (isdir(d) && has_data(d, ep)) { wcscpy(out, d); return 1; }
		if (isdir(bases[i]) && has_data(bases[i], ep)) { wcscpy(out, bases[i]); return 1; }
	}
	return 0;
}

static void mkdirs(const wchar_t *p)
{
	wchar_t t[PL];
	wcscpy(t, p);
	for (wchar_t *c = t + 3; *c; c++)
		if (*c == L'\\') { *c = 0; CreateDirectoryW(t, NULL); *c = L'\\'; }
	CreateDirectoryW(t, NULL);
}

static void clear_dir(const wchar_t *d)
{
	wchar_t pat[PL], fp[PL];
	WIN32_FIND_DATAW fd;
	swprintf(pat, PL, L"%ls\\*", d);
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		swprintf(fp, PL, L"%ls\\%ls", d, fd.cFileName);
		SetFileAttributesW(fp, FILE_ATTRIBUTE_NORMAL);
		DeleteFileW(fp);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void copy_dir(const wchar_t *from, const wchar_t *to)
{
	wchar_t pat[PL], a[PL], b[PL];
	WIN32_FIND_DATAW fd;
	swprintf(pat, PL, L"%ls\\*", from);
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		swprintf(a, PL, L"%ls\\%ls", from, fd.cFileName);
		swprintf(b, PL, L"%ls\\%ls", to, fd.cFileName);
		if (!CopyFileW(a, b, FALSE)) die(L"Cannot copy %ls to %ls (error %lu)", a, b, GetLastError());
		SetFileAttributesW(b, FILE_ATTRIBUTE_NORMAL);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static wchar_t *A[MAXA]; // collected user args (args.txt then command line)
static int nA;
static void addA(const wchar_t *s)
{
	if (nA >= MAXA) die(L"too many arguments");
	A[nA++] = _wcsdup(s);
}

static wchar_t cmd[32768];
static void put(const wchar_t *s) // append one quoted engine arg
{
	size_t l = wcslen(cmd);
	if (l + wcslen(s) + 4 >= 32768) die(L"command line too long");
	swprintf(cmd + l, 32768 - l, (wcschr(s, L' ') || !*s) ? L" \"%ls\"" : L" %ls", s);
}

static int in_list(const wchar_t *const *l, const wchar_t *v)
{
	for (; *l; l++) if (!wcscmp(*l, v)) return 1;
	return 0;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE p, LPWSTR cl, int sw)
{
	wchar_t exe[PL], dir[PL], appdata[PL] = L"", cfgdir[PL], cfg[PL];
	GetModuleFileNameW(NULL, exe, PL);
	wcscpy(dir, exe);
	*wcsrchr(dir, L'\\') = 0;
	if (!GetEnvironmentVariableW(L"APPDATA", appdata, PL))
		wcscpy(appdata, dir);
	swprintf(cfgdir, PL, L"%ls\\keen456", appdata);
	swprintf(cfg, PL, L"%ls\\args.txt", cfgdir);

	// args.txt first (so the command line wins), then the real command line
	FILE *f = _wfopen(cfg, L"rb");
	if (f)
	{
		static char buf[8192];
		size_t n = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[n] = 0;
		char *s = buf;
		if ((unsigned char)s[0] == 0xEF) s += 3; // UTF-8 BOM
		for (char *c = s; *c;)
		{
			if (*c == '#') { while (*c && *c != '\n') c++; continue; }
			if (*c <= ' ') { c++; continue; }
			char *e = c;
			while (*e > ' ' && *e != '#') e++;
			char sv = *e; *e = 0;
			wchar_t w[1024];
			MultiByteToWideChar(CP_UTF8, 0, c, -1, w, 1024);
			addA(w);
			*e = sv; c = e;
		}
	}
	int argc;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; i++) addA(argv[i]);

	int ep = 0, camera = 1, modern = 1, vanilla = 0, xray = 0, fs = 0, border = 0;
	int ew = 0, es = 0, ec = 0, ectl = 0;
	wchar_t dz[4] = L"", wide[16] = L"16:9", smooth[16] = L"subpixel", filter[16] = L"", pixels[16] = L"", zoom[4] = L"", bright[4] = L"", scanstyle[4] = L"";
	wchar_t data_opt[PL] = L"";
	wchar_t *raw[MAXA]; int nraw = 0;
	static const wchar_t *flt[] = {L"off", L"scanlines", L"crt", L"curved", L"rgb", L"soft", 0};
	static const wchar_t *pxl[] = {L"crisp", L"light", L"low", L"medium", L"high", 0};
	static const wchar_t *smo[] = {L"off", L"grid", L"pixel", L"subpixel", 0};

#define IS(x) (!wcscmp(a, x))
#define PRE(x) (!wcsncmp(a, x, wcslen(x)))
	for (int i = 0; i < nA; i++)
	{
		const wchar_t *a = A[i];
		if (IS(L"4") || IS(L"5") || IS(L"6")) ep = a[0] - L'0';
		else if (IS(L"-h") || IS(L"--help") || IS(L"-?") || IS(L"/?")) { MessageBoxW(NULL, USAGE, L"keen456", MB_OK); return 0; }
		else if (IS(L"--wide")) { wcscpy(wide, L"16:9"); ew = 1; }
		else if (IS(L"--wide=16:9") || IS(L"--wide=16:10") || IS(L"--wide=fit")) { wcscpy(wide, a + 7); ew = 1; }
		else if (PRE(L"--wide=")) die(L"bad %ls (use 16:9, 16:10 or fit)", a);
		else if (IS(L"--no-wide")) { wide[0] = 0; ew = 1; }
		else if (PRE(L"--smooth="))
		{
			if (!in_list(smo, a + 9)) die(L"bad %ls (use off, grid, pixel or subpixel)", a);
			wcscpy(smooth, a + 9); es = 1;
		}
		else if (IS(L"--no-camera")) { camera = 0; ec = 1; }
		else if (IS(L"--camera")) { camera = 1; ec = 1; }
		else if (PRE(L"--filter="))
		{
			if (!in_list(flt, a + 9)) die(L"bad %ls (use off scanlines crt curved rgb soft)", a);
			wcscpy(filter, a + 9);
		}
		else if (PRE(L"--pixels="))
		{
			if (!in_list(pxl, a + 9)) die(L"bad %ls (use crisp light low medium high)", a);
			wcscpy(pixels, a + 9);
		}
		else if (IS(L"--bright=0") || IS(L"--bright=1") || IS(L"--bright=2") || IS(L"--bright=3") || IS(L"--bright=4")) wcscpy(bright, a + 9);
		else if (PRE(L"--bright=")) die(L"bad %ls (use 0 to 4)", a);
		else if (IS(L"--scanstyle=0") || IS(L"--scanstyle=1") || IS(L"--scanstyle=2")) wcscpy(scanstyle, a + 12);
		else if (PRE(L"--scanstyle=")) die(L"bad %ls (use 0 to 4)", a);
		else if (IS(L"--zoom=0") || IS(L"--zoom=1") || IS(L"--zoom=2")) wcscpy(zoom, a + 7);
		else if (PRE(L"--zoom=")) die(L"bad %ls (use 0, 1 or 2)", a);
		else if (IS(L"--camera=0") || IS(L"--camera=1") || IS(L"--camera=2") || IS(L"--camera=3")) wcscpy(dz, a + 9);
		else if (PRE(L"--camera=")) die(L"bad %ls (use 0, 1, 2 or 3)", a);
		else if (IS(L"--classic-controls")) { modern = 0; ectl = 1; }
		else if (IS(L"--vanilla")) vanilla = 1;
		else if (IS(L"--xray")) xray = 1;
		else if (IS(L"--fullscreen")) fs = 1;
		else if (IS(L"--border")) border = 1;
		else if (IS(L"--no-border")) border = 0;
		else if (PRE(L"--data=")) wcsncpy(data_opt, a + 7, PL - 1);
		else if (IS(L"--data")) { if (++i >= nA) die(L"--data needs a directory"); wcsncpy(data_opt, A[i], PL - 1); }
		else raw[nraw++] = (wchar_t *)a;
	}
	if (vanilla)
	{
		if (!ew) wide[0] = 0;
		if (!es) wcscpy(smooth, L"off");
		if (!ec) camera = 0;
		if (!ectl) modern = 0;
	}
	if (!ep) ep = 4;

	// game data
	wchar_t kd[PL] = L"", dd[PL], shipped[PL], user[PL];
	GetEnvironmentVariableW(L"KEEN_DATA", kd, PL);
	swprintf(shipped, PL, L"%ls\\data", dir);
	swprintf(user, PL, L"%ls\\data", cfgdir);
	const wchar_t *bases[] = {data_opt, kd, shipped, user};
	if (!find_data(ep, bases, 4, dd))
		die(L"No game files for Keen %d found.\n\nNeed EGAGRAPH.CK%d, GAMEMAPS.CK%d and AUDIO.CK%d.\n"
		    L"Use: keen456.exe %d --data DIR   (or put them in %ls\\keen%d)", ep, ep, ep, ep, ep, user, ep);

	// engine tables (copied into the run dir below)
	wchar_t tabs[PL];
	const wchar_t *tn = ep == 4 ? L"k4" : ep == 5 ? L"k5" : L"k6e14";
	if (ep == 6)
	{
		wchar_t eg[PL];
		swprintf(eg, PL, L"%ls\\EGAGRAPH.CK6", dd);
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (GetFileAttributesExW(eg, GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow == 464662)
			tn = L"k6e15";
	}
	swprintf(tabs, PL, L"%ls\\tables\\%ls", dir, tn);
	if (!isdir(tabs)) die(L"Missing engine tables folder:\n%ls", tabs);

	wchar_t save[PL];
	swprintf(save, PL, L"%ls\\keen%d", cfgdir, ep);
	mkdirs(save);

	// run dir: tables + the three game files, like runner.sh
	wchar_t run[PL], src[PL], dst[PL];
	swprintf(run, PL, L"%ls\\run\\keen%d", cfgdir, ep);
	mkdirs(run);
	clear_dir(run);
	copy_dir(tabs, run);
	static const wchar_t *gn[] = {L"EGAGRAPH", L"GAMEMAPS", L"AUDIO"};
	for (int k = 0; k < 3; k++)
	{
		swprintf(src, PL, L"%ls\\%ls.CK%d", dd, gn[k], ep);
		swprintf(dst, PL, L"%ls\\%ls.CK%d", run, gn[k], ep);
		if (!CopyFileW(src, dst, FALSE)) die(L"Cannot copy %ls (error %lu)", src, GetLastError());
		SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
	}

	// engine args
	swprintf(cmd, 32768, L"\"%ls\\omnispeak.exe\"", dir);
	wchar_t t[PL];
	swprintf(t, PL, L"%d", ep);
	put(L"/EPISODE"); put(t);
	put(L"/USERPATH"); put(save);
	put(L"/PROFILE"); put(vanilla ? L"vanilla" : L"enhanced");
	if (!wcscmp(smooth, L"off")) put(L"/NOINTERP"); else { put(L"/INTERP"); put(smooth); }
	if (camera && wcscmp(smooth, L"off")) { put(L"/FIX"); put(L"camera-smoothing"); }
	if (modern && !vanilla) { put(L"/MODERNCONTROLS"); put(L"/POGOHOLD"); put(L"180"); } else put(L"/NOMODERNCONTROLS");
	if (wide[0]) { put(L"/ASPECT"); put(wide); put(L"/FIX"); put(L"widescreen"); } else { put(L"/ASPECT"); put(L"auto"); }
	if (filter[0]) { put(L"/FILTER"); put(filter); }
	if (pixels[0]) { put(L"/PIXELS"); put(pixels); }
	if (bright[0]) { put(L"/FILTERBRIGHT"); put(bright); }
	if (scanstyle[0]) { put(L"/SCANSTYLE"); put(scanstyle); }
	if (xray) put(L"/XRAY");
	if (zoom[0]) { put(L"/ZOOM"); put(zoom); }
	if (dz[0]) { put(L"/CAMERA"); put(dz); }
	if (fs) put(L"/FULLSCREEN");
	if (!border) put(L"/NOBORDER");
	for (int i = 0; i < nraw; i++) put(raw[i]);

	{ // debugging aid: KEEN456_DEBUG=<file> logs the omnispeak command line there
		wchar_t lf[PL];
		if (GetEnvironmentVariableW(L"KEEN456_DEBUG", lf, PL) > 0) {
			FILE *f = _wfopen(lf, L"ab, ccs=UTF-8");
			if (f) { fwprintf(f, L"keen456: exec %ls\n", cmd); fclose(f); }
		}
	}

	STARTUPINFOW si = {sizeof si};
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, run, &si, &pi))
		die(L"Cannot start omnispeak.exe (error %lu):\n%ls", GetLastError(), cmd);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
