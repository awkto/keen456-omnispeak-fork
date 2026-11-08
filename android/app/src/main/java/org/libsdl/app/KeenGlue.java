// SPDX-License-Identifier: GPL-2.0-or-later
package org.libsdl.app;

import android.view.View;

/**
 * keen456 addition, NOT part of SDL's Java glue (that comes unmodified from the
 * SDL2 source tree, see app/build.gradle). Lives in SDL's package to reach the
 * few package-private/protected pieces the Kotlin shell needs: the game surface
 * (to size it to the top of the screen) and the soft-keyboard toggle.
 */
public final class KeenGlue {
    private KeenGlue() {}

    /** SDL's game surface, or null before SDLActivity.onCreate has run. */
    public static View surface() {
        return SDLActivity.mSurface;
    }

    /** True when SDLActivity could not load libSDL2/libmain (it shows its own error). */
    public static boolean brokenLibraries() {
        return SDLActivity.mBrokenLibraries;
    }

    public static boolean keyboardShown() {
        return SDLActivity.mScreenKeyboardShown;
    }

    /** Raise the IME; the 1x1 DummyEdit at the top-left keeps it from panning the game. */
    public static void showKeyboard() {
        SDLActivity.showTextInput(0, 0, 1, 1);
    }

    public static void hideKeyboard() {
        if (SDLActivity.mSingleton != null) {
            SDLActivity.mSingleton.sendCommand(SDLActivity.COMMAND_TEXTEDIT_HIDE, null);
        }
    }

    /** Set an environment variable (SDL hints read them) before SDL_main starts. */
    public static void setenv(String name, String value) {
        SDLActivity.nativeSetenv(name, value);
    }
}
