// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.content.pm.ActivityInfo
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Toast
import org.libsdl.app.KeenGlue
import org.libsdl.app.SDLActivity
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The game screen: the engine (libmain.so, SDL_main = Omnispeak's main) on
 * SDL's surface, sized to a 4:3 pane at the top of a portrait screen, with the
 * touch controls in the space beneath it.
 *
 * Runs in its own :game process (manifest): SDL_main runs once per process, and
 * the engine's Quit() exits the process; the picker underneath survives that.
 */
class KeenActivity : SDLActivity() {

    companion object {
        const val EXTRA_EPISODE = "episode"
        /** The 320x200 picture is shown at 4:3 (the engine's aspect correction). */
        const val GAME_ASPECT = 4f / 3f
    }

    private lateinit var episode: Episode
    private var overlay: TouchOverlayView? = null

    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    /**
     * The engine's command line: episode, data and save paths, then the
     * settings (smoothing on by default: /PROFILE enhanced /INTERP subpixel).
     */
    override fun getArguments(): Array<String> {
        val p = Settings.prefs(this)
        val args = arrayListOf(
            "/EPISODE", episode.num.toString(),
            "/GAMEPATH", GameSetup.gameDir(this, episode).absolutePath,
            "/USERPATH", GameSetup.saveDir(this, episode).absolutePath,
        )
        args += Settings.engineArgs(p)
        android.util.Log.i("KeenActivity", "engine args: $args")
        return args.toTypedArray()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        episode = Episodes.byId(intent.getStringExtra(EXTRA_EPISODE) ?: "keen4") ?: Episodes.ALL.first()
        val err = GameSetup.prepare(this, episode)
        if (err != null) {
            Toast.makeText(this, err, Toast.LENGTH_LONG).show()
            super.onCreate(savedInstanceState)
            finish()
            return
        }
        Settings.prefs(this).edit().putString("lastEpisode", episode.id).apply()

        super.onCreate(savedInstanceState)
        if (KeenGlue.brokenLibraries()) return

        // The accelerometer would otherwise show up as a joystick.
        KeenGlue.setenv("SDL_ACCELEROMETER_AS_JOYSTICK", "0")

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        requestFastestDisplayMode()

        overlay = TouchOverlayView(this).also { ov ->
            ov.xrayOn = Settings.xrayAtStart(Settings.prefs(this))
            ov.onOpenSettings = { Settings.show(this, inGame = true) { applySystemBars() } }
            ov.onToggleKeyboard = {
                if (KeenGlue.keyboardShown()) KeenGlue.hideKeyboard() else KeenGlue.showKeyboard()
            }
            addContentView(ov, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        }

        // Size SDL's surface to the 4:3 pane at the top; the overlay owns the rest.
        // The engine reads the renderer output size every present, so it simply
        // fills the smaller surface.
        val root = window.decorView
        root.addOnLayoutChangeListener { _, l, t, r, b, _, _, _, _ -> layoutGame(r - l, b - t) }
    }

    private fun layoutGame(w: Int, h: Int) {
        if (w <= 0 || h <= 0) return
        val gameH = min(w / GAME_ASPECT, h * 0.62f).roundToInt()
        val surface = KeenGlue.surface() ?: return
        val lp = surface.layoutParams ?: return
        if (lp.height != gameH || lp.width != ViewGroup.LayoutParams.MATCH_PARENT) {
            lp.width = ViewGroup.LayoutParams.MATCH_PARENT
            lp.height = gameH
            surface.layoutParams = lp
        }
        if (overlay?.panelTop != gameH.toFloat()) overlay?.panelTop = gameH.toFloat()
    }

    /**
     * Portrait by design (game on top, controls below), whatever SDL asks for.
     * USER_PORTRAIT, not PORTRAIT: SDLSurface.surfaceChanged refuses ("Skip ..
     * Surface is not ready") a wider-than-tall surface while PORTRAIT or
     * SENSOR_PORTRAIT is requested, and our 4:3 game pane is exactly that.
     */
    override fun setOrientationBis(w: Int, h: Int, resizable: Boolean, hint: String?) {
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applySystemBars() else overlay?.releaseAll()
    }

    override fun onPause() {
        overlay?.releaseAll()
        super.onPause()
    }

    /**
     * Ask for the panel's fastest refresh rate at the current resolution: the
     * interpolated presentation redraws at the display rate, so 90/120 Hz
     * panels get proportionally smoother motion.
     */
    private fun requestFastestDisplayMode() {
        val d = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) display
                else @Suppress("DEPRECATION") windowManager.defaultDisplay
        val cur = d?.mode ?: return
        val best = d.supportedModes
            .filter { it.physicalWidth == cur.physicalWidth && it.physicalHeight == cur.physicalHeight }
            .maxByOrNull { it.refreshRate } ?: return
        window.attributes = window.attributes.apply { preferredDisplayModeId = best.modeId }
    }

    private fun applySystemBars() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = if (Settings.fullscreen(Settings.prefs(this))) {
            (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        } else {
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        }
    }

    /** Back = the game's Esc menu (save/quit live there), not "kill the run". */
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            overlay?.tapKey(KeyEvent.KEYCODE_ESCAPE)
            return true
        }
        return super.onKeyDown(keyCode, event)
    }
}
