// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.app.Activity
import android.graphics.Color
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * Status bar + navigation bar policy for both activities, driven by the
 * "fullscreen" pref. On: both bars hidden (swipe shows them briefly) and the
 * content draws edge to edge. Off: both bars shown and the content sits
 * between them (decor fits system windows), so nothing hides behind them.
 * Uses the window insets controller (API 30+ platform, flag fallback below).
 */
object SystemBars {
    fun apply(a: Activity) {
        val on = Settings.fullscreen(Settings.prefs(a))
        val w = a.window
        // The theme no longer sets FLAG_FULLSCREEN; SDL also clears it, so be explicit.
        w.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        WindowCompat.setDecorFitsSystemWindows(w, !on)
        val c = WindowCompat.getInsetsController(w, w.decorView)
        if (on) {
            c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            c.hide(WindowInsetsCompat.Type.systemBars())
        } else {
            w.statusBarColor = Color.BLACK
            w.navigationBarColor = Color.BLACK
            c.isAppearanceLightStatusBars = false
            c.isAppearanceLightNavigationBars = false
            c.show(WindowInsetsCompat.Type.systemBars())
        }
    }

    fun toggle(a: Activity) {
        val p = Settings.prefs(a)
        p.edit().putBoolean("fullscreen", !Settings.fullscreen(p)).apply()
        apply(a)
    }
}
