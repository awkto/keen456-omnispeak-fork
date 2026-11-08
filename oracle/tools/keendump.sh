#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the KEENDUMP (trace-writing) DOS executable and prove its DGROUP layout is the normal one.
#   oracle/tools/keendump.sh build <4|5|6>                       -> $KEEN_BUILD/k<N>-dump/KEENDUMP.EXE (unpacked)
#   oracle/tools/keendump.sh run <4|5|6> <demo> <out.trc>        -> plays built-in demo <demo> once, headless, writes the trace
# The normal build's map ($KEEN_BUILD/k<N>/.../RCK<N>.MAP, from `make verify-k<N>`) is needed for the
# DGROUP check. Format and rules: oracle/docs/TRACE_FORMAT.md. Needs what oracle/tools/verify-dos.sh needs.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
if [ -f "$HERE/tools/keendata.sh" ]; then . "$HERE/tools/keendata.sh"; else
  KD=${KEEN_DATA:-$HOME/keen-data-decomp}; kd_exe() { ls "$KD/keen$1"/KEEN$1*.EXE 2>/dev/null | head -1 || true; }; fi
KB=${KEEN_BUILD:-$HOME/keen-build}
CMD=${1:-}; EP=${2:-}
case "$EP" in 4|5|6) ;; *) sed -n '3,8p' "$0" | sed 's/^# \{0,1\}//'; exit 1;; esac

build() {
  local ORIG B MK
  ORIG=$(kd_exe "$EP")
  [ -f "${ORIG:-}" ] || { echo "SKIP: no Keen $EP executable in $KD/keen$EP"; exit 0; }
  [ -d "$HOME/bcc/BC30/BIN" ] && [ -d "$HOME/bcc/BC20/LIB" ] || { echo "SKIP: Borland C++ 3.0 + 2.0 LIB not installed in ~/bcc"; exit 0; }
  # Same pipeline as oracle/tools/verify-dos.sh, in its own staging dir, plus -DKEENDUMP.
  B=$KB/k$EP-dump; rm -rf "$B"; mkdir -p "$B"; cp -a "$HERE/dos/." "$B/"
  cp "$HERE/vendor/MAKEOBJ.EXE" "$B/KEEN4-6/static/"
  export BCC_VER=BC30 DOSBUILD_TIMEOUT=1500
  python3 "$HERE/tools/ripstatic.py" "$EP" "$ORIG" "$B/KEEN4-6/static"
  MK=$(grep -ai "\.\.\\\\keen$EP\\\\" "$B/KEEN4-6/static/make.bat" | tr -d '\r' | grep -vi 'CGA' | sed "s/^/'/; s/\$/'/" | tr '\n' ' ')
  eval "\"$HERE/tools/dosbuild.sh\" \"$B\" 'cd KEEN4-6\\STATIC' $MK" > "$B/makeobj.log"
  "$HERE/tools/dosbuild.sh" "$B" 'cd KEEN4-6' "PRJ2MAK RCK$EP.PRJ RCK$EP.MAK RCK$EP.CFG" > /dev/null
  rm -f "$B/KEEN4-6/RCK$EP.CFG"
  sed -i "s|^LIBPATH = .*|LIBPATH = D:\\\\BC20\\\\LIB\r|; s|^INCLUDEPATH = .*|INCLUDEPATH = D:\\\\BC30\\\\INCLUDE;.\\\\KEEN$EP\r|; s|^-nKEEN$EP\\\\OBJ|-g255\r\n-w-ucp\r\n-DKEENDUMP\r\n-nKEEN$EP\\\\OBJ|" "$B/KEEN4-6/RCK$EP.MAK"
# Keen 6's project links with /x (no map); /s adds the segment map + publics only (identical code), as K4/K5 have
sed -i 's|/x/i/c/P-|/s/i/c/P-|' "$B/KEEN4-6/RCK$EP.MAK"
  grep -aq '^-DKEENDUMP' "$B/KEEN4-6/RCK$EP.MAK" || { echo "FAIL: could not add -DKEENDUMP to RCK$EP.MAK"; exit 1; }
  "$HERE/tools/dosbuild.sh" "$B" 'cd KEEN4-6' "MAKE -fRCK$EP.MAK" > "$B/build.log" || { grep -ai '^error' "$B/build.log" | head; echo "FAIL: build"; exit 1; }
  cp "$B/KEEN4-6/KEEN$EP/OBJ/RCK$EP.EXE" "$B/KEENDUMP.EXE"
  dgroup_check "$KB/k$EP/KEEN4-6/KEEN$EP/OBJ/RCK$EP.MAP" "$B/KEEN4-6/KEEN$EP/OBJ/RCK$EP.MAP"
  echo "OK Keen $EP: $B/KEENDUMP.EXE ($(stat -c %s "$B/KEENDUMP.EXE") bytes, unpacked)"
}

# Every PUBLIC symbol in DGROUP (states, objarray, gamestate, spritearray ...) must have the same
# offset in both maps, and DGROUP's segments (_DATA, _BSS, _COMDEF_ ...) the same lengths.
dgroup_check() {
  [ -f "$1" ] || { echo "FAIL: normal map $1 missing; run 'make verify-k$EP' first (same KEEN_BUILD)"; exit 1; }
  KEEN_TOOLS="$HERE/tools" python3 - "$1" "$2" <<'PY'
import sys, os
sys.path.insert(0, os.environ['KEEN_TOOLS'])
from map2syms import parse_map
def dgroup(path):
    segs, pubs = parse_map(path)
    by = {n: (s, o) for s, o, n in pubs}
    frame = by['_gamestate'][0]
    syms = {n: o for n, (s, o) in by.items() if s == frame}
    lens = [(name, ln) for s, e, ln, name, cls in segs if frame * 16 <= s < frame * 16 + 0x10000]
    return frame, syms, lens
fa, na, la = dgroup(sys.argv[1]); fb, nb, lb = dgroup(sys.argv[2])
moved = sorted(n for n in na if nb.get(n) != na[n])
extra = sorted(set(nb) - set(na))
st = [n for n in na if n.startswith('_s_')]
print(f"DGROUP frame: normal {fa:04X}, KEENDUMP {fb:04X} (frame may differ; near pointers are offsets)")
print(f"DGROUP publics: normal {len(na)}, KEENDUMP {len(nb)}; moved {len(moved)}, added {len(extra)}; states compared {len(st)}")
print(f"DGROUP segments: normal {la}")
if lb != la: print(f"                 KEENDUMP {lb}")
for n in ('_objarray', '_gamestate', '_dummyobj', '_spritearray', '_s_keenstand'):
    if n in na: print(f"  {n[1:]:12} {na[n]:04X} -> {nb.get(n, -1):04X}")
ok = not moved and not extra and la == lb
for n in moved[:10]: print(f"  MOVED {n}: {na[n]:04X} -> {nb.get(n)}")
for n in extra[:10]: print(f"  ADDED {n}")
print("DGROUP check: " + ("PASS (all DGROUP addresses identical to the normal build)" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
}

run() {
  local DEMO=${3:?demo number}; local OUT=${4:?output trace}
  local EXE=$KB/k$EP-dump/KEENDUMP.EXE W f RC
  [ -f "$EXE" ] || { echo "keendump: $EXE missing; run: oracle/tools/keendump.sh build $EP"; exit 1; }
  [ -d "$KD/keen$EP" ] || { echo "SKIP: no game data in $KD/keen$EP"; exit 0; }
  # Headless DOSBox via oracle/tools/dosbuild.sh (C: = throwaway copy of the data, never the originals).
  # The game quits by itself when the demo ends, so DOSBox exits as soon as the trace is complete.
  W=$(mktemp -d "${TMPDIR:-/tmp}/keendump.XXXXXX")
  for f in "$KD/keen$EP"/*.CK$EP; do cp "$f" "$W/"; done
  rm -f "$W/CONFIG.CK$EP" "$W"/SAVEGAM*       # clean config -> deterministic start
  cp "$EXE" "$W/KEENDUMP.EXE"
  RC=0; DOSBUILD_TIMEOUT=${KEENDUMP_TIMEOUT:-180} nice -n 19 ionice -c3 \
    "$HERE/tools/dosbuild.sh" "$W" "KEENDUMP.EXE /NOWAIT /PLAYDEMO $DEMO /DUMPFILE KD.TRC" > "$W/run.log" 2>&1 || RC=$?
  if [ $RC -ne 0 ] || [ ! -s "$W/KD.TRC" ]; then
    cat "$W/run.log"; echo "FAIL: dosbox rc=$RC, trace $( [ -f "$W/KD.TRC" ] && stat -c %s "$W/KD.TRC" || echo missing)"; rm -rf "$W"; exit 1
  fi
  mkdir -p "$(dirname "$OUT")"; cp "$W/KD.TRC" "$OUT"; rm -rf "$W"
  echo "trace $OUT: $(stat -c %s "$OUT") bytes"
}

case "$CMD" in
  build) build;;
  run) run "$@";;
  *) sed -n '3,8p' "$0" | sed 's/^# \{0,1\}//'; exit 1;;
esac
