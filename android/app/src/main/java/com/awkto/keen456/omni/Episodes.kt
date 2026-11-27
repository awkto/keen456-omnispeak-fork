// SPDX-License-Identifier: GPL-2.0-or-later
package com.awkto.keen456.omni

import java.io.File

/**
 * The three episodes and where their files live. Keen 4 v1.4 shareware is
 * redistributable and ships in the APK (fetched at build time, never
 * committed); Keen 5 and 6 are commercial and the player imports their own
 * files in the PickerActivity.
 *
 * Omnispeak needs only the three data files per episode (no DOS executable)
 * plus its own tables from the repo's data/ dir, which the APK carries as
 * assets under omni/ (see GameSetup.prepare).
 */
data class Episode(val num: Int, val id: String, val title: String) {
    /** Only Keen 4 (v1.4 shareware) is freely redistributable. */
    val shareware: Boolean get() = num == 4
    val dataFiles: List<String> get() = listOf("EGAGRAPH", "GAMEMAPS", "AUDIO").map { "$it.CK$num" }
}

object Episodes {
    val ALL = listOf(
        Episode(4, "keen4", "Secret of the Oracle"),
        Episode(5, "keen5", "The Armageddon Machine"),
        Episode(6, "keen6", "Aliens Ate My Babysitter!"),
    )

    fun byId(id: String): Episode? = ALL.firstOrNull { it.id == id }

    /** The file in dir whose name matches, case-insensitively. */
    fun find(dir: File, name: String): File? =
        dir.listFiles()?.firstOrNull { it.isFile && it.name.equals(name, ignoreCase = true) }

    /** Whether dir holds this episode's three data files. */
    fun hasGameFiles(dir: File, ep: Episode): Boolean = ep.dataFiles.all { find(dir, it) != null }

    /** Every file in dir that is one of this episode's data files, in any case. */
    fun dataFilesIn(dir: File, ep: Episode): List<File> =
        dir.listFiles()?.filter { f -> f.isFile && ep.dataFiles.any { it.equals(f.name, ignoreCase = true) } }.orEmpty()

    /**
     * Which set of Omnispeak tables (data/<name> in the repo) to run the
     * player's files with: the detected release's, else the episode's default.
     */
    fun tableSet(dir: File, ep: Episode): String =
        Releases.check(dir, ep).release?.tableSet ?: Releases.forEpisode(ep).first().tableSet
}

/** One supported release of an episode: its Omnispeak table set and the exact data file sizes it needs. */
data class Release(val ep: Episode, val label: String, val tableSet: String, val sizes: Map<String, Long>) {
    fun describe(): String = "Keen ${ep.num} $label: " +
        ep.dataFiles.joinToString(", ") { "$it ${sizes[it]}" }
}

/** What is installed in a directory compared with the supported releases. */
data class Check(val ep: Episode, val found: Map<String, Long?>, val release: Release?) {
    val ok: Boolean get() = release != null
    val missing: List<String> get() = ep.dataFiles.filter { found[it] == null }

    fun foundText(): String = ep.dataFiles.joinToString(", ") { n -> found[n]?.let { "$n $it bytes" } ?: "$n missing" }

    /** The supported release closest to what was found and the files that differ from it. */
    fun closestHint(): String {
        val best = Releases.forEpisode(ep).minByOrNull { r -> ep.dataFiles.count { r.sizes[it] != found[it] } } ?: return ""
        val diff = ep.dataFiles.filter { best.sizes[it] != found[it] }
        return "Closest is ${best.label}: " + diff.joinToString(", ") { "$it should be ${best.sizes[it]} bytes" + (found[it]?.let { s -> " (is $s)" } ?: " (missing)") } + "."
    }

    /** The full explanation for a refused import or a refused Play. */
    fun problem(): String =
        "Found: ${foundText()}.\n\n${closestHint()}\n\nSupported:\n" +
            Releases.forEpisode(ep).joinToString("\n") { "• " + it.describe() }
}

/**
 * The releases this build supports, read from the Omnispeak tables the APK
 * carries (assets/omni/<set>): the table files say exactly how big the data
 * files they describe are, so the sizes are derived, not hardcoded:
 *   EGAGRAPH = the largest chunk offset in EGAHEAD (3-byte entries),
 *   AUDIO    = the largest offset in AUDIOHHD (4-byte entries),
 *   GAMEMAPS = the largest map header offset in MAPHEAD + 42 (38-byte header
 *              and the 4-byte "!ID!" signature, the last thing in the file).
 * A file that doesn't match makes the engine read past its end (black screen)
 * or decode garbage (#102), so only exact matches are accepted.
 */
object Releases {
    /** Table sets per episode, oldest first, with the release they belong to. */
    private val SETS = mapOf(4 to listOf("v1.4" to "keen4"), 5 to listOf("v1.4" to "keen5"),
        6 to listOf("v1.4" to "keen6e14", "v1.5" to "keen6e15"))

    @Volatile private var cache: Map<Int, List<Release>>? = null

    /** Call once with any Context (reads the table assets). */
    fun init(ctx: android.content.Context) {
        if (cache != null) return
        val am = ctx.applicationContext.assets
        val all = HashMap<Int, List<Release>>()
        for (ep in Episodes.ALL) {
            all[ep.num] = SETS.getValue(ep.num).map { (label, set) ->
                fun table(name: String): ByteArray = am.open("omni/$set/$name.CK${ep.num}").use { it.readBytes() }
                Release(ep, label, set, mapOf(
                    "EGAGRAPH.CK${ep.num}" to egaGraphSize(table("EGAHEAD")),
                    "GAMEMAPS.CK${ep.num}" to gameMapsSize(table("MAPHEAD")),
                    "AUDIO.CK${ep.num}" to audioSize(table("AUDIOHHD")),
                ))
            }
        }
        cache = all
    }

    fun forEpisode(ep: Episode): List<Release> =
        cache?.get(ep.num) ?: throw IllegalStateException("Releases.init not called")

    /** The installed (or about to be installed) data files in dir against the supported releases. */
    fun check(dir: File, ep: Episode): Check {
        val found = ep.dataFiles.associateWith { Episodes.find(dir, it)?.length() }
        val rel = forEpisode(ep).firstOrNull { r -> ep.dataFiles.all { found[it] == r.sizes[it] } }
        return Check(ep, found, rel)
    }

    private fun u16(b: ByteArray, i: Int) = (b[i].toInt() and 0xff) or ((b[i + 1].toInt() and 0xff) shl 8)
    private fun u32(b: ByteArray, i: Int): Long = u16(b, i).toLong() or (u16(b, i + 2).toLong() shl 16)

    internal fun egaGraphSize(head: ByteArray): Long {
        var max = 0L
        for (i in 0 until head.size / 3) {
            val v = u16(head, 3 * i).toLong() or ((head[3 * i + 2].toLong() and 0xff) shl 16)
            if (v != 0xffffffL && v > max) max = v
        }
        return max
    }

    internal fun audioSize(head: ByteArray): Long =
        (0 until head.size / 4).maxOf { u32(head, 4 * it).let { v -> if (v >= 0x80000000L) 0L else v } }

    /** MAPHEAD: uint16 RLEW tag, then 100 uint32 header offsets (0 / 0xFFFFFFFF = no map). */
    internal fun gameMapsSize(head: ByteArray): Long {
        var max = 0L
        for (i in 0 until 100) {
            val v = u32(head, 2 + 4 * i)
            if (v != 0xffffffffL && v > max) max = v
        }
        return max + 42
    }
}
