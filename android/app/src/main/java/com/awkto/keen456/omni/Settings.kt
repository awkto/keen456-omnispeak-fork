// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.Switch
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
    // Screen filters and pixel softness: the engine's /FILTER and /PIXELS (src/id_vl_filter.c).
    val FILTER_KEYS = arrayOf("off", "scanlines", "crt", "curved", "rgb", "soft", "amber", "green")
    val FILTER_NAMES = arrayOf(
        "Off (default)", "Scanlines", "CRT (scanlines + mask + vignette)", "Curved CRT",
        "RGB grille", "Soft (blurry pixels)", "Amber monochrome", "Green monochrome")
    val PIXELS_KEYS = arrayOf("crisp", "light", "low", "medium", "high")
    val PIXELS_NAMES = arrayOf("Crisp (sharp pixels, default)", "Smooth: light", "Smooth: low",
        "Smooth: medium", "Smooth: high")
    val POGO_KEYS = arrayOf("off", "120", "150", "180", "240", "300")
    val POGO_NAMES = arrayOf("Off (tap toggles only)", "120 ms", "150 ms", "180 ms (default)", "240 ms", "300 ms")

    @Suppress("DEPRECATION")
    fun prefs(ctx: Context): SharedPreferences =
        ctx.getSharedPreferences("keen456omni", Context.MODE_PRIVATE or Context.MODE_MULTI_PROCESS)

    fun smoothing(p: SharedPreferences) = p.getString("smoothing", "subpixel") ?: "subpixel"
    fun profile(p: SharedPreferences) = p.getString("profile", "enhanced") ?: "enhanced"
    fun cameraSmoothing(p: SharedPreferences) = p.getBoolean("cameraSmoothing", false)
    /** Camera dead zone (#108): 0 vanilla, 1 scaled (= vanilla unzoomed), 2 loose, 3 very loose. */
    fun cameraDeadzone(p: SharedPreferences) = p.getInt("cameraDeadzone", 1).coerceIn(0, 3)
    fun pogoHold(p: SharedPreferences) = p.getString("pogohold", "180") ?: "180"
    fun modernKeyboard(p: SharedPreferences) = p.getBoolean("modernKeyboard", true)
    fun fullscreen(p: SharedPreferences) = p.getBoolean("fullscreen", true)
    fun filter(p: SharedPreferences) = p.getString("filter", "off") ?: "off"
    fun pixels(p: SharedPreferences) = p.getString("pixels", "crisp") ?: "crisp"
    /** Filter brightness: 0 off, 1 auto (default), 2 bright; the engine's /FILTERBRIGHT (#109). */
    fun filterBright(p: SharedPreferences) = p.getInt("filterBright", 1).coerceIn(0, 2)
    /** Scanline style (#111): 0 fine, 1 strong (default here: visible at phone pixel density), 2 thick. */
    fun scanStyle(p: SharedPreferences) = p.getInt("scanStyle", 1).coerceIn(0, 2)
    fun xrayAtStart(p: SharedPreferences) = p.getBoolean("xray", false)
    /** The engine's QuickSave/QuickLoad "Really ...? (Y/N)" prompt; off by default (one-tap 💾). */
    fun quickSaveConfirm(p: SharedPreferences) = p.getBoolean("quickSaveConfirm", false)
    /** The EGA overscan border (cyan in gameplay) round the picture; off by default on a phone. */
    fun egaBorder(p: SharedPreferences) = p.getBoolean("egaBorder", false)

    /** Engine arguments for these settings (episode and paths are added by the caller). */
    fun engineArgs(p: SharedPreferences): List<String> {
        val a = ArrayList<String>()
        a += listOf("/PROFILE", profile(p))
        when (val s = smoothing(p)) {
            "off" -> a += "/NOINTERP"
            else -> a += listOf("/INTERP", s)
        }
        if (cameraSmoothing(p)) a += listOf("/FIX", "camera-smoothing")
        a += listOf("/ZOOM", zoom(p).toString()) // enhanced profile only (#106)
        a += listOf("/CAMERA", cameraDeadzone(p).toString()) // enhanced profile only (#108)
        // Alt on a hardware keyboard; the touch POGO button is always modern.
        a += if (modernKeyboard(p)) "/MODERNCONTROLS" else "/NOMODERNCONTROLS"
        a += listOf("/POGOHOLD", pogoHold(p).uppercase())
        a += listOf("/FILTER", filter(p), "/PIXELS", pixels(p), "/FILTERBRIGHT", filterBright(p).toString(),
            "/SCANSTYLE", scanStyle(p).toString())
        // Portrait shows a 4:3 pane: never the widescreen view.
        a += listOf("/ASPECT", "4:3")
        a += if (xrayAtStart(p)) "/XRAY" else "/NOXRAY"
        // On, the engine's default (or its saved "border" option, see GameSetup.prepare).
        if (!egaBorder(p)) a += "/NOBORDER"
        a += "/VSYNC"
        return a
    }

    /**
     * The values a running game can take without a restart (#110): zoom, camera, filter, pixels,
     * filter brightness. SharedPreferences is not reliable between the picker (main process) and
     * the game (:game process), so every change also lands in this small file, which the game
     * reads when it comes back to the front and after each change made in its own dialog.
     */
    private fun liveFile(ctx: Context) = java.io.File(ctx.filesDir, "live.txt")

    fun writeLive(ctx: Context) {
        val p = prefs(ctx)
        try {
            liveFile(ctx).writeText("${zoom(p)} ${cameraDeadzone(p)} ${idxOf(FILTER_KEYS, filter(p))} " +
                "${idxOf(PIXELS_KEYS, pixels(p))} ${filterBright(p)} ${scanStyle(p)}\n")
        } catch (e: java.io.IOException) {
            android.util.Log.w("Settings", "live settings not saved: $e")
        }
    }

    /** Push the live file (if any) into the running engine. Call from the game process. */
    fun applyLive(ctx: Context) {
        val v = try { liveFile(ctx).readText().trim().split(" ").map { it.toInt() } } catch (e: Exception) { return }
        if (v.size >= 5) Native.applyLive(v[0], v[1], v[2], v[3], v[4], v.getOrElse(5) { scanStyle(prefs(ctx)) })
    }

    private fun idxOf(keys: Array<String>, v: String) = keys.indexOf(v).coerceAtLeast(0)

    private var dlgRef: AlertDialog? = null
    private fun setEnabledDeep(v: View, on: Boolean) {
        v.isEnabled = on
        if (v is ViewGroup) for (i in 0 until v.childCount) setEnabledDeep(v.getChildAt(i), on)
    }

    /** Zoom out (#106): /ZOOM 0|1|2, enhanced profile only (the 'zoom' fix). */
    fun zoom(p: SharedPreferences) = p.getInt("zoom", 0).coerceIn(0, 2)
    val ZOOM_NAMES = arrayOf("Normal", "Zoom out 1 (1.25x)", "Zoom out 2 (1.5x)")
    val CAMERA_NAMES = arrayOf("Vanilla", "Scaled", "Loose", "Very loose")
    private val ALL_KEYS = arrayOf("smoothing", "profile", "cameraSmoothing", "cameraDeadzone", "pogohold",
        "modernKeyboard", "fullscreen", "filter", "pixels", "xray", "quickSaveConfirm", "egaBorder", "zoom", "filterBright", "scanStyle")

    /**
     * The settings dialog, shared by the picker and the game. onChanged runs
     * after any change (the game uses it for live bits like fullscreen).
     */
    fun show(base: Context, inGame: Boolean, onChanged: () -> Unit = {}) {
        // Material widgets (the default dialog theme is the old black Holo one: tiny switches, grey spinners).
        val ctx = android.view.ContextThemeWrapper(base, android.R.style.Theme_DeviceDefault)
        val p = prefs(ctx)
        val dark = Color.parseColor("#0b1526")
        val card = Color.parseColor("#12203a")
        val text = Color.parseColor("#e9eef7")
        val dim = Color.parseColor("#8fa3c4")
        val gold = Color.parseColor("#e8b84b")
        val dens = ctx.resources.displayMetrics.density
        val pad = (dens * 20).toInt()
        val enhancedRows = ArrayList<View>() // dimmed when the vanilla profile is picked

        fun idx(keys: Array<String>, v: String) = keys.indexOf(v).coerceAtLeast(0)
        val col = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(dark)
            setPadding(pad, pad / 2, pad, pad / 2)
        }
        fun section(label: String) = col.addView(TextView(ctx).apply {
            this.text = label
            setTextColor(gold); textSize = 13f; isAllCaps = true; letterSpacing = 0.1f
            setTypeface(typeface, Typeface.BOLD)
            setPadding(pad / 4, pad / 2, 0, pad / 4)
        })
        // One row: label + grey hint on the left, the control on the right.
        fun row(label: String, hint: String, enhancedOnly: Boolean, ctl: View, ctlW: Int): View {
            val r = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setBackgroundColor(card)
                setPadding(pad * 3 / 4, pad / 2, pad * 3 / 4, pad / 2)
                addView(LinearLayout(ctx).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(TextView(ctx).apply { this.text = label; setTextColor(text); textSize = 16f })
                    addView(TextView(ctx).apply { this.text = hint; setTextColor(dim); textSize = 12f })
                }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { marginEnd = pad / 2 })
                addView(ctl, LinearLayout.LayoutParams(ctlW, ViewGroup.LayoutParams.WRAP_CONTENT))
            }
            col.addView(r, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = (dens * 2).toInt() })
            if (enhancedOnly) { enhancedRows += r; r.tag = ctl }
            return r
        }
        val dropW = (dens * 150).toInt()
        fun drop(label: String, hint: String, names: Array<String>, cur: Int, enhancedOnly: Boolean = false,
                 onPick: (Int) -> Unit) {
            val sp = Spinner(ctx, Spinner.MODE_DROPDOWN)
            sp.adapter = object : ArrayAdapter<String>(ctx, android.R.layout.simple_spinner_item, names) {
                private fun style(v: View, bg: Int): View {
                    (v as TextView).apply { setTextColor(text); textSize = 14f; setBackgroundColor(bg)
                        setPadding(pad / 2, pad / 2, pad / 2, pad / 2) }
                    return v
                }
                override fun getView(i: Int, c: View?, g: ViewGroup) = style(super.getView(i, c, g), card)
                override fun getDropDownView(i: Int, c: View?, g: ViewGroup) =
                    style(super.getDropDownView(i, c, g), Color.parseColor("#1b2d4d"))
            }
            sp.setPopupBackgroundDrawable(ColorDrawable(Color.parseColor("#1b2d4d")))
            sp.setSelection(cur)
            var first = true
            sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(a: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                    if (first) { first = false; return }
                    onPick(pos); writeLive(ctx); onChanged()
                }
                override fun onNothingSelected(a: AdapterView<*>?) {}
            }
            row(label, hint, enhancedOnly, sp, dropW)
        }
        fun toggle(label: String, hint: String, cur: Boolean, enhancedOnly: Boolean = false,
                   onSet: (Boolean) -> Unit) {
            val sw = Switch(ctx).apply {
                isChecked = cur
                setOnCheckedChangeListener { _, on -> onSet(on); writeLive(ctx); onChanged() }
            }
            val box = LinearLayout(ctx).apply { gravity = Gravity.END; addView(sw) }
            row(label, hint, enhancedOnly, box, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        fun applyProfile() {
            val on = profile(p) == "enhanced"
            for (r in enhancedRows) { r.alpha = if (on) 1f else 0.4f; setEnabledDeep(r, on) }
        }

        col.addView(TextView(ctx).apply {
            this.text = if (inGame) "Zoom, Camera, Screen filter, Filter brightness, Scanline style and Pixels apply now. " +
                "Other changes apply the next time you start a game."
            else "Zoom, Camera and the screen filter options also reach a game that is already running; " +
                "the rest apply when a game starts."
            setTextColor(dim); textSize = 12f
        })

        section("Picture")
        drop("Zoom", "Show more of the level (Z in game). Enhanced only.", ZOOM_NAMES, zoom(p), true) {
            p.edit().putInt("zoom", it).apply() }
        drop("Screen filter", "CRT-style looks and tints", arrayOf("Off", "Scanlines", "CRT", "Curved CRT",
            "RGB grille", "Soft", "Amber", "Green"), idx(FILTER_KEYS, filter(p))) {
            p.edit().putString("filter", FILTER_KEYS[it]).apply() }
        drop("Filter brightness", "Make up for the darkening of scanlines and masks", arrayOf("Off", "Auto", "Bright"),
            filterBright(p)) { p.edit().putInt("filterBright", it).apply() }
        drop("Scanline style", "Fine, Strong (one dark gap per row) or Thick (per two rows); for every filter with scanlines",
            arrayOf("Fine", "Strong", "Thick"), scanStyle(p)) { p.edit().putInt("scanStyle", it).apply() }
        drop("Pixels", "Sharp or smoothed pixel edges", arrayOf("Crisp", "Smooth: light", "Smooth: low",
            "Smooth: medium", "Smooth: high"), idx(PIXELS_KEYS, pixels(p))) {
            p.edit().putString("pixels", PIXELS_KEYS[it]).apply() }
        toggle("EGA border", "Coloured frame round the picture", egaBorder(p)) {
            p.edit().putBoolean("egaBorder", it).apply() }
        toggle("Fullscreen", "Hide the system bars", fullscreen(p)) {
            p.edit().putBoolean("fullscreen", it).apply() }

        section("Motion")
        drop("Smoothing", "Frame interpolation; Subpixel is smoothest", arrayOf("Subpixel", "Pixel", "Grid", "Off"),
            idx(SMOOTH_KEYS, smoothing(p))) { p.edit().putString("smoothing", SMOOTH_KEYS[it]).apply() }
        toggle("Camera smoothing", "Glide the view instead of jumping. Enhanced only.", cameraSmoothing(p), true) {
            p.edit().putBoolean("cameraSmoothing", it).apply() }
        drop("Camera", "How far Keen moves before the view follows. Enhanced only.", CAMERA_NAMES,
            cameraDeadzone(p), true) { p.edit().putInt("cameraDeadzone", it).apply() }

        section("Controls")
        drop("Pogo retract time", "Hold POGO longer than this to retract on release", arrayOf("Off", "120 ms",
            "150 ms", "180 ms", "240 ms", "300 ms"), idx(POGO_KEYS, pogoHold(p))) {
            p.edit().putString("pogohold", POGO_KEYS[it]).apply() }
        drop("Hardware keyboard Alt", "Modern = super-bounce pogo", arrayOf("Modern pogo", "Plain pogo key"),
            if (modernKeyboard(p)) 0 else 1) { p.edit().putBoolean("modernKeyboard", it == 0).apply() }
        toggle("Save/load confirmation", "Ask Y/N on the QuickSave/QuickLoad buttons", quickSaveConfirm(p)) {
            p.edit().putBoolean("quickSaveConfirm", it).apply() }

        section("Game")
        drop("Profile", "Enhanced adds the fixes; Vanilla is bug-for-bug DOS", arrayOf("Enhanced", "Vanilla"),
            idx(PROFILE_KEYS, profile(p))) {
            p.edit().putString("profile", PROFILE_KEYS[it]).apply(); applyProfile() }
        toggle("X-ray at start", "XR toggles it in game", xrayAtStart(p)) {
            p.edit().putBoolean("xray", it).apply() }
        applyProfile()

        col.addView(Button(ctx).apply {
            this.text = "Reset to defaults"
            setTextColor(text)
            setBackgroundColor(Color.parseColor("#3a1a22"))
            setOnClickListener {
                AlertDialog.Builder(ctx).setTitle("Reset all settings?")
                    .setMessage("Every option goes back to its default.")
                    .setPositiveButton("Reset") { _, _ ->
                        val e = p.edit(); for (k in ALL_KEYS) e.remove(k); e.apply()
                        writeLive(ctx); onChanged(); dlgRef?.dismiss(); show(ctx, inGame, onChanged)
                    }.setNegativeButton("Cancel", null).show()
            }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            .apply { topMargin = pad; bottomMargin = pad })
        // Title bar with Done at the TOP: the old Theme.Black dialog clips its own
        // button bar below a tall custom view, so Done never lived there reliably.
        lateinit var dlg: AlertDialog
        dlgRef = null
        val bar = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Color.parseColor("#081020"))
            setPadding(pad, pad / 2, pad / 2, pad / 2)
            addView(LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                addView(TextView(ctx).apply {
                    this.text = "Settings"
                    setTextColor(text); textSize = 20f
                    setTypeface(typeface, Typeface.BOLD)
                })
                addView(TextView(ctx).apply {
                    this.text = "Build ${BuildConfig.VERSION_NAME} (code ${BuildConfig.VERSION_CODE})"
                    setTextColor(dim); textSize = 11f
                })
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(Button(ctx).apply {
                this.text = "Done"
                setTextColor(Color.parseColor("#14110a"))
                setBackgroundColor(Color.parseColor("#e8b84b"))
                setOnClickListener { dlg.dismiss() }
            })
        }
        val root = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(dark)
            addView(bar)
            addView(ScrollView(ctx).apply { setBackgroundColor(dark); addView(col) },
                LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        }
        dlg = AlertDialog.Builder(ctx).setView(root).create()
        dlgRef = dlg
        dlg.window?.setBackgroundDrawable(ColorDrawable(dark))
        dlg.show()
        // A fixed size inside the screen, so the list scrolls instead of running off the bottom.
        val dm = ctx.resources.displayMetrics
        dlg.window?.setLayout((dm.widthPixels * 0.92f).toInt(), (dm.heightPixels * 0.85f).toInt())
    }
}
