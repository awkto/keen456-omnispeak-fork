# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared game-data resolver for every python tool (shell twin: oracle/tools/keendata.sh). Policy: docs/DATA.md.

    import keendata
    d = keendata.require(4)          # episode dir, or prints "SKIP: ..." and exits 0
    exe = keendata.exe(4)            # path to KEEN4E.EXE / KEEN5.EXE (case-insensitive) or None
    keendata.missing(4)              # names of required files that are absent ([] = complete)
    keendata.identify(4)             # '1.4' / None, via ripstatic's offset test (oracle/docs/VERSIONS.md)

Data root = $KEEN_DATA, default ~/keen-data-decomp; one subdir per episode (keen4/ keen5/ keen6/).
Never default to ~/keen-data (mounted into live containers). Game data is never committed.
"""
import glob, os, sys

DEFAULT_ROOT = "~/keen-data-decomp"
EXE_NAMES = {4: ["KEEN4E.EXE", "KEEN4.EXE"], 5: ["KEEN5.EXE", "KEEN5E.EXE"], 6: ["KEEN6.EXE", "KEEN6C.EXE", "KEEN6E.EXE"]}
# What the original game ships; the engine fork needs the first three (see docs/DATA.md).
CK_FILES = ["EGAGRAPH.CK{ep}", "GAMEMAPS.CK{ep}", "AUDIO.CK{ep}"]


def data_root():
    return os.path.expanduser(os.environ.get("KEEN_DATA") or DEFAULT_ROOT)


def episode_dir(ep):
    return os.path.join(data_root(), f"keen{ep}")


def find_file(d, names):
    """First file in d whose name matches one of names, case-insensitively."""
    for n in names:
        for p in glob.glob(os.path.join(glob.escape(d), "*")):
            if os.path.basename(p).upper() == n.upper():
                return p
    return None


def exe(ep):
    return find_file(episode_dir(ep), EXE_NAMES[int(ep)])


def required(ep, with_exe=True):
    """Names of the files a tool needs for this episode."""
    names = [n.format(ep=ep) for n in CK_FILES]
    return names + (["KEEN%s*.EXE" % ep] if with_exe else [])


def missing(ep, with_exe=True):
    d = episode_dir(ep)
    out = [n.format(ep=ep) for n in CK_FILES if not find_file(d, [n.format(ep=ep)])]
    if with_exe and not exe(ep):
        out.append("KEEN%s*.EXE" % ep)
    return out


def skip(msg):
    """Print the standard SKIP line and exit 0 (absent data is not a failure)."""
    print(f"SKIP: {msg}")
    sys.exit(0)


def require(ep, with_exe=True):
    """Return the episode dir, or SKIP + exit 0 when it is absent or incomplete."""
    d = episode_dir(ep)
    if not os.path.isdir(d):
        skip(f"no Keen {ep} game data in {d} (set KEEN_DATA; see docs/DATA.md)")
    m = missing(ep, with_exe)
    if m:
        skip(f"Keen {ep} data incomplete in {d}: missing {', '.join(m)}")
    return d


def skip_if_missing_file(path, what="input"):
    if not os.path.isfile(path):
        skip(f"{what} not found: {path} (game data absent? see docs/DATA.md)")


def identify(ep, exe_path=None):
    """Supported-version name (e.g. '1.4') of the episode's EXE, or None if unknown/unsupported."""
    exe_path = exe_path or exe(ep)
    if not exe_path:
        return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ripstatic
    img, versions = ripstatic.load_image(exe_path), ripstatic.parse_pat(str(ep))
    match = [v for v, d in versions.items() if ripstatic.fits(img, d)]
    return match[0] if len(match) == 1 else None


if __name__ == "__main__":
    print(f"KEEN_DATA = {data_root()}")
    for e in (4, 5, 6):
        m = missing(e)
        print(f"keen{e}: " + ("ok, version " + str(identify(e)) if not m else "missing " + ", ".join(m)))
