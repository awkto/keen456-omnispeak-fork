// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import android.content.Context
import android.util.Log
import java.io.File

/**
 * Install plumbing: unpack the bundled Keen 4 shareware into app storage and
 * put Omnispeak's tables next to whichever episode is about to run.
 *
 * Layout under baseDir (external files dir, reachable over USB/MTP):
 *   game/keen<n>/   the episode's data files (+ Omnispeak's tables, refreshed every launch)
 *   save/keen<n>/   /USERPATH: savegames and omnispeak.cfg, never touched by installs/imports
 */
object GameSetup {
    private const val TAG = "KeenSetup"

    fun baseDir(ctx: Context): File = ctx.getExternalFilesDir(null) ?: ctx.filesDir

    fun gameDir(ctx: Context, ep: Episode): File = File(File(baseDir(ctx), "game"), ep.id)

    fun saveDir(ctx: Context, ep: Episode): File = File(File(baseDir(ctx), "save"), ep.id)

    /** The engine's log of the last run of this episode (Native.startLog); "<log>.error" holds an error Quit(). */
    fun logFile(ctx: Context, ep: Episode): File = File(File(baseDir(ctx), "logs"), "${ep.id}.log")

    /**
     * Seed the episodes bundled in the APK (assets/game/<id>): the Keen 4
     * shareware, plus Keen 6 in the private diagnostic build only. Re-extracts
     * only when this build's stamp is missing, and never overwrites a file that
     * exists (an imported registered Keen 4 must survive app updates).
     */
    fun seedShareware(ctx: Context) {
        val base = baseDir(ctx)
        base.mkdirs()
        val stamp = File(base, ".game-installed")
        val want = BuildConfig.VERSION_CODE.toString()
        val bundled = Episodes.ALL.filter { ctx.assets.list("game/${it.id}").orEmpty().isNotEmpty() }
        if (stamp.isFile && stamp.readText().trim() == want &&
            bundled.all { Episodes.hasGameFiles(gameDir(ctx, it), it) }) return
        for (ep in bundled) {
            val game = gameDir(ctx, ep)
            Log.i(TAG, "installing bundled ${ep.id} into $game")
            game.mkdirs()
            val assetDir = "game/${ep.id}"
            for (name in ctx.assets.list(assetDir).orEmpty()) {
                val dest = File(game, name)
                if (dest.exists()) continue
                ctx.assets.open("$assetDir/$name").use { input ->
                    dest.outputStream().use { output -> input.copyTo(output) }
                }
            }
        }
        stamp.writeText(want)
    }

    /**
     * Make the episode runnable: shareware seeded, data present, the matching
     * Omnispeak tables copied into the game dir (always overwritten: they come
     * from this APK and must match its engine), save dir created.
     * Returns null when ready, or the reason it can't run.
     */
    fun prepare(ctx: Context, ep: Episode): String? {
        seedShareware(ctx)
        val game = gameDir(ctx, ep)
        if (!Episodes.hasGameFiles(game, ep)) {
            return "Keen ${ep.num} needs your game files (${ep.dataFiles.joinToString()}): import them first."
        }
        val set = Episodes.tableSet(game, ep)
        for (name in ctx.assets.list("omni/$set").orEmpty()) {
            ctx.assets.open("omni/$set/$name").use { input ->
                File(game, name).outputStream().use { input.copyTo(it) }
            }
        }
        val save = saveDir(ctx, ep)
        save.mkdirs()
        val prefs = Settings.prefs(ctx)
        setConfig(save, "ck_quickSaveConfirm", if (Settings.quickSaveConfirm(prefs)) "true" else "false")
        // The app setting owns the border: /NOBORDER turns it off, and "on" must also
        // undo an in-game Options toggle the engine saved as border = false.
        setConfig(save, "border", if (Settings.egaBorder(prefs)) "true" else "false")
        Log.i(TAG, "prepared ${ep.id} in $game (tables $set)")
        return null
    }

    /**
     * Set one option in the engine's config (OMNISPK.CFG in /USERPATH, matched
     * case-insensitively as the engine does), keeping every other line. Options
     * with no command-line switch (e.g. the QuickSave confirm) are set this way.
     */
    fun setConfig(saveDir: File, name: String, value: String) {
        try {
            val cfg = saveDir.listFiles()?.firstOrNull { it.name.equals("OMNISPK.CFG", ignoreCase = true) }
                ?: File(saveDir, "OMNISPK.CFG")
            val re = Regex("^\\s*" + Regex.escape(name) + "\\s*=.*$")
            val lines = if (cfg.isFile) cfg.readLines().filterNot { re.matches(it) } else emptyList()
            cfg.writeText((lines + "$name = $value").joinToString("\n", postfix = "\n"))
        } catch (e: Exception) {
            Log.w(TAG, "could not set $name in config: $e")
        }
    }

    /**
     * Whether Keen 4's installed data is the registered version rather than the
     * bundled shareware: GAMEMAPS.CK4's size differs from the bundled copy.
     */
    fun isFullVersion(ctx: Context, ep: Episode): Boolean {
        if (!ep.shareware) return true
        val name = "GAMEMAPS.CK${ep.num}"
        val installed = Episodes.find(gameDir(ctx, ep), name) ?: return false
        val assetLen = try {
            ctx.assets.openFd("game/${ep.id}/$name").use { it.length }
        } catch (e: Exception) {
            try {
                ctx.assets.open("game/${ep.id}/$name").use { ins ->
                    var total = 0L
                    val buf = ByteArray(8192)
                    while (true) { val r = ins.read(buf); if (r < 0) break; total += r }
                    total
                }
            } catch (e2: Exception) { return false }
        }
        return installed.length() != assetLen
    }
}
