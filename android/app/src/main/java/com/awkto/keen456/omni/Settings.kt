// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView

/**
 * App settings, turned into engine command-line arguments at launch
 * (KeenActivity.getArguments). The in-game Configure menu can still change
 * smoothing live; these are what each session starts with.
 */
object Settings {
    // Smoothing default = what the user loves: the enhanced profile with subpixel interpolation.
    val SMOOTH_KEYS = arrayOf("subpixel", "pixel", "grid", "off")
    val SMOOTH_NAMES = arrayOf(
        "Subpixel (smoothest, default)", "Pixel (whole-pixel motion)",
        "Grid (vanilla 2-px scroll steps)", "Off (vanilla 35 fps)")
    val PROFILE_KEYS = arrayOf("enhanced", "vanilla")
    val PROFILE_NAMES = arrayOf("Enhanced (default)", "Vanilla (bug-for-bug DOS)")
    val POGO_KEYS = arrayOf("off", "120", "150", "180", "240", "300")
    val POGO_NAMES = arrayOf("Off (tap toggles only)", "120 ms", "150 ms", "180 ms (default)", "240 ms", "300 ms")

    @Suppress("DEPRECATION")
    fun prefs(ctx: Context): SharedPreferences =
        ctx.getSharedPreferences("keen456omni", Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS)

    fun smoothing(p: SharedPreferences) = p.getString("smoothing", "subpixel") ?: "subpixel"
    fun profile(p: SharedPreferences) = p.getString("profile", "enhanced") ?: "enhanced"
    fun cameraSmoothing(p: SharedPreferences) = p.getBoolean("cameraSmoothing", false)
    fun pogoHold(p: SharedPreferences) = p.getString("pogohold", "180") ?: "180"
    fun modernKeyboard(p: SharedPreferences) = p.getBoolean("modernKeyboard", true)
    fun fullscreen(p: SharedPreferences) = p.getBoolean("fullscreen", true)

    /** Engine arguments for these settings (episode and paths are added by the caller). */
    fun engineArgs(p: SharedPreferences): List<String> {
        val a = ArrayList<String>()
        a += listOf("/PROFILE", profile(p))
        when (val s = smoothing(p)) {
            "off" -> a += "/NOINTERP"
            else -> a += listOf("/INTERP", s)
        }
        if (cameraSmoothing(p)) a += listOf("/FIX", "camera-smoothing")
        // Alt on a hardware keyboard; the touch POGO button is always modern.
        a += if (modernKeyboard(p)) "/MODERNCONTROLS" else "/NOMODERNCONTROLS"
        a += listOf("/POGOHOLD", pogoHold(p).uppercase())
        a += "/VSYNC"
        return a
    }

    /**
     * The settings dialog, shared by the picker and the game. onChanged runs
     * after any change (the game uses it for live bits like fullscreen).
     */
    fun show(ctx: Context, inGame: Boolean, onChanged: () -> Unit = {}) {
        val p = prefs(ctx)
        val dark = Color.parseColor("#0b1526")
        val text = Color.parseColor("#e9eef7")
        val dim = Color.parseColor("#8fa3c4")
        val pad = (ctx.resources.displayMetrics.density * 20).toInt()

        fun header(label: String) = TextView(ctx).apply {
            this.text = label
            setTextColor(dim)
            textSize = 13f
            isAllCaps = true
            setPadding(0, pad / 2, 0, pad / 4)
        }
        fun radios(names: Array<String>, checked: Int, onPick: (Int) -> Unit) = RadioGroup(ctx).apply {
            for ((i, n) in names.withIndex()) {
                addView(RadioButton(ctx).apply { this.text = n; id = i; setTextColor(text) })
            }
            check(checked)
            setOnCheckedChangeListener { _, id -> onPick(id); onChanged() }
        }
        fun idx(keys: Array<String>, v: String) = keys.indexOf(v).coerceAtLeast(0)

        val col = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(dark)
            setPadding(pad, pad, pad, pad / 2)
            addView(TextView(ctx).apply {
                this.text = if (inGame) "Smoothing, profile and pogo apply the next time an episode starts " +
                    "(the in-game Configure ▸ Options menu changes smoothing live)."
                else "Applied when an episode starts."
                setTextColor(dim); textSize = 12f
            })
            addView(header("Smoothing"))
            addView(radios(SMOOTH_NAMES, idx(SMOOTH_KEYS, smoothing(p))) {
                p.edit().putString("smoothing", SMOOTH_KEYS[it]).apply()
            })
            addView(header("Camera smoothing"))
            addView(radios(arrayOf("Off (default)", "On"), if (cameraSmoothing(p)) 1 else 0) {
                p.edit().putBoolean("cameraSmoothing", it == 1).apply()
            })
            addView(header("Profile"))
            addView(radios(PROFILE_NAMES, idx(PROFILE_KEYS, profile(p))) {
                p.edit().putString("profile", PROFILE_KEYS[it]).apply()
            })
            addView(header("Pogo retract (hold POGO past this = auto-retract on release)"))
            addView(radios(POGO_NAMES, idx(POGO_KEYS, pogoHold(p))) {
                p.edit().putString("pogohold", POGO_KEYS[it]).apply()
            })
            addView(header("Hardware keyboard Alt"))
            addView(radios(arrayOf("Modern pogo (super-bounce, default)", "Plain pogo key"),
                if (modernKeyboard(p)) 0 else 1) {
                p.edit().putBoolean("modernKeyboard", it == 0).apply()
            })
            addView(header("Fullscreen"))
            addView(radios(arrayOf("On (hide system bars)", "Off (show status bar)"),
                if (fullscreen(p)) 0 else 1) {
                p.edit().putBoolean("fullscreen", it == 0).apply()
            })
        }
        AlertDialog.Builder(ctx)
            .setView(ScrollView(ctx).apply { setBackgroundColor(dark); addView(col) })
            .setPositiveButton("Done", null)
            .create()
            .also { dlg ->
                dlg.window?.setBackgroundDrawable(ColorDrawable(dark))
                dlg.show()
                dlg.getButton(AlertDialog.BUTTON_POSITIVE)?.setTextColor(text)
            }
    }
}
