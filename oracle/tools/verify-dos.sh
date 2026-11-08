#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Rebuild an episode from dos/ with the original toolchain and prove it matches the original EXE.
#   oracle/tools/verify-dos.sh <4|5|6> [original.exe]
# Needs: ~/bcc/{BC30,BC20} (see oracle/docs/TOOLCHAIN.md), oracle/vendor/{LZEXE,MAKEOBJ}.EXE, $KEEN_DATA (default ~/keen-data-decomp).
set -euo pipefail
EP=$1; HERE=$(cd "$(dirname "$0")/.." && pwd); . "$HERE/tools/keendata.sh"
ORIG=${2:-$(kd_exe "$EP")}
[ -f "${ORIG:-}" ] || { echo "SKIP: no Keen $EP executable in $KD/keen$EP"; exit 0; }
[ -d "$HOME/bcc/BC30/BIN" ] && [ -d "$HOME/bcc/BC20/LIB" ] || { echo "SKIP: Borland C++ 3.0 + 2.0 LIB not installed in ~/bcc"; exit 0; }
B=${KEEN_BUILD:-$HOME/keen-build}/k$EP; rm -rf "$B"; mkdir -p "$B"; cp -a "$HERE/dos/." "$B/"
cp "$HERE/vendor/LZEXE.EXE" "$B/"; cp "$HERE/vendor/MAKEOBJ.EXE" "$B/KEEN4-6/static/"
export BCC_VER=BC30 DOSBUILD_TIMEOUT=1500
python3 "$HERE/tools/ripstatic.py" "$EP" "$ORIG" "$B/KEEN4-6/static"
MK=$(grep -ai "\.\.\\\\keen$EP\\\\" "$B/KEEN4-6/static/make.bat" | tr -d '\r' | grep -vi 'CGA' | sed "s/^/'/; s/\$/'/" | tr '\n' ' ')
eval "\"$HERE/tools/dosbuild.sh\" \"$B\" 'cd KEEN4-6\\STATIC' $MK" > "$B/makeobj.log"
"$HERE/tools/dosbuild.sh" "$B" 'cd KEEN4-6' "PRJ2MAK RCK$EP.PRJ RCK$EP.MAK RCK$EP.CFG" > /dev/null
rm -f "$B/KEEN4-6/RCK$EP.CFG"
# paths for our mount layout; lift the command-line compiler's 100-warning cap (no effect on code)
sed -i "s|^LIBPATH = .*|LIBPATH = D:\\\\BC20\\\\LIB\r|; s|^INCLUDEPATH = .*|INCLUDEPATH = D:\\\\BC30\\\\INCLUDE;.\\\\KEEN$EP\r|; s|^-nKEEN$EP\\\\OBJ|-g255\r\n-w-ucp\r\n-nKEEN$EP\\\\OBJ|" "$B/KEEN4-6/RCK$EP.MAK"
# Keen 6's project links with /x (no map); /s adds the segment map + publics only (identical code), as K4/K5 have
sed -i 's|/x/i/c/P-|/s/i/c/P-|' "$B/KEEN4-6/RCK$EP.MAK"
"$HERE/tools/dosbuild.sh" "$B" 'cd KEEN4-6' "MAKE -fRCK$EP.MAK" > "$B/build.log" || { grep -ai '^error' "$B/build.log" | head; echo "FAIL: build"; exit 1; }
cp "$B/KEEN4-6/KEEN$EP/OBJ/RCK$EP.EXE" "$B/OUT.EXE"
KEEN_TOOLS="$HERE/tools" python3 - "$ORIG" "$B/OUT.EXE" <<'PY'
import sys, os, struct, hashlib
sys.path.insert(0, os.environ['KEEN_TOOLS'])
from unlzexe import unlzexe, NotLZEXE
def parts(p):
    d = open(p, 'rb').read()
    try: d = unlzexe(d)
    except NotLZEXE: pass
    h = struct.unpack_from('<14H', d, 0); size = (h[2] - 1) * 512 + (h[1] or 512)
    rel = sorted((((s << 4) + o) & 0xFFFFF) for o, s in (struct.unpack_from('<HH', d, h[12] + 4 * i) for i in range(h[3])))
    return d[h[4] * 16:size], rel, (h[7], h[8], h[10], h[11])
o, n = parts(sys.argv[1]), parts(sys.argv[2])
print(f"unpacked image: original {len(o[0])} sha256 {hashlib.sha256(o[0]).hexdigest()[:16]} | ours {len(n[0])} sha256 {hashlib.sha256(n[0]).hexdigest()[:16]}")
ok = o == n
if not ok:
    m = min(len(o[0]), len(n[0])); first = next((i for i in range(m) if o[0][i] != n[0][i]), None)
    print(f"  image equal: {o[0]==n[0]} (first diff {first if first is None else hex(first)}), relocs equal: {o[1]==n[1]}, entry/stack equal: {o[2]==n[2]}")
sys.exit(0 if ok else 1)
PY
echo "unpacked image + relocations + entry point: IDENTICAL"
"$HERE/tools/dosbuild.sh" "$B" 'LZEXE OUT.EXE' > /dev/null
if cmp -s "$B/OUT.EXE" "$ORIG"; then echo "PASS Keen $EP: packed EXE byte-identical ($(stat -c %s "$ORIG") bytes, sha256 $(sha256sum "$ORIG" | cut -c1-16)…)"; else echo "FAIL Keen $EP: packed file differs ($(stat -c %s "$B/OUT.EXE") vs $(stat -c %s "$ORIG"))"; exit 1; fi
