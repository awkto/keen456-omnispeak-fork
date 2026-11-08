#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# DOS ground-truth runner: boot Keen 4 or 5 headless in the pinned DOSBox-X, optionally
# drive it with keys, and capture screenshots / an AVI. See oracle/docs/HARNESS.md.
#
#   oracle/harness/dosrun.sh <4|5> [--exe path] [--args "..."] [--keys script] [--avi out.avi]
#                           [--shots dir] [--timeout s] [--wait s] [--keep]
#
# Defaults: original EXE from $KEEN_DATA/keen<N> (default ~/keen-data-decomp), args
# "/TEDLEVEL 1", wait 12 s before the first screenshot, timeout 60 s (hard cap, whole run).
# Exit: 0 ok, 1 usage/setup error, 2 emulator died early or produced no screenshot, 3 timeout.
set -uo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/tools/keendata.sh"   # KD, kd_exe (KEEN_DATA policy: docs/DATA.md)
DOSBOX_X=${DOSBOX_X:-$HOME/git/keen456/native/vendor/dosbox-x-2026.08.02/dosbox-x}

usage() { sed -n '3,9p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ $# -ge 1 ] || usage
EP=$1; shift
case "$EP" in 4|5) ;; *) echo "dosrun: episode must be 4 or 5 (Keen 6 not supported yet)" >&2; exit 1;; esac

EXE=; ARGS="/TEDLEVEL 1"; KEYS=; AVI=; SHOTS=; TIMEOUT=60; WAIT=12; KEEP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --exe) EXE=$2; shift 2;;
    --args) ARGS=$2; shift 2;;
    --keys) KEYS=$2; shift 2;;
    --avi) AVI=$2; shift 2;;
    --shots) SHOTS=$2; shift 2;;
    --timeout) TIMEOUT=$2; shift 2;;
    --wait) WAIT=$2; shift 2;;
    --keep) KEEP=1; shift;;
    -h|--help) usage;;
    *) echo "dosrun: unknown option $1" >&2; exit 1;;
  esac
done

# ---- inputs -----------------------------------------------------------------
DATA=$KD/keen$EP
[ -d "$DATA" ] || { echo "SKIP: no game data in $DATA"; exit 0; }
[ -x "$DOSBOX_X" ] || { echo "dosrun: DOSBox-X not found at $DOSBOX_X (set DOSBOX_X=)" >&2; exit 1; }
[ -n "$EXE" ] || EXE=$(kd_exe "$EP")
[ -f "$EXE" ] || { echo "dosrun: EXE not found: ${EXE:-$DATA/KEEN$EP*.EXE}" >&2; exit 1; }
[ -z "$KEYS" ] || [ -f "$KEYS" ] || { echo "dosrun: keys script not found: $KEYS" >&2; exit 1; }
for t in xvfb-run import xdotool; do command -v $t >/dev/null || { echo "dosrun: missing dependency: $t" >&2; exit 1; }; done
EXENAME=$(basename "$EXE" | tr a-z A-Z)
[ -n "$SHOTS" ] || SHOTS=$PWD/dosrun-shots
mkdir -p "$SHOTS"; SHOTS=$(realpath "$SHOTS")
[ -z "$AVI" ] || { mkdir -p "$(dirname "$AVI")"; AVI=$(realpath -m "$AVI"); }
[ -z "$KEYS" ] || KEYS=$(realpath "$KEYS")

# ---- work dir (throwaway copy; originals are never touched) -------------------
W=$(mktemp -d "${TMPDIR:-/tmp}/dosrun.XXXXXX")
cleanup() { [ "$KEEP" = 1 ] && echo "dosrun: kept $W" || rm -rf "$W"; }
trap cleanup EXIT
mkdir -p "$W/game" "$W/capture" "$W/home"
for f in "$DATA"/*.CK$EP; do cp "$f" "$W/game/"; done
rm -f "$W/game/CONFIG.CK$EP" "$W/game/SAVEGAM"*   # clean config/saves -> deterministic start
cp "$EXE" "$W/game/$EXENAME"

# Mapper: bind only "capture video" to Scroll Lock (Keen does not use it).
printf 'hand_video "key 71"\n' > "$W/mapper.map"

cat > "$W/dosbox-x.conf" <<CFG
[sdl]
autolock=false
fullscreen=false
output=surface
mapperfile=$W/mapper.map
[render]
aspect=false
scaler=none
[log]
logfile=$W/dosbox.log
[dosbox]
quit warning=false
machine=svga_s3
memsize=16
captures=$W/capture
saveremark=false
[cpu]
core=normal
cputype=auto
cycles=fixed 25000
[mixer]
nosound=true
[sblaster]
sbtype=none
[autoexec]
@echo off
mount c "$W/game"
c:
$EXENAME $ARGS
exit
CFG

# ---- inner script, runs inside Xvfb --------------------------------------------
cat > "$W/inner.sh" <<'INNER'
#!/usr/bin/env bash
export HOME="$W/home" SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH="$(dirname "$DOSBOX_X")/lib:${LD_LIBRARY_PATH:-}"
"$DOSBOX_X" -conf "$W/dosbox-x.conf" -nomenu -fastlaunch >"$W/stdout.log" 2>&1 &
PID=$!
shot() { import -window root "$SHOTS/$1.png" 2>>"$W/import.log" && echo "dosrun: shot $SHOTS/$1.png"; }
alive() { kill -0 $PID 2>/dev/null; }
[ -z "$AVI" ] || { sleep 3; xdotool key Scroll_Lock; }   # start AVI capture during boot
sleep "$WAIT"
alive || { echo "dosrun: emulator exited before first screenshot" >&2; echo DEAD > "$W/status"; exit 0; }
shot boot
if [ -n "$KEYS" ]; then
  n=0
  while read -r cmd a b || [ -n "${cmd:-}" ]; do
    case "${cmd:-}" in ''|'#'*) continue;; esac
    alive || { echo "dosrun: emulator exited during keys script" >&2; break; }
    case "$cmd" in
      sleep) sleep "$a";;
      key)   xdotool key "$a";;                    # xdotool key name(s), e.g. Return, alt, ctrl+x
      down)  xdotool keydown "$a";;
      up)    xdotool keyup "$a";;
      type)  xdotool type --delay 80 "$a${b:+ $b}";;
      shot)  shot "${a:-key$((++n))}";;
      *) echo "dosrun: unknown keys command: $cmd" >&2;;
    esac
  done < "$KEYS"
fi
[ -z "$AVI" ] || { xdotool key Scroll_Lock; sleep 1; }
alive && touch "$W/alive_at_end"
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
wait $PID 2>/dev/null
echo OK > "$W/status"
INNER
chmod +x "$W/inner.sh"

export W SHOTS AVI KEYS WAIT DOSBOX_X
nice -n 19 ionice -c3 timeout -k 5 "$TIMEOUT" \
  xvfb-run -a -s "-screen 0 1024x768x24" bash "$W/inner.sh" >"$W/run.log" 2>&1
RC=$?
cat "$W/run.log"

if [ -n "$AVI" ]; then
  A=$(ls "$W"/capture/*.avi 2>/dev/null | head -1)
  if [ -n "$A" ]; then cp "$A" "$AVI"; echo "dosrun: avi $AVI"; else echo "dosrun: no AVI captured" >&2; fi
fi
pkill -f "$W/dosbox-x.conf" 2>/dev/null   # belt and braces
if [ $RC -eq 124 ] || [ $RC -eq 137 ]; then echo "dosrun: TIMEOUT after ${TIMEOUT}s" >&2; exit 3; fi
[ "$(cat "$W/status" 2>/dev/null)" = OK ] && [ -f "$SHOTS/boot.png" ] || { echo "dosrun: FAIL (status=$(cat "$W/status" 2>/dev/null)); see --keep" >&2; tail -5 "$W/stdout.log" >&2; KEEP=1; exit 2; }
echo "dosrun: ok (emulator ${EP}: $EXENAME $ARGS; ${DOSBOX_X##*/})"
exit 0
