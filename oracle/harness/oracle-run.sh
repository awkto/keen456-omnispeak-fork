#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# The full oracle pass, meant for a throwaway droplet (tools/droplet.sh; run from anywhere, paths resolve to oracle/), not the dev host:
#   1. rebuild Keen 4 + 5 byte-identical, build the KEENDUMP variants
#   2. golden traces: every built-in demo, played twice, must be byte-identical (#58)
#   3. build the engine fork (Omnispeak, DEBUG for /DUMPFILE) and compare its trace of every
#      demo against golden (#60 Keen 4, #62 Keen 5), with interpolation off and on
#
#   oracle/harness/oracle-run.sh [outdir]      (default tests/dos-golden/ at the repo root)
# Writes <outdir>/k<E>-d<N>.trc, MANIFEST.sha256 and oracle-report.txt. Exit 0 = everything matched.
set -uo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); cd "$HERE"
. tools/keendata.sh
OUT=${1:-$HERE/../tests/dos-golden}; mkdir -p "$OUT"; OUT=$(realpath "$OUT")
REP=$OUT/oracle-report.txt; : > "$REP"
log() { echo "$*" | tee -a "$REP"; }
FAIL=0; JOBS=${JOBS:-$(nproc)}
DEMOS=${DEMOS:-"4:0 4:1 4:2 4:3 4:4 5:0 5:1 5:2 5:3 5:4 6:0 6:1 6:2 6:3 6:4"}

log "== oracle run $(date -u +%FT%TZ) on $(hostname), engine $(git -C "$HERE/.." rev-parse --short HEAD 2>/dev/null || echo '?')"
for e in ${EPISODES:-4 5 6}; do
  tools/verify-dos.sh $e > "$OUT/verify-k$e.log" 2>&1 && grep -q PASS "$OUT/verify-k$e.log" \
    && log "verify-k$e: PASS" || { log "verify-k$e: FAIL (see verify-k$e.log)"; FAIL=1; }
  tools/keendump.sh build $e > "$OUT/keendump-k$e.log" 2>&1 \
    && log "keendump-k$e: built" || { log "keendump-k$e: FAIL"; FAIL=1; }
done
[ $FAIL = 0 ] || { log "ABORT: builds failed"; exit 1; }

# Golden traces: each demo twice (run a/b), in parallel. keendump.sh run uses its own temp dir per call.
run_dos() { local e=${1%%:*} d=${1##*:} r=$2; tools/keendump.sh run "$e" "$d" "$OUT/k$e-d$d.$r.trc" > "$OUT/k$e-d$d.$r.log" 2>&1; }
export -f run_dos; export OUT
for r in a b; do for x in $DEMOS; do echo "$x $r"; done; done | xargs -P "$JOBS" -n 2 bash -c 'run_dos "$0" "$1"'
for x in $DEMOS; do e=${x%%:*}; d=${x##*:}; a=$OUT/k$e-d$d.a.trc; b=$OUT/k$e-d$d.b.trc
  if [ -s "$a" ] && cmp -s "$a" "$b"; then mv "$a" "$OUT/k$e-d$d.trc"; rm -f "$b"
    log "golden k$e demo $d: reproducible, $(python3 tools/tracecmp.py info -e $e "$OUT/k$e-d$d.trc" | head -1)"
  elif [ -s "$a" ] && python3 tools/tracecmp.py cmp -e $e "$a" "$b" > /dev/null; then mv "$a" "$OUT/k$e-d$d.trc"; rm -f "$b"
    log "golden k$e demo $d: reproducible on compared fields (TimeCount differs)"
  else log "golden k$e demo $d: NOT REPRODUCIBLE / missing"; FAIL=1; fi
done
(cd "$OUT" && sha256sum k*-d*.trc > MANIFEST.sha256)

# Engine fork: DEBUG build with the SDL2 renderer (interpolation hooks live there).
EB=$HERE/engine-build; rm -rf "$EB"; ENGINE=$(cd "$HERE/.." && pwd)
mkdir -p "$EB"; tar -C "$ENGINE" --exclude=./oracle --exclude=./.git --exclude="./bin*" --exclude="./obj*" -cf - . | tar -C "$EB" -xf -
if make -C "$EB/src" -j"$JOBS" DEBUG=1 RENDERER=sdl2 > "$OUT/engine-build.log" 2>&1; then log "engine: built (DEBUG, sdl2)"
else log "engine: BUILD FAILED (see engine-build.log)"; exit 1; fi
BIN=$(find "$EB" -maxdepth 2 -type f -name omnispeak -perm -u+x | head -1)
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
for x in $DEMOS; do e=${x%%:*}; d=${x##*:}; g=$OUT/k$e-d$d.trc; [ -s "$g" ] || continue
  for mode in off pixel; do
    R=$(mktemp -d); kd_stage $e "$R"; t=$OUT/fork-k$e-d$d-$mode.trc
    args=(/EPISODE $e /PLAYDEMO $d /DUMPFILE "$t"); [ $mode = off ] && args+=(/NOINTERP) || args+=(/INTERP $mode)
    (cd "$R" && timeout 300 "$BIN" "${args[@]}" > "$OUT/fork-k$e-d$d-$mode.log" 2>&1); rm -rf "$R"
    if cmp -s "$g" "$t"; then log "fork k$e demo $d interp=$mode: BYTE-IDENTICAL to DOS"; rm -f "$t"
    elif python3 tools/tracecmp.py cmp -e $e "$g" "$t" > "$OUT/fork-k$e-d$d-$mode.cmp" 2>&1; then
      log "fork k$e demo $d interp=$mode: equal on compared fields"; rm -f "$t"
    else log "fork k$e demo $d interp=$mode: DIVERGES: $(head -3 "$OUT/fork-k$e-d$d-$mode.cmp" | tr '\n' ' ')"; FAIL=1; fi
  done
done
log "== result: $([ $FAIL = 0 ] && echo ALL MATCH || echo FAILURES)"
exit $FAIL
