#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the Windows x86_64 zip `keen456-windows-x86_64.zip`: the Windows counterpart of
# package-linux-single.sh. Unzips to keen456/ with omnispeak.exe (static MinGW build, SDL2 linked in),
# keen456.exe (tiny launcher, tools/windows-single/launcher.c), `Keen 4.bat` `Keen 5.bat` `Keen 6.bat`,
# the Omnispeak tables, README.txt and, depending on flavor, game data.
#
#   tools/package-windows-single.sh [public|private|both]     (default: both)
#     public   Keen 4 shareware only (tools/fetch-k4-shareware.sh); safe to publish -> dist/
#     private  K4+K5+K6 from $KEEN_DATA (default ~/keen-data-decomp). Commercial: owner only,
#              NEVER publish. -> ~/keen-share/windows/keen456-windows-x86_64.zip (+ .sha256)
# Cross-built in docker ubuntu:24.04 + mingw-w64 (SDL2 is downloaded by the engine Makefile).
# Env: DOCKER_CPUS=2 DOCKER_MEM=2g JOBS=2; PRIVATE_OUT=dir overrides the private output directory.
set -euo pipefail

FLAVORS=${1:-both}
HERE=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
cd "$HERE"
HASH=$( (git rev-parse --short HEAD 2>/dev/null) || echo local)
KD=${KEEN_DATA:-$HOME/keen-data-decomp}
PRIVATE_OUT=${PRIVATE_OUT:-$HOME/keen-share/windows}
NICE="nice -n 19 ionice -c3"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/src" "$tmp/out" dist

echo "== source snapshot =="
$NICE tar czf "$tmp/omnispeak-src.tar.gz" --exclude='./oracle' --exclude='./bin*' --exclude='./obj*' \
    --exclude='./dist' --exclude='./.git' --exclude='./.github' --exclude='./tests' --exclude='./SDL*' .
tar xzf "$tmp/omnispeak-src.tar.gz" -C "$tmp/src"

echo "== cross-building engine + launcher in ubuntu:24.04 (mingw-w64) =="
$NICE docker run --rm --cpus "${DOCKER_CPUS:-2}" --memory "${DOCKER_MEM:-2g}" \
    -v "$tmp/src:/src" -v "$tmp/out:/out" ubuntu:24.04 bash -ec "
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq && apt-get install -y -qq --no-install-recommends make curl wget ca-certificates \
          gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 >/dev/null
      # the mingw cross compilers default to the win32 thread model; static libgcc/libstdc++ avoid runtime DLLs
      make -C /src/src -j${JOBS:-2} PLATFORM=win64 RENDERER=sdl2 BINDIR=/out/bin OBJDIR=/tmp/obj >/dev/null
      x86_64-w64-mingw32-gcc -O2 -mwindows -municode -static -o /out/bin/keen456.exe /src/tools/windows-single/launcher.c -lshell32
      x86_64-w64-mingw32-strip /out/bin/*.exe
      chown -R $(id -u):$(id -g) /out /src"
[ -f "$tmp/out/bin/omnispeak.exe" ] && [ -f "$tmp/out/bin/keen456.exe" ] || { echo "build failed" >&2; exit 1; }
ls -la "$tmp/out/bin"

common="$tmp/common"
mkdir -p "$common/tables"
cp "$tmp/out/bin/omnispeak.exe" "$tmp/out/bin/keen456.exe" "$common/"
for d in "$tmp"/out/bin/*.dll; do [ -e "$d" ] && cp "$d" "$common/"; done
for t in keen4:k4 keen5:k5 keen6e14:k6e14 keen6e15:k6e15; do
    mkdir -p "$common/tables/${t#*:}"; cp -a "data/${t%%:*}/." "$common/tables/${t#*:}/"
done
cp "$tmp/omnispeak-src.tar.gz" "$common/"
cp "$tmp/src/tools/windows-single/launcher.c" "$common/launcher-src.c"
cp "$( [ -f COPYING ] && echo COPYING || echo LICENSE)" "$common/LICENSE-omnispeak"
for ep in 4 5 6; do
    printf '@echo off\r\nstart "" "%%~dp0keen456.exe" %s %%*\r\n' "$ep" > "$common/Keen $ep.bat"
done

readme() { # flavor-specific intro in $1
cat <<R
keen456 - Commander Keen 4/5/6 for Windows: smooth, widescreen
==============================================================
$1

Needs Windows 10 or 11, 64-bit. Nothing to install. Windows SmartScreen may warn because the
programs are unsigned: click "More info" then "Run anyway".

HOW TO RUN
Unzip the whole folder somewhere, then double-click  Keen 4.bat, Keen 5.bat or Keen 6.bat.
Defaults: smooth subpixel scrolling, camera smoothing, 16:9 widescreen, enhanced profile,
modern pogo (Alt = pogo/jump, 180 ms hold), no EGA border, windowed.

IN GAME
  V        cycle the screen filter (off, scanlines, crt, curved, rgb, soft)
  Z        cycle the zoom (vanilla, out 1, out 2: 1.25x / 1.5x more of the level)
  C        cycle the camera dead zone (0 vanilla, 1 default, 2 loose, 3 very loose)
  F11      fullscreen         F10+H  toggle x-ray (see hidden passages)
  Esc      menu (Configure > Options: smoothing, camera, fullscreen, aspect, VSync ...)
  Arrows move, Ctrl jump, Alt pogo, Space fire (the usual Keen controls)
Filter, zoom and camera are remembered.

OPTIONS
Put options (one or more per line, # for comments) in  %APPDATA%\keen456\args.txt , or run
keen456.exe from a command prompt:  keen456.exe 5 --filter=crt --wide=16:10
  --wide[=16:9|16:10|fit]  widescreen (default 16:9)      --no-wide   original 4:3
  --smooth=off|grid|pixel|subpixel  (default subpixel)    --no-camera camera smoothing off
  --filter=off|scanlines|crt|curved|rgb|soft  --pixels=crisp|light|low|medium|high
  --zoom=0|1|2             start zoomed out               --xray      hidden passages see-through
  --camera=0|1|2|3         camera dead zone: 0 vanilla, 1 default (scaled with zoom), 2 loose, 3 very loose
  --fullscreen             start fullscreen               --border    original cyan EGA border
  --classic-controls       no modern pogo                 --vanilla   original: 4:3, no smoothing/fixes
  --data DIR               your game files (DIR holding keen4\ keen5\ keen6\ or the .CKx files)
Anything starting with / goes straight to the engine (e.g. /NOVSYNC /INTEGER).
Run  keen456.exe --help  for the list.

WHERE THINGS LIVE
Saves and config:  %APPDATA%\keen456\keen4 (keen5, keen6)     Options file: %APPDATA%\keen456\args.txt
Game files: the data\ folder here, or %APPDATA%\keen456\data\keen<N>\, or --data / KEEN_DATA.
Need EGAGRAPH.CK<N>, GAMEMAPS.CK<N>, AUDIO.CK<N> (v1.4 or v1.5).

LICENSE
Engine: keen456 fork of Omnispeak, GNU GPL v2 (LICENSE-omnispeak). Complete source of this build:
omnispeak-src.tar.gz (engine) and launcher-src.c (launcher).
R
}

build() { # flavor outzip
    local flavor=$1 out=$2 st="$tmp/stage-$1/keen456"
    rm -rf "$tmp/stage-$1"; mkdir -p "$st"; cp -a "$common/." "$st/"
    case $flavor in
    public)
        KEEN_DATA="$tmp/kd" bash tools/fetch-k4-shareware.sh >/dev/null
        mkdir -p "$st/data/keen4"
        for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$tmp/kd/keen4/$f.CK4" "$st/data/keen4/"; done
        readme "Keen 4: Secret of the Oracle (shareware, freely redistributable, (c) id Software).
Keen 5 and 6 are not included: to play them, put your own files in %APPDATA%\\keen456\\data\\keen5 (and keen6)
or point at them with  Keen 5.bat --data C:\\path\\to\\files  (engine tables are included)." > "$st/README.txt"
        ;;
    private)
        for ep in 4 5 6; do
            mkdir -p "$st/data/keen$ep"
            for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$KD/keen$ep/$f.CK$ep" "$st/data/keen$ep/"; done
        done
        readme "PRIVATE BUILD: contains commercial Keen 4/5/6 data (c) id Software. Owner only; never publish." > "$st/README.txt"
        ;;
    esac
    for f in "$st"/*.txt "$st"/*.bat; do sed -i 's/\r$//; s/$/\r/' "$f"; done
    rm -f "$out"; mkdir -p "$(dirname "$out")"
    (cd "$tmp/stage-$flavor" && $NICE zip -qr -9 "$out" keen456)
    echo "wrote $out ($(du -h "$out" | cut -f1)) [$flavor, $HASH]"
}

case $FLAVORS in public|both) build public "$HERE/dist/keen456-$HASH-windows-x86_64.zip" ;; esac
case $FLAVORS in private|both)
    build private "$PRIVATE_OUT/keen456-windows-x86_64.zip"
    (cd "$PRIVATE_OUT" && sha256sum keen456-windows-x86_64.zip > keen456-windows-x86_64.zip.sha256) ;; esac
