/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * keen456-omnispeak-fork Android bridge (#102): the few JNI entry points the
 * Kotlin shell calls into the engine. Linked into libmain.so next to the engine.
 */
#include <jni.h>
#include <stdbool.h>

#include "id_in.h"
#include "id_rf.h"

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
