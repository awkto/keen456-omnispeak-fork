/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * keen456-omnispeak-fork Android bridge (#102): the few JNI entry points the
 * Kotlin shell calls into the engine. Linked into libmain.so next to the engine.
 */
#define _GNU_SOURCE
#include <jni.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unwind.h>
#include <sys/syscall.h>
#include <android/log.h>

#include <SDL.h>
#include <SDL_system.h>

#include "id_heads.h"
#include "id_in.h"
#include "id_rf.h"
#include "id_sd.h"
#include "ck_def.h"
#include "ck_play.h"

#define LOG_TAG "omnispeak"

/* com.awkto.keen456.omni.Native.nativeSetTouchPogo: the touch POGO button.
 * Drives the engine's modern pogo (super-bounce + auto-retract) directly, so it
 * works whatever the keyboard pogo setting is. Called from the UI thread. */
JNIEXPORT void JNICALL Java_com_awkto_keen456_omni_Native_nativeSetTouchPogo(JNIEnv *env, jclass cls, jboolean down)
{
	(void)env;
	(void)cls;
	IN_SetTouchPogo(down ? true : false);
}

/* com.awkto.keen456.omni.Native.nativeToggleXray: the touch XR button (#104).
 * Flips X-ray (display only: hidden passages drawn see-through). Only sets a
 * request; the engine applies it at its next refresh, so any thread may call it. */
JNIEXPORT void JNICALL Java_com_awkto_keen456_omni_Native_nativeToggleXray(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	RF_ToggleXray();
}

/* com.awkto.keen456.omni.Native.nativeApplyLive (#110): new zoom / camera / filter / pixels /
 * filter brightness / scanline style values for the RUNNING game. Only queued here (one atomic word); the engine's
 * play loop applies them on its own thread, so any thread may call it. */
JNIEXPORT void JNICALL Java_com_awkto_keen456_omni_Native_nativeApplyLive(JNIEnv *env, jclass cls, jint zoom, jint camera,
	jint filter, jint pixels, jint bright, jint scanstyle)
{
	(void)env;
	(void)cls;
	CK_QueueLiveSettings(zoom, camera, filter, pixels, bright, scanstyle);
}

/* --- engine log (#102) -------------------------------------------------------
 * On Android the engine's stdout/stderr (its Quit() messages, warnings) go to
 * /dev/null. nativeStartLog points them at a pipe; a reader thread copies every
 * line to logcat (tag "omnispeak") and to a log file the picker can show/share.
 * With diag set, a watchdog also logs a heartbeat every 3 s: the game clock, the
 * level state and a native backtrace of the engine (SDL) thread, so a hang or a
 * black screen on a device we can't attach to still says where the engine is. */

static FILE *ka_logFile;
static char ka_errorPath[1024];
static int ka_pipeRead = -1;
static pthread_t ka_reader;
static bool ka_readerUp;
static volatile bool ka_readerDone;
static pthread_mutex_t ka_logLock = PTHREAD_MUTEX_INITIALIZER;

static void ka_logLine(const char *line)
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, line);
	pthread_mutex_lock(&ka_logLock);
	if (ka_logFile)
	{
		fputs(line, ka_logFile);
		fputc('\n', ka_logFile);
		fflush(ka_logFile);
	}
	pthread_mutex_unlock(&ka_logLock);
}

static void *ka_readerMain(void *arg)
{
	(void)arg;
	char buf[1024];
	size_t len = 0;
	for (;;)
	{
		ssize_t n = read(ka_pipeRead, buf + len, sizeof(buf) - 1 - len);
		if (n <= 0)
			break;
		len += (size_t)n;
		size_t start = 0;
		for (size_t i = 0; i < len; ++i)
		{
			if (buf[i] == '\n')
			{
				buf[i] = 0;
				ka_logLine(buf + start);
				start = i + 1;
			}
		}
		memmove(buf, buf + start, len - start);
		len -= start;
		if (len == sizeof(buf) - 1) /* a very long line: flush what we have */
		{
			buf[len] = 0;
			ka_logLine(buf);
			len = 0;
		}
	}
	if (len)
	{
		buf[len] = 0;
		ka_logLine(buf);
	}
	ka_readerDone = true;
	return NULL;
}

/* At exit (the engine's Quit() calls exit()): push the last lines, including the
 * quit message, through the pipe before the process goes. */
static void ka_atExit(void)
{
	if (!ka_readerUp)
		return;
	fflush(stdout);
	fflush(stderr);
	int devnull = open("/dev/null", O_WRONLY);
	if (devnull >= 0)
	{
		dup2(devnull, 1); /* closes the pipe's write ends: the reader sees EOF */
		dup2(devnull, 2);
		close(devnull);
	}
	for (int i = 0; i < 1000 && !ka_readerDone; ++i) /* up to 1 s */
		usleep(1000);
	ka_readerUp = false;
}

/* --- diag watchdog ---------------------------------------------------------- */

#define KA_MAX_FRAMES 24
static volatile int ka_btCount;
static uintptr_t ka_btFrames[KA_MAX_FRAMES];
static volatile int ka_btDone;

static _Unwind_Reason_Code ka_unwindOne(struct _Unwind_Context *ctx, void *arg)
{
	(void)arg;
	uintptr_t pc = _Unwind_GetIP(ctx);
	if (pc && ka_btCount < KA_MAX_FRAMES)
		ka_btFrames[ka_btCount++] = pc;
	return ka_btCount < KA_MAX_FRAMES ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void ka_btSignal(int sig)
{
	(void)sig;
	ka_btCount = 0;
	_Unwind_Backtrace(ka_unwindOne, NULL);
	ka_btDone = 1;
}

static pid_t ka_findThread(const char *name)
{
	DIR *d = opendir("/proc/self/task");
	if (!d)
		return 0;
	struct dirent *e;
	pid_t found = 0;
	while (!found && (e = readdir(d)))
	{
		char path[64], comm[32] = {0};
		snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(comm, sizeof(comm), f) && !strncmp(comm, name, strlen(name)))
			found = (pid_t)atoi(e->d_name);
		fclose(f);
	}
	closedir(d);
	return found;
}

static void ka_logBacktrace(pid_t tid)
{
	ka_btDone = 0;
	if (syscall(SYS_tgkill, getpid(), tid, SIGUSR2) != 0)
		return;
	for (int i = 0; i < 200 && !ka_btDone; ++i)
		usleep(1000);
	if (!ka_btDone)
	{
		ka_logLine("[diag] engine thread did not answer the backtrace signal");
		return;
	}
	char line[256];
	for (int i = 0; i < ka_btCount; ++i)
	{
		Dl_info info;
		const char *sym = "?", *lib = "?";
		uintptr_t off = 0;
		if (dladdr((void *)ka_btFrames[i], &info))
		{
			if (info.dli_sname)
			{
				sym = info.dli_sname;
				off = ka_btFrames[i] - (uintptr_t)info.dli_saddr;
			}
			if (info.dli_fname)
			{
				const char *slash = strrchr(info.dli_fname, '/');
				lib = slash ? slash + 1 : info.dli_fname;
			}
		}
		snprintf(line, sizeof(line), "[diag]   #%02d %s+0x%zx (%s)", i, sym, (size_t)off, lib);
		ka_logLine(line);
	}
}

static void *ka_watchdogMain(void *arg)
{
	(void)arg;
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = ka_btSignal;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR2, &sa, NULL);
	for (int beat = 0;; ++beat)
	{
		sleep(3);
		char line[160];
		snprintf(line, sizeof(line), "[diag] beat %d: timeCount=%u levelState=%d mapon=%d",
			beat, (unsigned)SD_GetTimeCount(), ck_gameState.levelState, ck_gameState.currentLevel);
		ka_logLine(line);
		pid_t tid = ka_findThread("SDLThread");
		if (tid)
			ka_logBacktrace(tid);
		else
			ka_logLine("[diag] no SDLThread");
	}
	return NULL;
}

/* Called by Quit() (src/ck_quit.c) with an error message, before the engine
 * exits: saved next to the log so the picker can show it instead of the player
 * just bouncing back (or staring at a black screen). */
void KA_QuitError(const char *msg)
{
	if (!ka_errorPath[0] || !msg)
		return;
	FILE *f = fopen(ka_errorPath, "w");
	if (f)
	{
		fputs(msg, f);
		fclose(f);
	}
}

/* Called by Quit() (src/ck_quit.c) instead of exit() once the engine has shut
 * down (#102). exit() runs atexit handlers and static destructors while Android's
 * own threads (HWUI's RenderThread, SDL's audio/Java glue) are still running,
 * which crashed on quit (SIGSEGV in RenderThread after an error). Instead: push
 * the last log lines out, ask the activity to finish (its task is removed, so the
 * picker comes back), then end the :game process with _exit(). */
void KA_Exit(int code)
{
	ka_atExit();
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = env ? (jobject)SDL_AndroidGetActivity() : NULL;
	if (activity)
	{
		jclass cls = (*env)->GetObjectClass(env, activity);
		jmethodID finish = cls ? (*env)->GetMethodID(env, cls, "finishAndRemoveTask", "()V") : NULL;
		if (!finish)
		{
			(*env)->ExceptionClear(env);
			finish = cls ? (*env)->GetMethodID(env, cls, "finish", "()V") : NULL;
		}
		if (finish)
			(*env)->CallVoidMethod(env, activity, finish);
		if ((*env)->ExceptionCheck(env))
			(*env)->ExceptionClear(env);
		(*env)->DeleteLocalRef(env, activity);
		if (cls)
			(*env)->DeleteLocalRef(env, cls);
		usleep(150 * 1000); /* let the finish reach the system before the process goes */
	}
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "engine quit (code %d): ending the game process", code);
	_exit(code);
}

/* com.awkto.keen456.omni.Native.nativeStartLog: call once per process, before SDL_main. */
JNIEXPORT void JNICALL Java_com_awkto_keen456_omni_Native_nativeStartLog(JNIEnv *env, jclass cls, jstring jpath, jboolean diag)
{
	(void)cls;
	static bool started;
	if (started)
		return;
	started = true;
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	ka_logFile = path ? fopen(path, "w") : NULL;
	if (path)
	{
		snprintf(ka_errorPath, sizeof(ka_errorPath), "%s.error", path);
		unlink(ka_errorPath);
		(*env)->ReleaseStringUTFChars(env, jpath, path);
	}

	int fds[2];
	if (pipe(fds) == 0)
	{
		ka_pipeRead = fds[0];
		setvbuf(stdout, NULL, _IOLBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		close(fds[1]);
		if (pthread_create(&ka_reader, NULL, ka_readerMain, NULL) == 0)
		{
			ka_readerUp = true;
			atexit(ka_atExit);
		}
	}
	if (diag)
	{
		pthread_t wd;
		if (pthread_create(&wd, NULL, ka_watchdogMain, NULL) == 0)
			pthread_detach(wd);
	}
}
