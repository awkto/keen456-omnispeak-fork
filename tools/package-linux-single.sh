#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the single-file Linux launcher `keen456` (x86_64): a self-extracting script
# (tools/linux-single/runner.sh + gzip tar payload) holding the SDL2 engine build, bundled
# libSDL2, the Omnispeak tables for Keen 4/5/6 (v1.4 and v1.5) and, depending on flavor, game data.
#
#   tools/package-linux-single.sh [public|private|both]     (default: both)
#     public   Keen 4 shareware only (fetched by tools/fetch-k4-shareware.sh); safe to publish.
#              K5/K6: the user passes --data DIR. Output: dist/keen456-<hash>-linux-x86_64
#     private  K4+K5+K6 from $KEEN_DATA (default ~/keen-data-decomp). Commercial data: owner only,
#              NEVER publish. Output: ~/keen-share/linux/keen456-<hash>-linux-x86_64
#
# The engine is built in docker ubuntu:22.04 (glibc 2.35) so it runs on 22.04+/Debian 12+/Fedora 36+.
# Env: DOCKER_CPUS=4 DOCKER_MEM=4g JOBS=4; PRIVATE_OUT=dir overrides the private output directory.
set -euo pipefail

FLAVORS=${1:-both}
HERE=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
cd "$HERE"
HASH=$( (git rev-parse --short HEAD 2>/dev/null) || echo local)
KD=${KEEN_DATA:-$HOME/keen-data-decomp}
PRIVATE_OUT=${PRIVATE_OUT:-$HOME/keen-share/linux}
NICE="nice -n 19 ionice -c3"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/src" "$tmp/out" dist

echo "== source snapshot =="
# GPL-2: the exact engine source ships inside the payload (engine + Omnispeak tables only).
$NICE tar czf "$tmp/omnispeak-src.tar.gz" --exclude='./oracle' --exclude='./bin*' --exclude='./obj*' \
    --exclude='./dist' --exclude='./.git' --exclude='./.github' --exclude='./tests' --exclude='./SDL*' .
tar xzf "$tmp/omnispeak-src.tar.gz" -C "$tmp/src"

echo "== building engine in ubuntu:22.04 (RENDERER=sdl2) =="
$NICE docker run --rm --cpus "${DOCKER_CPUS:-4}" --memory "${DOCKER_MEM:-4g}" \
    -v "$tmp/src:/src" -v "$tmp/out:/out" ubuntu:22.04 bash -ec "
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential libsdl2-dev >/dev/null
      make -C /src/src -j${JOBS:-4} RENDERER=sdl2 BINDIR=/out/bin OBJDIR=/tmp/obj >/dev/null
      mkdir -p /out/lib
      cp -L \$(ldd /out/bin/omnispeak | awk '/libSDL2-2.0.so.0/{print \$3}') /out/lib/
      chown -R $(id -u):$(id -g) /out"
[ -x "$tmp/out/bin/omnispeak" ] || { echo "engine build failed" >&2; exit 1; }
strip "$tmp/out/bin/omnispeak" 2>/dev/null || true

# ---- common payload: engine, SDL2, tables, legal
common="$tmp/common"
mkdir -p "$common/tables"
cp -a "$tmp/out/bin" "$tmp/out/lib" "$common/"
for t in keen4:k4 keen5:k5 keen6e14:k6e14 keen6e15:k6e15; do
    mkdir -p "$common/tables/${t#*:}"; cp -a "data/${t%%:*}/." "$common/tables/${t#*:}/"
done
cp "$tmp/omnispeak-src.tar.gz" "$common/"
cp "$( [ -f COPYING ] && echo COPYING || echo LICENSE)" "$common/LICENSE-omnispeak"

build() { # flavor outfile
    local flavor=$1 out=$2 st="$tmp/stage-$1"
    rm -rf "$st"; cp -a "$common" "$st"
    case $flavor in
    public)
        KEEN_DATA="$tmp/kd" bash tools/fetch-k4-shareware.sh >/dev/null
        mkdir -p "$st/data/keen4"
        for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$tmp/kd/keen4/$f.CK4" "$st/data/keen4/"; done
        cat > "$st/NOTICE.txt" <<'N'
keen456 is built from github.com/awkto/keen456-omnispeak-fork, a fork of Omnispeak, licensed under
the GNU GPL v2 (see LICENSE-omnispeak). Complete source for this build: omnispeak-src.tar.gz.
Commander Keen 4: Secret of the Oracle shareware (c) id Software, freely redistributable.
Keen 5 and 6 are not included: run with --data DIR pointing at your own copy.
N
        ;;
    private)
        for ep in 4 5 6; do
            mkdir -p "$st/data/keen$ep"
            for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$KD/keen$ep/$f.CK$ep" "$st/data/keen$ep/"; done
        done
        cat > "$st/NOTICE.txt" <<'N'
PRIVATE BUILD: contains commercial Keen 4/5/6 data (c) id Software. Owner only; never publish.
Engine: GPL v2 keen456 Omnispeak fork (LICENSE-omnispeak, omnispeak-src.tar.gz).
N
        ;;
    esac
    $NICE tar -C "$st" -cf - . | $NICE gzip -9 > "$tmp/payload.tar.gz"
    local id; id=$(sha256sum "$tmp/payload.tar.gz" | cut -c1-12)
    sed -e "s/@PAYLOAD_ID@/$id/" -e "s/@BUILD_ID@/$HASH/" -e "s/@FLAVOR@/$flavor/" \
        tools/linux-single/runner.sh > "$out"
    cat "$tmp/payload.tar.gz" >> "$out"
    chmod 755 "$out"
    echo "wrote $out ($(du -h "$out" | cut -f1)) [$flavor, payload $id]"
}

case $FLAVORS in public|both) build public "$HERE/dist/keen456-$HASH-linux-x86_64" ;; esac
case $FLAVORS in private|both)
    mkdir -p "$PRIVATE_OUT"; build private "$PRIVATE_OUT/keen456-$HASH-linux-x86_64" ;; esac
