#!/bin/bash
# vanilla-guard.sh: check that the 'vanilla' profile still plays the built-in
# demos exactly like the reference, and that the 'enhanced' profile leaves the
# game state untouched during demos.
#
# Builds a CK_DEBUG binary of the working tree (RENDERER=sdl2, so the SDL2-only
# code such as interpolation is included), plays the built-in demos headless
# with /DUMPFILE in both profiles and compares the playloop dumps:
#   vanilla  == reference   (tests/demoN.dumpE, or a dump from a git ref, --base)
#   enhanced == vanilla     (sim-affecting fixes must be off in demos/dumps)
# Exits non-zero on any difference.
#
# Usage: tools/vanilla-guard.sh [--base tests|<git-ref>] [--episodes "4 5"] [--demos "0 1"] [--keep]
# Environment: KEEN_DATA (default ~/keen-data-decomp) with keen4/ and keen5/
#   holding the v1.4 AUDIO, EGAGRAPH and GAMEMAPS files; JOBS (default 4);
#   RUN_TIMEOUT per demo in seconds (default 300); GUARD_WORK (default: mktemp).
# Prints SKIP and exits 0 if the game data is missing.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License, version 2 or (at your option)
# any later version.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=tests
EPISODES="4 5"
DEMOS="0 1"
KEEP=0
while [ $# -gt 0 ]; do
	case "$1" in
	--base) BASE=$2; shift 2 ;;
	--episodes) EPISODES=$2; shift 2 ;;
	--demos) DEMOS=$2; shift 2 ;;
	--keep) KEEP=1; shift ;;
	*) echo "usage: $0 [--base tests|<git-ref>] [--episodes \"4 5\"] [--demos \"0 1\"] [--keep]" >&2; exit 2 ;;
	esac
done

KEEN_DATA=${KEEN_DATA:-$HOME/keen-data-decomp}
JOBS=${JOBS:-4}
RUN_TIMEOUT=${RUN_TIMEOUT:-300}
NICE="nice -n 19 ionice -c3"

# Byte compare first; if that fails, compare with oracle/tools/tracecmp.py, which ignores
# the wall-clock TimeCount and the presentation-only sprite field (docs in oracle/docs/
# TRACE_FORMAT.md). Wall-clock noise is reported but does not fail the guard.
same() { # a b ep -> 0 identical, 1 equal on compared fields, 2 differs
	cmp -s "$1" "$2" && return 0
	python3 "$ROOT/oracle/tools/tracecmp.py" cmp -e "$3" "$1" "$2" > /dev/null 2>&1 && return 1
	return 2
}

for ep in $EPISODES; do
	for f in AUDIO EGAGRAPH GAMEMAPS; do
		if [ ! -f "$KEEN_DATA/keen$ep/$f.CK$ep" ]; then
			echo "SKIP: $KEEN_DATA/keen$ep/$f.CK$ep not found (set KEEN_DATA)"
			exit 0
		fi
	done
done

WORK=${GUARD_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/vanilla-guard.XXXXXX")}
mkdir -p "$WORK"
cleanup() { [ $KEEP = 1 ] || rm -rf "$WORK"; }
trap cleanup EXIT
echo "vanilla-guard: work dir $WORK"

# build <srcdir> <outdir>
build() {
	echo "building $1 -> $2"
	if ! timeout 900 $NICE make -C "$1/src" -j"$JOBS" DEBUG=1 RENDERER=sdl2 \
		BINDIR="$2/bin" OBJDIR="$2/obj" > "$2.build.log" 2>&1; then
		echo "FAIL: build of $1 failed, see $2.build.log"
		KEEP=1
		exit 1
	fi
}

# rundemo <bindir> <episode> <demo> <dumpfile> [extra args...]
rundemo() {
	local bin=$1 ep=$2 demo=$3 dump=$4
	shift 4
	local run="$WORK/run"
	rm -rf "$run"
	mkdir -p "$run"
	cp "$bin"/* "$run"/
	cp "$KEEN_DATA/keen$ep/AUDIO.CK$ep" "$KEEN_DATA/keen$ep/EGAGRAPH.CK$ep" \
		"$KEEN_DATA/keen$ep/GAMEMAPS.CK$ep" "$run"/
	(cd "$run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "$RUN_TIMEOUT" \
		$NICE ./omnispeak /EPISODE "$ep" /PLAYDEMO "$demo" /DUMPFILE "$dump" "$@" \
		> "$dump.log" 2>&1)
	local rc=$?
	if [ $rc -ne 0 ] || [ ! -s "$dump" ]; then
		echo "FAIL: k$ep demo $demo ($*) exited with $rc, see $dump.log"
		return 1
	fi
}

build "$ROOT" "$WORK/cur"
if [ "$BASE" != tests ]; then
	mkdir -p "$WORK/base-src"
	git -C "$ROOT" archive "$BASE" | tar -x -C "$WORK/base-src" || { echo "FAIL: cannot export $BASE"; exit 1; }
	build "$WORK/base-src" "$WORK/base"
fi

FAILS=0
for ep in $EPISODES; do
	for demo in $DEMOS; do
		tag="k$ep demo $demo"
		if [ "$BASE" = tests ]; then
			ref="$ROOT/tests/demo$demo.dump$ep"
		else
			ref="$WORK/base_k${ep}_d$demo.dump"
			rundemo "$WORK/base/bin" "$ep" "$demo" "$ref" || { FAILS=$((FAILS + 1)); continue; }
		fi
		van="$WORK/vanilla_k${ep}_d$demo.dump"
		enh="$WORK/enhanced_k${ep}_d$demo.dump"
		rundemo "$WORK/cur/bin" "$ep" "$demo" "$van" /PROFILE vanilla || { FAILS=$((FAILS + 1)); continue; }
		rundemo "$WORK/cur/bin" "$ep" "$demo" "$enh" /PROFILE enhanced || { FAILS=$((FAILS + 1)); continue; }

		same "$van" "$ref" "$ep"; r=$?
		if [ $r = 0 ]; then
			echo "ok:   $tag vanilla == $BASE ($(sha1sum < "$van" | cut -c1-12))"
		elif [ $r = 1 ]; then
			echo "ok:   $tag vanilla == $BASE on compared fields (wall-clock TimeCount differs)"
		else
			echo "FAIL: $tag vanilla differs from $BASE: $(cmp "$van" "$ref" 2>&1 | head -1)"
			FAILS=$((FAILS + 1))
		fi
		same "$enh" "$van" "$ep"; r=$?
		if [ $r = 0 ]; then
			echo "ok:   $tag enhanced == vanilla"
		elif [ $r = 1 ]; then
			echo "ok:   $tag enhanced == vanilla on compared fields (wall-clock TimeCount differs)"
		else
			echo "FAIL: $tag enhanced differs from vanilla: $(cmp "$enh" "$van" 2>&1 | head -1)"
			FAILS=$((FAILS + 1))
		fi
		grep -q '^profile vanilla$' "$van.meta" && grep -q '^profile enhanced$' "$enh.meta" ||
			{ echo "FAIL: $tag missing or wrong .meta sidecar"; FAILS=$((FAILS + 1)); }
	done
done

if [ $FAILS -ne 0 ]; then
	KEEP=1
	echo "vanilla-guard: FAILED ($FAILS problems); files kept in $WORK"
	exit 1
fi
echo "vanilla-guard: PASSED"
