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

    /**
     * Which set of Omnispeak tables (data/<name> in the repo) matches the
     * player's files. Only Keen 6 has two supported releases: v1.5's
     * EGAGRAPH.CK6 is 464662 bytes, v1.4's 464726 (the last EGAHEAD offset of
     * each table set), so the size tells them apart.
     */
    fun tableSet(dir: File, ep: Episode): String = when (ep.num) {
        6 -> if (find(dir, "EGAGRAPH.CK6")?.length() == 464662L) "keen6e15" else "keen6e14"
        else -> ep.id
    }
}
