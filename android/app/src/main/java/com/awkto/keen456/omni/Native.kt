// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

/**
 * Bridge to the engine (android/app/jni/keen_android.c, linked into libmain.so).
 * The libraries are loaded by SDLActivity.onCreate; calls before that (or after a
 * failed load) are ignored rather than crashing the UI.
 */
object Native {
    /**
     * The touch POGO button: drives the engine's modern pogo (IN_SetTouchPogo
     * in src/id_in.c): Pogo, then Jump ~30 ms later while held (super-bounce);
     * a hold longer than /POGOHOLD retracts the pogo on release.
     */
    fun setTouchPogo(down: Boolean) {
        try {
            nativeSetTouchPogo(down)
        } catch (e: UnsatisfiedLinkError) {
            // libmain.so not loaded (yet); nothing to drive.
        }
    }

    /**
     * The touch XR button: toggles X-ray (RF_ToggleXray in src/id_rf.c), display
     * only: hidden passages are drawn see-through. Applied at the next frame.
     */
    fun toggleXray() {
        try {
            nativeToggleXray()
        } catch (e: UnsatisfiedLinkError) {
            // libmain.so not loaded (yet); nothing to drive.
        }
    }

    @JvmStatic private external fun nativeSetTouchPogo(down: Boolean)
    @JvmStatic private external fun nativeToggleXray()
}
