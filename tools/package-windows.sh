#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build a tester zip for Windows x86_64 (MinGW cross-build): our Omnispeak fork with the
# display-rate interpolation fix, plus the freely redistributable Keen 4
# shareware. Run from the repo root on a Linux build host with mingw-w64,
# make, curl, unzip and zip (e.g. the Ubuntu 24.04 droplet). SDL2 is
# downloaded by the engine Makefile (LOCAL_SDL defaults on for win64).
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
cd "$HERE"

if [ -e .git ]; then
    HASH=$(git rev-parse --short HEAD 2>/dev/null || true)
fi
HASH=${HASH:-local}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "== building engine (PLATFORM=win64) =="
make -C src -j"$(nproc)" PLATFORM=win64 RENDERER=sdl2 \
    BINDIR="$tmp/bin" OBJDIR="$tmp/obj"
ls -la "$tmp/bin"

stage="$tmp/stage/keen456-smooth"
mkdir -p "$stage"

echo "== staging files =="
cp "$tmp/bin/omnispeak.exe" "$stage/omnispeak.exe"
for d in "$tmp"/bin/*.dll; do [ -e "$d" ] && cp "$d" "$stage/"; done
cp -a data/keen4/. "$stage/"

echo "== fetching Keen 4 v1.4 shareware =="
KEEN_DATA="$tmp/kd" bash tools/fetch-k4-shareware.sh
for f in EGAGRAPH.CK4 GAMEMAPS.CK4 AUDIO.CK4; do
    cp "$tmp/kd/keen4/$f" "$stage/$f"
done

# Optional private build: KEEN_EXTRA=/dir with keen5/ and/or keen6/ (v1.4 EGAGRAPH/GAMEMAPS/AUDIO).
# Commercial data: such a zip is for the owner only, never published.
EXTRA_EPS=""
if [ -n "${KEEN_EXTRA:-}" ]; then
    for ep in 5 6; do
        [ -f "$KEEN_EXTRA/keen$ep/EGAGRAPH.CK$ep" ] || continue
        tabs=data/keen$ep; [ $ep = 6 ] && tabs=data/keen6e14
        cp -a "$tabs"/. "$stage/"
        for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$KEEN_EXTRA/keen$ep/$f.CK$ep" "$stage/"; done
        EXTRA_EPS="$EXTRA_EPS $ep"
    done
fi

if [ -f COPYING ]; then
    cp COPYING "$stage/LICENSE-omnispeak"
else
    cp LICENSE "$stage/LICENSE-omnispeak"
fi

# GPL-2 (#72): ship the exact engine source with the binary (engine code + Omnispeak tables only).
tar czf "$stage/omnispeak-src.tar.gz" -C . --exclude='./oracle' --exclude='./bin*' --exclude='./obj*' --exclude='./dist' --exclude='./.git' --exclude='./.github' --exclude='./tests' --exclude='./SDL*' .

cat > "$stage/NOTICE.txt" <<'EOF'
keen456-smooth is built from github.com/awkto/keen456-omnispeak-fork, a fork of
Omnispeak, licensed under the GNU GPL v2 (see LICENSE-omnispeak). Its complete
source code for this build is in omnispeak-src.tar.gz.

Commander Keen 4: Secret of the Oracle shareware (c) id Software, freely
redistributable.
EOF

echo "== writing launch scripts =="
mk() { printf '@echo off\r\ncd /d "%%~dp0"\r\nomnispeak.exe %s %%*\r\n' "$2" > "$stage/$1"; }
mk play-vanilla.bat         "/EPISODE 4 /NOINTERP"
mk play-smooth.bat          "/EPISODE 4 /PROFILE enhanced /INTERP pixel"
mk play-smooth-subpixel.bat "/EPISODE 4 /PROFILE enhanced /INTERP subpixel"
mk play-smooth-camera.bat   "/EPISODE 4 /PROFILE enhanced /INTERP subpixel /FIX camera-smoothing"
for ep in $EXTRA_EPS; do mk "play-keen$ep.bat" "/EPISODE $ep /PROFILE enhanced /INTERP subpixel"; done

cat > "$stage/README.txt" <<'EOF'
keen456-smooth - Commander Keen 4 on our Omnispeak fork
======================================================

WHAT THIS IS
This is Commander Keen 4: Secret of the Oracle (v1.4 shareware) running on
keen456-smooth, our fork of Omnispeak. The fork adds display-rate
interpolation: the game logic still runs at the original 35 Hz, but frames
are drawn at your monitor's refresh rate by interpolating the camera and
sprites between them. The default is untouched vanilla behaviour.

REQUIREMENTS
- Windows 10 or 11, 64-bit. Nothing to install.
- Windows SmartScreen may warn because the exe is unsigned: click
  "More info" then "Run anyway".

HOW TO RUN
Extract the zip, then double-click one of these in the keen456-smooth folder:

    play-vanilla.bat         vanilla (no interpolation)
    play-smooth.bat          interpolation, whole-pixel snapping
    play-smooth-subpixel.bat interpolation, quarter-pixel snapping (best on 120/144 Hz)
    play-smooth-camera.bat   subpixel + experimental camera smoothing (softens the
                               camera's catch-up when Keen turns, jumps or falls;
                               Keen may drift a few pixels on screen - tell us if you like it)

IN-GAME OPTIONS
F11 toggles fullscreen. Esc > Configure > Options has:
  Smoothing (Off / Grid / Pixel / Subpixel), Camera smoothing (On / Off),
  Fullscreen, Correct aspect ratio, Overscan border, Integer scaling, VSync.
Changes take effect immediately and are saved in OMNISPK.CFG; the launcher you
start with still sets the smoothing mode at startup.

KEEN 5 AND 6
If play-keen5.bat / play-keen6.bat are present, this is a private build with
Keen 5 and 6 (v1.4) included; they start with subpixel smoothing - change it
in the Options menu.

HOW TO COMPARE
Run play-vanilla.bat first, then play-smooth.bat. While smoothing is on,
press F12 to cycle the interpolation mode: grid -> pixel -> subpixel.
'grid' does not interpolate and looks like vanilla's 2-pixel steps, so it is
a good reference for the difference.

WHAT TO LOOK AT
- Scrolling while walking or running: smooth mode should move in much smaller
  steps than vanilla.
- Riding moving platforms and falling.
- The scorebox staying still (it should not shimmer).
- Walking on a moving platform (Keen should not shimmer against it).
- Fades into and out of levels (same length on every monitor).
The biggest difference shows on a high refresh monitor (120 Hz or 144 Hz).

PLEASE REPORT BACK
- Your monitor's refresh rate.
- Which mode looked best (pixel or subpixel).
- Any stutter, tearing or smearing you noticed.

CONTROLS
See Omnispeak's README for the full keyboard and joystick options. The
defaults are the usual Keen controls: arrow keys to move, Ctrl to jump,
Alt to pogo, Space to fire, Esc for the menu.
EOF

echo "== packaging =="
mkdir -p dist
TARBALL="$HERE/dist/keen456-smooth-${HASH}-windows-x86_64${EXTRA_EPS:+-k456}.zip"
rm -f "$TARBALL"
for f in "$stage"/*.txt; do sed -i 's/$/\r/' "$f"; done
(cd "$tmp/stage" && zip -qr "$TARBALL" keen456-smooth)

SIZE=$(du -h "$TARBALL" | cut -f1)
echo "wrote $TARBALL ($SIZE)"
