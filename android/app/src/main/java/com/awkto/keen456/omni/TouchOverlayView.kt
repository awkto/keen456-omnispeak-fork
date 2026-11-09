// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import org.libsdl.app.SDLActivity
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * On-screen controls below the game, ported from the DOSBox-X app's overlay
 * (same layout and palette as the web app):
 *
 *     ┌───────────────────────┐
 *     │        game pane      │   SDL surface, sized by KeenActivity
 *     ├───────────────────────┤
 *     │ XR ⛶             Y  N │
 *     │            SHOOT POGO │
 *     │  (stick)              │
 *     │              JUMP     │
 *     │  ⚙  ESC  💾  ⌨  ⏎     │
 *     └───────────────────────┘
 *
 * Portrait only (#102): the activity is locked to portrait, so there is no
 * landscape layout. The 💾 pill opens a SAVE / LOAD popup (as in the DOSBox-X
 * and web apps); here they tap the engine's QuickSave (F5) / QuickLoad (F9).
 * The DOSBox-X app's FF (turbo) pill has no counterpart in this engine.
 *
 * One View owns every control and every pointer (two thumbs at once need a
 * single hit-tester). Buttons press plain SDL keys:
 *
 *   stick -> arrow keys (8-way)   JUMP -> Ctrl   SHOOT -> Space
 *   POGO  -> Native.nativeSetTouchPogo: the engine's modern pogo (super-bounce
 *            + auto-retract), the same logic /MODERNCONTROLS gives Alt
 *   ESC / ⏎ / Y / N -> the keys, for menus and prompts
 *   XR    -> Native.toggleXray: X-ray on/off (display only, hidden passages see-through);
 *            the pill is lit (teal) while X-ray is on
 *   💾    -> popup: SAVE = F5 (QuickSave), LOAD = F9 (QuickLoad)
 */
class TouchOverlayView(context: Context) : View(context) {

    private class Control(
        val id: String,
        val label: String,
        val keys: IntArray,
        val fill: Int,
        val labelColor: Int,
        val circle: Boolean,
        val textScale: Float = 1f,
    ) {
        val rect = RectF()
        var pressed = false
    }

    private val gold = Color.parseColor("#e8b84b")
    private val teal = Color.parseColor("#54c8e0")
    private val shootRed = Color.parseColor("#ff6b6b")
    private val jumpGreen = Color.parseColor("#6ee06e")
    private val border = Color.parseColor("#2a3f63")
    private val textCol = Color.parseColor("#e9eef7")
    private val panelBg = Color.parseColor("#081020")
    private val btnBg = Color.parseColor("#18304f")
    private val stickBg = Color.parseColor("#0a1426")
    private val ink = Color.parseColor("#14110a")
    private val green = Color.parseColor("#6ee0a0")
    private val greenBg = Color.parseColor("#143a2f")

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; color = border }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }

    private val controls = ArrayList<Control>()

    /** Top of the controls panel: the bottom of the game surface (set by KeenActivity). */
    var panelTop = 0f
        set(v) { field = v; if (width > 0) layoutControls(width.toFloat(), height.toFloat()); invalidate() }

    var onToggleFullscreen: (() -> Unit)? = null
    var onOpenSettings: (() -> Unit)? = null
    var onToggleKeyboard: (() -> Unit)? = null

    /**
     * Whether X-ray is on, as far as this view knows (seeded from the start
     * setting; flipped by XR). Only drives the XR pill's look.
     */
    var xrayOn = false
        set(v) { field = v; invalidate() }

    // Save/Load popup (the 💾 pill), drawn above the controls while open.
    private var saveLoadOpen = false
    private val popupSave = RectF()
    private val popupLoad = RectF()

    private var stickCx = 0f
    private var stickCy = 0f
    private var stickR = 0f
    private var knobX = 0f
    private var knobY = 0f
    private var stickPointer = -1
    private val stickKeys = HashSet<Int>()
    private val pointerControl = HashMap<Int, Control>()

    /** Ref-counted key holds: a key is released only when its last holder lets go. */
    private val keyHolds = HashMap<Int, Int>()

    private fun dp(v: Float) = v * resources.displayMetrics.density

    init {
        isFocusable = false
        setWillNotDraw(false)
        controls += Control("shoot", "SHOOT", intArrayOf(KeyEvent.KEYCODE_SPACE), shootRed, ink, true, 0.7f)
        controls += Control("pogo", "POGO", intArrayOf(), teal, ink, true, 0.7f)
        controls += Control("jump", "JUMP", intArrayOf(KeyEvent.KEYCODE_CTRL_LEFT), jumpGreen, ink, true, 0.7f)
        controls += Control("ny", "Y", intArrayOf(KeyEvent.KEYCODE_Y), btnBg, gold, false)
        controls += Control("nn", "N", intArrayOf(KeyEvent.KEYCODE_N), btnBg, gold, false)
        controls += Control("xray", "XR", intArrayOf(), btnBg, teal, false, 0.8f)
        controls += Control("fs", "⛶", intArrayOf(), btnBg, textCol, false, 0.8f)
        controls += Control("gear", "⚙", intArrayOf(), btnBg, textCol, false)
        controls += Control("esc", "ESC", intArrayOf(KeyEvent.KEYCODE_ESCAPE), btnBg, textCol, false, 0.8f)
        controls += Control("saveload", "💾", intArrayOf(), greenBg, green, false)
        controls += Control("kbd", "⌨", intArrayOf(), btnBg, textCol, false)
        controls += Control("enter", "⏎", intArrayOf(KeyEvent.KEYCODE_ENTER), btnBg, textCol, false)
    }

    private fun byId(id: String) = controls.first { it.id == id }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        layoutControls(w.toFloat(), h.toFloat())
    }

    private fun layoutControls(w: Float, h: Float) {
        val pad = w * 0.025f
        val gap = w * 0.012f
        val top = if (panelTop > 0f) panelTop else min(w * 0.75f, h * 0.6f)

        // Y/N at the top-right of the panel.
        val nW = w * 0.11f
        val nH = min(nW * 0.8f, dp(40f))
        val nY = top + pad
        byId("nn").rect.set(w - pad - nW, nY, w - pad, nY + nH)
        byId("ny").rect.set(w - pad - nW * 2f - gap, nY, w - pad - nW - gap, nY + nH)
        // XR (X-ray toggle) at the top-left, same size as Y/N.
        byId("xray").rect.set(pad, nY, pad + nW, nY + nH)
        // Fullscreen (system bars) toggle right next to it.
        byId("fs").rect.set(pad + nW + gap, nY, pad + nW * 2f + gap, nY + nH)

        // Bottom row, well clear of the gesture bar / rounded screen corners.
        val menu = listOf("gear", "esc", "saveload", "kbd", "enter")
        val mW = min((w - 2 * pad - gap * (menu.size - 1)) / menu.size, w * 0.15f)
        val mH = min(mW * 0.5f, dp(40f))
        val mY = h - pad - dp(28f) - mH
        val mStep = (w - 2 * pad - mW) / (menu.size - 1)
        for ((i, id) in menu.withIndex()) {
            val mx = pad + i * mStep
            byId(id).rect.set(mx, mY, mx + mW, mY + mH)
        }

        // Save/Load popup, stacked just above the 💾 pill.
        val sl = byId("saveload").rect
        val pw = w * 0.30f
        val ph = dp(46f)
        val px0 = (sl.centerX() - pw / 2f).coerceIn(pad, w - pad - pw)
        popupLoad.set(px0, sl.top - gap * 2f - ph, px0 + pw, sl.top - gap * 2f)
        popupSave.set(px0, popupLoad.top - gap - ph, px0 + pw, popupLoad.top - gap)

        // Main row: stick | action cluster (SHOOT, POGO on top, JUMP beneath).
        val rowTop = nY + nH + gap * 2f
        val rowBottom = mY - gap * 2f
        val region = rowBottom - rowTop
        stickR = min(w * 0.17f, region * 0.30f)
        val aR = min(w * 0.09f, region * 0.20f)
        val blockH = maxOf(stickR * 2f, aR * 4.2f)
        val nudge = dp(12f)
        val rowMid = maxOf((rowTop + rowBottom) / 2f, rowTop + blockH / 2f) - nudge

        stickCx = pad + stickR + nudge
        stickCy = rowMid
        knobX = stickCx
        knobY = stickCy
        fun place(c: Control, cx: Float, cy: Float, r: Float) = c.rect.set(cx - r, cy - r, cx + r, cy + r)
        val pogoCx = w - pad - aR - nudge
        val shootCx = pogoCx - aR * 2.2f
        val upperCy = rowMid - blockH / 2f + aR
        place(byId("shoot"), shootCx, upperCy, aR)
        place(byId("pogo"), pogoCx, upperCy, aR)
        place(byId("jump"), (shootCx + pogoCx) / 2f, upperCy + aR * 2.2f, aR)

        textPaint.textSize = nH * 0.5f
        strokePaint.strokeWidth = dp(1.5f)
    }

    override fun onDraw(canvas: Canvas) {
        val top = if (panelTop > 0f) panelTop else 0f
        fillPaint.color = panelBg
        canvas.drawRect(0f, top, width.toFloat(), height.toFloat(), fillPaint)

        fillPaint.color = stickBg
        canvas.drawCircle(stickCx, stickCy, stickR, fillPaint)
        strokePaint.color = border
        canvas.drawCircle(stickCx, stickCy, stickR, strokePaint)
        fillPaint.color = teal
        fillPaint.alpha = if (stickPointer >= 0) 255 else 210
        canvas.drawCircle(knobX, knobY, stickR * 0.42f, fillPaint)

        for (c in controls) {
            val lit = c.id == "xray" && xrayOn
            fillPaint.color = if (lit) teal else c.fill
            if (c.pressed) fillPaint.alpha = (fillPaint.alpha * 0.6f).roundToInt().coerceIn(0, 255)
            if (c.circle) {
                val r = c.rect.width() / 2f
                canvas.drawCircle(c.rect.centerX(), c.rect.centerY(), r, fillPaint)
                canvas.drawCircle(c.rect.centerX(), c.rect.centerY(), r, strokePaint)
            } else {
                val rr = dp(9f)
                canvas.drawRoundRect(c.rect, rr, rr, fillPaint)
                canvas.drawRoundRect(c.rect, rr, rr, strokePaint)
            }
            textPaint.color = if (lit) ink else c.labelColor
            val base = textPaint.textSize
            textPaint.textSize = base * c.textScale * (if (c.circle) 1.15f else 1f)
            val fm = textPaint.fontMetrics
            canvas.drawText(c.label, c.rect.centerX(), c.rect.centerY() - (fm.ascent + fm.descent) / 2f, textPaint)
            textPaint.textSize = base
        }

        if (saveLoadOpen) {
            val rr = dp(9f)
            for ((rect, label, color) in listOf(
                Triple(popupSave, "SAVE  F5", gold),
                Triple(popupLoad, "LOAD  F9", teal),
            )) {
                fillPaint.color = btnBg
                canvas.drawRoundRect(rect, rr, rr, fillPaint)
                strokePaint.color = color
                canvas.drawRoundRect(rect, rr, rr, strokePaint)
                strokePaint.color = border
                textPaint.color = color
                val fm = textPaint.fontMetrics
                canvas.drawText(label, rect.centerX(), rect.centerY() - (fm.ascent + fm.descent) / 2f, textPaint)
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                return handleDown(event.getPointerId(i), event.getX(i), event.getY(i))
            }
            MotionEvent.ACTION_MOVE -> {
                if (stickPointer >= 0) {
                    val i = event.findPointerIndex(stickPointer)
                    if (i >= 0) updateStick(event.getX(i), event.getY(i))
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                handleUp(event.getPointerId(event.actionIndex))
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                releaseAll()
                return true
            }
        }
        return false
    }

    private fun handleDown(pointerId: Int, x: Float, y: Float): Boolean {
        // Touches on the game picture fall through to SDL.
        if (y < panelTop) return false

        // An open Save/Load popup takes the whole tap: run the choice, or just
        // close on a tap anywhere else.
        if (saveLoadOpen) {
            saveLoadOpen = false
            invalidate()
            when {
                popupSave.contains(x, y) -> tapKey(KeyEvent.KEYCODE_F5)
                popupLoad.contains(x, y) -> tapKey(KeyEvent.KEYCODE_F9)
            }
            return true
        }

        if (hypot(x - stickCx, y - stickCy) <= stickR * 1.15f && stickPointer < 0) {
            stickPointer = pointerId
            updateStick(x, y)
            invalidate()
            return true
        }
        for (c in controls) {
            if (!hit(c, x, y)) continue
            when (c.id) {
                "gear" -> { onOpenSettings?.invoke(); return true }
                "fs" -> { onToggleFullscreen?.invoke(); return true }
                "kbd" -> { onToggleKeyboard?.invoke(); return true }
                "saveload" -> { saveLoadOpen = true; invalidate(); return true }
                "pogo" -> Native.setTouchPogo(true)
                "xray" -> { Native.toggleXray(); xrayOn = !xrayOn }
            }
            pointerControl[pointerId] = c
            c.pressed = true
            c.keys.forEach { holdKey(it, true) }
            invalidate()
            return true
        }
        // Consume panel touches that miss every control, so a second finger
        // landing on a button still reaches this view.
        return true
    }

    private fun hit(c: Control, x: Float, y: Float): Boolean {
        if (!c.circle) return c.rect.contains(x, y)
        return hypot(x - c.rect.centerX(), y - c.rect.centerY()) <= c.rect.width() / 2f
    }

    private fun handleUp(pointerId: Int) {
        if (pointerId == stickPointer) {
            stickPointer = -1
            knobX = stickCx
            knobY = stickCy
            stickKeys.forEach { holdKey(it, false) }
            stickKeys.clear()
            invalidate()
            return
        }
        pointerControl.remove(pointerId)?.let { c ->
            c.pressed = false
            if (c.id == "pogo" && pointerControl.values.none { it.id == "pogo" }) Native.setTouchPogo(false)
            c.keys.forEach { holdKey(it, false) }
            invalidate()
        }
    }

    /** Eight-way from the stick, with a dead zone; diagonals hold both keys. */
    private fun updateStick(x: Float, y: Float) {
        var dx = x - stickCx
        var dy = y - stickCy
        val dist = hypot(dx, dy)
        val max = stickR * 0.72f
        if (dist > max) {
            dx = dx / dist * max
            dy = dy / dist * max
        }
        knobX = stickCx + dx
        knobY = stickCy + dy

        val want = HashSet<Int>()
        val dead = stickR * 0.28f
        if (dist > dead) {
            val ax = abs(dx)
            val ay = abs(dy)
            if (ax > dead * 0.5f && ax >= ay * 0.41f)
                want += if (dx < 0) KeyEvent.KEYCODE_DPAD_LEFT else KeyEvent.KEYCODE_DPAD_RIGHT
            if (ay > dead * 0.5f && ay >= ax * 0.41f)
                want += if (dy < 0) KeyEvent.KEYCODE_DPAD_UP else KeyEvent.KEYCODE_DPAD_DOWN
        }
        if (want != stickKeys) {
            (stickKeys - want).forEach { holdKey(it, false) }
            (want - stickKeys).forEach { holdKey(it, true) }
            stickKeys.clear()
            stickKeys.addAll(want)
        }
        invalidate()
    }

    private fun holdKey(code: Int, down: Boolean) {
        val n = keyHolds[code] ?: 0
        if (down) {
            keyHolds[code] = n + 1
            if (n == 0) SDLActivity.onNativeKeyDown(code)
        } else if (n > 0) {
            keyHolds[code] = n - 1
            if (n == 1) SDLActivity.onNativeKeyUp(code)
        }
    }

    /** Force every held key up (focus loss, pause). */
    fun releaseAll() {
        saveLoadOpen = false
        keyHolds.forEach { (code, n) -> if (n > 0) SDLActivity.onNativeKeyUp(code) }
        keyHolds.clear()
        stickKeys.clear()
        pointerControl.clear()
        Native.setTouchPogo(false)
        controls.forEach { it.pressed = false }
        stickPointer = -1
        knobX = stickCx
        knobY = stickCy
        invalidate()
    }

    /** Tap a key that has no button of its own (hardware Back -> Esc). */
    fun tapKey(code: Int) {
        SDLActivity.onNativeKeyDown(code)
        postDelayed({ SDLActivity.onNativeKeyUp(code) }, 60)
    }
}
