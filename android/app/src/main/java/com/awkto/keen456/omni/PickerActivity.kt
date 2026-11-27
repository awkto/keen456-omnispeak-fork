// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.util.concurrent.Executors
import java.util.zip.ZipInputStream

/**
 * Launcher screen, modelled on the DOSBox-X app's picker: one card per
 * episode. Keen 4 shareware ships in the APK and always plays; Keen 5/6 (and
 * the registered Keen 4) are imported from the player's own files through the
 * Storage Access Framework (a folder or a .zip) into app storage.
 *
 * The game runs in KeenActivity in a separate :game process.
 */
class PickerActivity : Activity() {

    private companion object {
        const val TAG = "KeenPicker"
        const val REQ_IMPORT_TREE = 41
        const val REQ_IMPORT_ZIP = 42
        val ACCENTS = mapOf("keen4" to "#e8b84b", "keen5" to "#54c8e0", "keen6" to "#6ee0a0")
    }

    private val exec = Executors.newSingleThreadExecutor()
    private var importTarget: Episode? = null
    private lateinit var list: LinearLayout

    private val dark = Color.parseColor("#0b1526")
    private val panel = Color.parseColor("#18304f")
    private val textCol = Color.parseColor("#e9eef7")
    private val dim = Color.parseColor("#8fa3c4")

    private fun dp(v: Int) = (resources.displayMetrics.density * v).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        importTarget = savedInstanceState?.getString("importTarget")?.let { Episodes.byId(it) }
        exec.execute { GameSetup.seedShareware(this); runOnUiThread { rebuild() } }

        list = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(24), dp(20), dp(24))
        }
        setContentView(ScrollView(this).apply {
            setBackgroundColor(dark)
            addView(list, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        })
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        importTarget?.let { outState.putString("importTarget", it.id) }
    }

    override fun onResume() {
        super.onResume()
        SystemBars.apply(this)
        rebuild()
        showEngineError()
    }

    /**
     * The engine's Quit() with an error (e.g. a data file it can't read) used to
     * just end the game process: the player saw a black screen or got bounced
     * back. KeenActivity's engine log keeps the message ("<log>.error"); show it.
     */
    private fun showEngineError() {
        for (ep in Episodes.ALL) {
            val err = File(GameSetup.logFile(this, ep).path + ".error")
            if (!err.isFile) continue
            val msg = try { err.readText().trim() } catch (e: Exception) { "" }
            err.delete()
            AlertDialog.Builder(this)
                .setTitle("Keen ${ep.num} stopped")
                .setMessage(msg.ifEmpty { "The engine quit with an error." })
                .setPositiveButton("OK", null)
                .setNeutralButton("Share log") { _, _ -> shareLog() }
                .show()
            return
        }
    }

    /**
     * Share a debug log as text: the engine logs of the last run of each episode
     * plus this app's own logcat (both processes; an app can read its own lines).
     */
    private fun shareLog() {
        exec.execute {
            val sb = StringBuilder()
            sb.append("keen456-omni ${BuildConfig.VERSION_NAME} (code ${BuildConfig.VERSION_CODE})")
                .append(if (BuildConfig.KEEN_DIAG) " DIAG" else "").append('\n')
            sb.append("device ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}, Android ")
                .append(android.os.Build.VERSION.RELEASE).append(" (API ${android.os.Build.VERSION.SDK_INT}), ")
                .append(android.os.Build.SUPPORTED_ABIS.joinToString()).append('\n')
            for (ep in Episodes.ALL) {
                val dir = GameSetup.gameDir(this, ep)
                sb.append("\n== ${ep.id} files: ")
                    .append(dir.listFiles()?.sortedBy { it.name }?.joinToString { "${it.name} ${it.length()}" } ?: "none")
                    .append('\n')
                val log = GameSetup.logFile(this, ep)
                if (log.isFile) {
                    sb.append("== ${ep.id} engine log (${java.util.Date(log.lastModified())}):\n")
                    sb.append(try { log.readLines().takeLast(400).joinToString("\n") } catch (e: Exception) { e.toString() })
                    sb.append('\n')
                }
            }
            sb.append("\n== logcat ==\n")
            try {
                val p = ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "4000")
                    .redirectErrorStream(true).start()
                sb.append(p.inputStream.bufferedReader().readText())
            } catch (e: Exception) {
                sb.append("logcat failed: $e\n")
            }
            val text = sb.toString()
            try {
                File(GameSetup.baseDir(this), "logs").mkdirs()
                File(File(GameSetup.baseDir(this), "logs"), "shared-log.txt").writeText(text)
            } catch (e: Exception) {
                Log.w(TAG, "could not save the log: $e")
            }
            runOnUiThread {
                val send = Intent(Intent.ACTION_SEND).setType("text/plain")
                    .putExtra(Intent.EXTRA_SUBJECT, "keen456-omni log ${BuildConfig.VERSION_NAME}")
                    .putExtra(Intent.EXTRA_TEXT, text.takeLast(150_000))
                startActivity(Intent.createChooser(send, "Share log"))
            }
        }
    }

    private fun rebuild() {
        list.removeAllViews()
        list.addView(TextView(this).apply {
            text = "COMMANDER KEEN 4·5·6"
            setTextColor(textCol)
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
        })
        list.addView(TextView(this).apply {
            text = "Smooth native build (Omnispeak) · pick an episode"
            setTextColor(dim)
            textSize = 13f
            setPadding(0, dp(2), 0, dp(16))
        })

        val last = Settings.prefs(this).getString("lastEpisode", null)
        for (ep in Episodes.ALL) {
            list.addView(card(ep, ep.id == last), LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply { bottomMargin = dp(14) })
        }

        list.addView(Button(this).apply {
            text = "⚙  Settings"
            isAllCaps = false
            setTextColor(textCol)
            setBackgroundColor(panel)
            setOnClickListener { Settings.show(this@PickerActivity, inGame = false) }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))

        list.addView(TextView(this).apply {
            text = "Keen 4 shareware is included. Own the registered Keen 4? Import its files to " +
                "play the full game (your saves are kept). Keen 5 and 6 are commercial: import " +
                "your own copy (a folder or .zip holding EGAGRAPH, GAMEMAPS and AUDIO .CK files, " +
                "v1.4; Keen 6 v1.4 or v1.5)."
            setTextColor(dim)
            textSize = 12f
            setPadding(0, dp(12), 0, 0)
        })
        list.addView(Button(this).apply {
            text = "Share debug log"
            isAllCaps = false
            textSize = 12f
            setTextColor(dim)
            setBackgroundColor(Color.parseColor("#0a1426"))
            setOnClickListener { shareLog() }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36)).apply { topMargin = dp(12) })
        list.addView(TextView(this).apply {
            text = "Build ${BuildConfig.VERSION_NAME} (code ${BuildConfig.VERSION_CODE})" +
                if (BuildConfig.KEEN_DIAG) " · DIAGNOSTIC" else ""
            setTextColor(dim)
            textSize = 11f
            setPadding(0, dp(16), 0, 0)
        })
    }

    private fun card(ep: Episode, lastPlayed: Boolean): LinearLayout {
        val accent = Color.parseColor(ACCENTS[ep.id] ?: "#e8b84b")
        val ready = Episodes.hasGameFiles(GameSetup.gameDir(this, ep), ep) || ep.shareware

        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(panel)
            setPadding(dp(16), dp(12), dp(16), dp(12))

            addView(TextView(this@PickerActivity).apply {
                text = "KEEN ${ep.num}" + if (lastPlayed) "   ·   last played" else ""
                setTextColor(accent)
                textSize = 16f
                typeface = Typeface.DEFAULT_BOLD
            })
            addView(TextView(this@PickerActivity).apply {
                text = ep.title
                setTextColor(textCol)
                textSize = 14f
            })
            addView(TextView(this@PickerActivity).apply {
                text = when {
                    ep.shareware && GameSetup.isFullVersion(this@PickerActivity, ep) ->
                        "Registered version: your game files are installed"
                    ep.shareware -> "Shareware, included · registered version can be imported"
                    ready -> "Your game files are installed"
                    else -> "Needs your game files"
                }
                setTextColor(dim)
                textSize = 12f
                setPadding(0, dp(2), 0, dp(8))
            })

            val row = LinearLayout(this@PickerActivity).apply { orientation = LinearLayout.HORIZONTAL }
            fun action(label: String, filled: Boolean, onTap: () -> Unit) {
                row.addView(Button(this@PickerActivity).apply {
                    text = label
                    isAllCaps = false
                    textSize = 14f
                    setTextColor(if (filled) Color.parseColor("#14110a") else textCol)
                    setBackgroundColor(if (filled) accent else Color.parseColor("#0a1426"))
                    setOnClickListener { onTap() }
                }, LinearLayout.LayoutParams(0, dp(44), 1f).apply { marginEnd = dp(8) })
            }
            if (ready) {
                action("▶  Play", true) { play(ep) }
                action(if (ep.shareware) "Import full version" else "Re-import", false) { importInto(ep) }
            } else {
                action("Import files", true) { importInto(ep) }
            }
            addView(row)
        }
    }

    /**
     * KeenActivity is singleInstance in its own :game process, and SDL_main can
     * run only once per process. Only a LIVE game process of the same episode
     * with the same engine settings is resumed; anything else starts a fresh
     * task (killing a live game of a different episode/settings first).
     *
     * The fresh task matters even when no game process is running: after the
     * engine quits (Quit() = exit()) or the system kills :game, the
     * singleInstance task record survives (alwaysRetainTaskState) and a plain
     * startActivity() brings that task back, recreating KeenActivity from its
     * OLD intent (the previous episode); the new intent only reaches
     * onNewIntent. That made Keen 6 launch Keen 5 (#102).
     */
    private fun play(ep: Episode) {
        val p = Settings.prefs(this)
        val want = ep.id + "|" + Settings.engineArgs(p).joinToString(" ")
        fun start(fresh: Boolean) {
            val i = Intent(this, KeenActivity::class.java).putExtra(KeenActivity.EXTRA_EPISODE, ep.id)
            if (fresh) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            Log.i(TAG, "play ${ep.id} fresh=$fresh")
            startActivity(i)
        }
        val am = getSystemService(ACTIVITY_SERVICE) as android.app.ActivityManager
        val game = am.runningAppProcesses?.firstOrNull { it.processName == "$packageName:game" }
        when {
            game == null -> start(true)
            p.getString("runningGame", null) == want -> start(false)
            else -> {
                android.os.Process.killProcess(game.pid)
                // Give the system a moment to drop the dead process's task.
                android.os.Handler(mainLooper).postDelayed({ start(true) }, 500)
            }
        }
    }

    // --- import (SAF) -------------------------------------------------------

    private fun importInto(ep: Episode) {
        importTarget = ep
        AlertDialog.Builder(this)
            .setTitle("Import Keen ${ep.num} game files")
            .setItems(arrayOf("Pick a folder", "Pick a .zip")) { _, which ->
                if (which == 0) {
                    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_IMPORT_TREE)
                } else {
                    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        addCategory(Intent.CATEGORY_OPENABLE)
                        type = "*/*"
                    }, REQ_IMPORT_ZIP)
                }
            }
            .show()
    }

    @Deprecated("Activity result API kept simple on purpose")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val ep = importTarget ?: return
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            REQ_IMPORT_TREE -> runImport(ep) { importTree(ep, uri) }
            REQ_IMPORT_ZIP -> runImport(ep) { importZip(ep, uri) }
        }
    }

    private fun runImport(ep: Episode, body: () -> Int) {
        val wait = Toast.makeText(this, "Importing…", Toast.LENGTH_SHORT).also { it.show() }
        exec.execute {
            val result = try {
                val n = body()
                val dir = GameSetup.gameDir(this, ep)
                when {
                    n == 0 -> "Nothing to import there (looking for ${ep.dataFiles.joinToString()})."
                    Episodes.hasGameFiles(dir, ep) -> {
                        val k4note = when {
                            !ep.shareware -> ""
                            GameSetup.isFullVersion(this, ep) -> "\nRegistered version detected."
                            else -> "\nThe data still matches the bundled shareware."
                        }
                        val k6note = if (ep.num == 6) "\nDetected Keen 6 " +
                            (if (Episodes.tableSet(dir, ep) == "keen6e15") "v1.5" else "v1.4") + "." else ""
                        "Imported $n file(s): Keen ${ep.num} is ready.$k4note$k6note"
                    }
                    else -> "Imported $n file(s), but Keen ${ep.num} still needs " +
                        ep.dataFiles.filter { Episodes.find(dir, it) == null }.joinToString() + "."
                }
            } catch (e: Exception) {
                Log.w(TAG, "import failed", e)
                "Import failed: ${e.message}"
            }
            runOnUiThread {
                wait.cancel()
                AlertDialog.Builder(this).setMessage(result)
                    .setPositiveButton("OK") { _, _ -> rebuild() }.show()
            }
        }
    }

    /**
     * Only this episode's three data files are imported (no executables, no
     * DOS saves or configs, no other episodes' data). Saves live in a separate dir
     * (GameSetup.saveDir), so an import can never overwrite progress.
     */
    private fun wanted(ep: Episode, name: String): Boolean =
        name.uppercase() in ep.dataFiles

    /** Write to the canonical upper-case name, replacing any differently-cased copy. */
    private fun destFor(dir: File, name: String): File {
        Episodes.find(dir, name)?.takeIf { it.name != name.uppercase() }?.delete()
        return File(dir, name.uppercase())
    }

    private fun importTree(ep: Episode, uri: Uri): Int {
        val tree = DocumentFile.fromTreeUri(this, uri) ?: return 0
        val dir = GameSetup.gameDir(this, ep)
        dir.mkdirs()
        var n = 0
        for (doc in tree.listFiles()) {
            val name = doc.name ?: continue
            if (!doc.isFile || !wanted(ep, name)) continue
            val dest = destFor(dir, name)
            contentResolver.openInputStream(doc.uri)?.use { input ->
                dest.outputStream().use { input.copyTo(it) }
            } ?: continue
            n++
        }
        return n
    }

    private fun importZip(ep: Episode, uri: Uri): Int {
        val dir = GameSetup.gameDir(this, ep)
        dir.mkdirs()
        var n = 0
        contentResolver.openInputStream(uri)?.use { raw ->
            ZipInputStream(raw).use { zin ->
                var e = zin.nextEntry
                while (e != null) {
                    // Flatten nested folders; File().name is the basename, which also defuses zip-slip.
                    val name = File(e.name).name
                    if (!e.isDirectory && wanted(ep, name)) {
                        destFor(dir, name).outputStream().use { zin.copyTo(it) }
                        n++
                    }
                    e = zin.nextEntry
                }
            }
        }
        return n
    }
}
