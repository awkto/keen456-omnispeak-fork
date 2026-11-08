#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the Android debug APK (android/, #102): fetch the pinned SDL2 source and the
# Keen 4 v1.4 shareware (bundled as an asset, never committed), then run Gradle.
#
#   tools/build-android.sh            -> dist/keen456-omni-<sha>-debug.apk
#
# Needs JDK 17 and an Android SDK (ANDROID_HOME / ANDROID_SDK_ROOT) with
# platforms;android-34, build-tools;34.0.0, ndk;27.2.12479018 and cmake;3.22.1.
# Heavy: run it on a build droplet (tools/droplet.sh), not on the dev host.
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
cd "$HERE"

SDL_VERSION=${SDL_VERSION:-2.32.10}
SDL_SHA256=${SDL_SHA256:-5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165}
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz"

: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-}}"
[ -n "$ANDROID_HOME" ] && [ -d "$ANDROID_HOME" ] || { echo "FAIL: set ANDROID_HOME to an Android SDK" >&2; exit 1; }
export ANDROID_HOME

deps=android/deps
mkdir -p "$deps"
if [ "$(cat "$deps/SDL2/.version" 2>/dev/null)" != "$SDL_VERSION" ]; then
    echo "== fetching SDL2 $SDL_VERSION =="
    tmp=$(mktemp -d)
    curl -fsSL "$SDL_URL" -o "$tmp/sdl.tgz"
    got=$(sha256sum "$tmp/sdl.tgz" | cut -d' ' -f1)
    [ "$got" = "$SDL_SHA256" ] || { echo "FAIL: SDL2 tarball sha256 $got, expected $SDL_SHA256" >&2; rm -rf "$tmp"; exit 1; }
    tar -xzf "$tmp/sdl.tgz" -C "$tmp"
    rm -rf "$deps/SDL2"
    mv "$tmp/SDL2-$SDL_VERSION" "$deps/SDL2"
    echo "$SDL_VERSION" > "$deps/SDL2/.version"
    rm -rf "$tmp"
fi

echo "== Keen 4 shareware asset =="
assets=android/app/src/main/assets/game/keen4
mkdir -p "$assets"
if ! [ -f "$assets/EGAGRAPH.CK4" ] || ! [ -f "$assets/GAMEMAPS.CK4" ] || ! [ -f "$assets/AUDIO.CK4" ]; then
    kd=$(mktemp -d)
    KEEN_DATA="$kd" bash tools/fetch-k4-shareware.sh
    for f in EGAGRAPH.CK4 GAMEMAPS.CK4 AUDIO.CK4; do cp "$kd/keen4/$f" "$assets/$f"; done
    rm -rf "$kd"
fi
bash tools/check-no-data.sh

echo "== gradle assembleDebug =="
echo "sdk.dir=$ANDROID_HOME" > android/local.properties
(cd android && ./gradlew --no-daemon assembleDebug)

HASH=${BUILD_HASH:-$(git rev-parse --short HEAD 2>/dev/null || echo local)}
mkdir -p dist
out="dist/keen456-omni-$HASH-debug.apk"
cp android/app/build/outputs/apk/debug/app-debug.apk "$out"

# Sanity: both ABIs and the assets the app needs are inside.
list=$(unzip -l "$out")
for f in lib/arm64-v8a/libmain.so lib/arm64-v8a/libSDL2.so lib/x86_64/libmain.so lib/x86_64/libSDL2.so \
         assets/game/keen4/EGAGRAPH.CK4 assets/omni/keen4/EPISODE.CK4 assets/omni/keen5/EPISODE.CK5 \
         assets/omni/keen6e14/EPISODE.CK6 assets/omni/keen6e15/EPISODE.CK6; do
    grep -q " $f\$" <<<"$list" || { echo "FAIL: $f missing from $out" >&2; exit 1; }
done
if grep -qiE 'assets/game/keen[56]/|\.exe$' <<<"$list"; then
    echo "FAIL: commercial data or an executable ended up in $out" >&2; exit 1
fi
echo "OK: $out"
