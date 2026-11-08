#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run DOS commands headless in DOSBox and get their output + success back.
#
#   oracle/tools/dosbuild.sh <workdir> '<dos command>' ['<dos command>' ...]
#
# <workdir> is mounted as C: (must be 8.3-clean), $BCC_ROOT (default ~/bcc) as D:.
# PATH is D:\$BCC_VER\BIN (BCC_VER default BC31). Each command's stdout is appended to
# C:\DOSBUILD.LOG; a command that sets errorlevel >= 1 fails the run.
# Exit status: 0 ok, 1 a command failed, 2 DOSBox did not finish (timeout/crash).
set -u
WORK=$(realpath "$1"); shift
BCC_ROOT=${BCC_ROOT:-$HOME/bcc}; BCC_VER=${BCC_VER:-BC31}; TIMEOUT=${DOSBUILD_TIMEOUT:-600}
LOG="$WORK/DOSBUILD.LOG"; BAT="$WORK/DOSBUILD.BAT"; CONF=$(mktemp --suffix=.conf)
rm -f "$LOG"
{
  printf '@echo off\r\n'
  n=0
  for cmd in "$@"; do
    n=$((n+1))
    printf 'echo --- %s >> C:\\DOSBUILD.LOG\r\n' "$(printf '%s' "$cmd" | tr -d '<>|')"
    printf '%s >> C:\\DOSBUILD.LOG\r\n' "$cmd"
    printf 'if errorlevel 1 echo DOSBUILD_FAIL step %d >> C:\\DOSBUILD.LOG\r\n' "$n"
  done
  printf 'echo DOSBUILD_DONE >> C:\\DOSBUILD.LOG\r\n'
} > "$BAT"
cat > "$CONF" <<CFG
[sdl]
output=surface
[dosbox]
memsize=16
[cpu]
core=dynamic
cycles=max
[mixer]
nosound=true
[autoexec]
mount c "$WORK"
mount d "$BCC_ROOT"
set PATH=D:\\$BCC_VER\\BIN;Z:\\
c:
call DOSBUILD.BAT
exit
CFG
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "$TIMEOUT" dosbox -conf "$CONF" -noconsole >/dev/null 2>&1
rm -f "$CONF" "$BAT"
[ -f "$LOG" ] && tr -d '\r' < "$LOG" | grep -v '^DOSBUILD_DONE$'
grep -q DOSBUILD_DONE "$LOG" 2>/dev/null || { echo "dosbuild: DOSBox did not finish (timeout ${TIMEOUT}s?)" >&2; exit 2; }
grep -q DOSBUILD_FAIL "$LOG" && exit 1
exit 0
