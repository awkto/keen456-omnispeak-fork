#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the data the original EXEs carry statically (dictionaries, headers, intro screens).

Native replacement for CKPatch + oracle/dos/KEEN4-6/static/ripck?.pat: reads the same .pat scripts,
picks the %version block whose offsets fit the given executable, and writes the %dump files.

    oracle/tools/ripstatic.py <episode 4|5|6> <KEEN?.EXE (packed or unpacked)> <outdir> [--version 1.4]
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unlzexe import unlzexe, NotLZEXE
import keendata

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dos", "KEEN4-6", "static")


def parse_pat(ep):
    versions, cur = {}, None
    for line in open(os.path.join(ROOT, f"ripck{ep}.pat"), encoding="latin-1"):
        line = line.split("#")[0].strip()
        m = re.match(r"%version\s+(\S+)", line)
        if m:
            cur = m.group(1); continue
        m = re.match(r"%dump\s+(\S+)\s+\$([0-9A-Fa-f]+)\s+(\d+)", line)
        if m and cur and cur != "all":
            versions.setdefault(cur, []).append((m.group(1), int(m.group(2), 16), int(m.group(3))))
    return versions


def load_image(path):
    data = open(path, "rb").read()
    try:
        data = unlzexe(data)
    except NotLZEXE:
        pass
    return data[struct.unpack_from("<H", data, 8)[0] * 16:]


def fits(img, dumps):
    d = {n.split(".")[0].lower(): (o, l) for n, o, l in dumps}
    o, _ = d["maphead"]
    if img[o:o + 2] != b"\xcd\xab":          # RLEW tag
        return False
    o, l = d.get("egahead") or d.get("cgahead")
    return img[o:o + 3] == b"\0\0\0" and img[o + 3:o + 6] != b"\0\0\0"


def main(argv):
    if len(argv) < 4:
        print(__doc__); return 2
    ep, exe, out = argv[1], argv[2], argv[3]
    want = argv[argv.index("--version") + 1] if "--version" in argv else None
    keendata.skip_if_missing_file(exe, "executable")
    img, versions = load_image(exe), parse_pat(ep)
    match = [v for v, d in versions.items() if fits(img, d) and (want in (None, v))]
    if len(match) != 1:
        print(f"ripstatic: {exe}: no unique supported version (candidates: {match or 'none'}; known: {list(versions)})", file=sys.stderr)
        return 1
    os.makedirs(out, exist_ok=True)
    for name, off, length in versions[match[0]]:
        open(os.path.join(out, name.upper()), "wb").write(img[off:off + length])
    print(f"ripstatic: Keen {ep} version {match[0]}: wrote {len(versions[match[0]])} files to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
