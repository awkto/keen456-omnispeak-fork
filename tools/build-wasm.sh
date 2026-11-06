#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the WebAssembly (Emscripten) version into dist/. Needs emcc on PATH (source emsdk_env.sh first).
#   KEEN_DATA=/path   dir with keen4/ keen5/ [keen6/] each holding EGAGRAPH.CKn GAMEMAPS.CKn AUDIO.CKn
#   EPISODES="4 5 6"  episodes to package (default: those present under KEEN_DATA)
#   K6VER=keen6e14|keen6e15  engine tables for Keen 6 (default keen6e14, the version we have)
# One <name>.data per episode is produced (engine tables + game data), loaded on demand by web/index.html.
# Game data is NEVER committed; dist/ then contains it and must stay private unless the data is freely redistributable.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
KD=${KEEN_DATA:?set KEEN_DATA}
K6VER=${K6VER:-keen6e14}
command -v emcc >/dev/null || { echo "emcc not found (source emsdk_env.sh)" >&2; exit 1; }
PACKAGER=$(dirname "$(readlink -f "$(command -v emcc)")")/tools/file_packager.py
[ -f "$PACKAGER" ] || PACKAGER=$(dirname "$(readlink -f "$(command -v emcc)")")/tools/file_packager

cd "$HERE/src"
make PLATFORM=emscripten RENDERER=sdl2 WITH_KEEN6=1 KEEN6VER="$K6VER" -j"$(nproc)" ../bin-wasm/omnispeak.js

DIST=$HERE/dist; rm -rf "$DIST"; mkdir -p "$DIST"
cp "$HERE"/bin-wasm/omnispeak.{js,wasm} "$DIST"/
cp "$HERE"/web/index.html "$DIST"/
# GPL-2 (#72): the engine is distributed as a binary here, so ship its exact source with it.
# The tree only holds engine code and Omnispeak's own data tables, never game data.
tar czf "$DIST/omnispeak-src.tar.gz" -C "$HERE" --exclude='./bin*' --exclude='./obj*' --exclude='./dist' --exclude='./.git' .
cp "$HERE"/LICENSE "$DIST"/LICENSE.txt
for ep in ${EPISODES:-4 5 6}; do
  [ -f "$KD/keen$ep/EGAGRAPH.CK$ep" ] || { echo "skip episode $ep (no data)"; continue; }
  tabs=$HERE/data/keen$ep; [ "$ep" = 6 ] && tabs=$HERE/data/$K6VER
  stage=$(mktemp -d); cp "$tabs"/*.CK$ep "$stage"/
  for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$KD/keen$ep/$f.CK$ep" "$stage"/; done
  python3 "$PACKAGER" "$DIST/k$ep.data" --preload "$stage@/" --js-output="$DIST/k$ep.js" --no-node
  rm -rf "$stage"
done
ls -la "$DIST"
