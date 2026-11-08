#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the WebAssembly (Emscripten) version into dist/. Needs emcc on PATH (source emsdk_env.sh first).
#   KEEN_DATA=/path   dir with keen4/ keen5/ [keen6/] each holding EGAGRAPH.CKn GAMEMAPS.CKn AUDIO.CKn
#   EPISODES="4 5 6"  episodes to package (default: those present under KEEN_DATA)
#   K6VER=keen6e14|keen6e15  engine tables for Keen 6 (default keen6e14, the version we have)
#   TABLES_ONLY="5 6" episodes packaged with ONLY the Omnispeak engine tables (no game files); the page then asks
#                    the player for their own files (GitHub Pages build, #99). Keen 6 also gets k6e15.data (v1.5 tables).
# dist/episodes.json tells web/index.html which episodes are "full" (play directly) or "tables" (ask for files).
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
tar czf "$DIST/omnispeak-src.tar.gz" -C "$HERE" --exclude='./bin*' --exclude='./obj*' --exclude='./dist' --exclude='./.git' --exclude='./tests' --exclude='./SDL*' .
cp "$HERE"/LICENSE "$DIST"/LICENSE.txt
TABLES_ONLY=" ${TABLES_ONLY:-} "
pack() { # name stagedir
  python3 "$PACKAGER" "$DIST/$1.data" --preload "$2@/" --js-output="$DIST/$1.js" --no-node
}
modes=""
for ep in ${EPISODES:-4 5 6}; do
  tabs=$HERE/data/keen$ep; [ "$ep" = 6 ] && tabs=$HERE/data/$K6VER
  stage=$(mktemp -d); cp "$tabs"/*.CK$ep "$stage"/
  if [[ "$TABLES_ONLY" == *" $ep "* ]]; then
    modes="$modes\"$ep\":\"tables\","
    if [ "$ep" = 6 ]; then # tables for both Keen 6 versions: the page picks by the size of the player's files
      s15=$(mktemp -d); cp "$HERE"/data/keen6e15/*.CK6 "$s15"/; s14=$(mktemp -d); cp "$HERE"/data/keen6e14/*.CK6 "$s14"/
      pack k6 "$s14"; pack k6e15 "$s15"; rm -rf "$s14" "$s15"
    else
      pack k$ep "$stage"
    fi
  else
    [ -f "$KD/keen$ep/EGAGRAPH.CK$ep" ] || { echo "skip episode $ep (no data)"; rm -rf "$stage"; continue; }
    modes="$modes\"$ep\":\"full\","
    for f in EGAGRAPH GAMEMAPS AUDIO; do cp "$KD/keen$ep/$f.CK$ep" "$stage"/; done
    pack k$ep "$stage"
  fi
  rm -rf "$stage"
done
# sizes of the real game files that the engine tables describe (the last offset in EGAHEAD/AUDIOHHD is the file size);
# the page uses them to validate user-supplied files and to tell Keen 6 v1.4 from v1.5.
python3 - "$HERE" "$DIST" "$modes" <<'PY'
import sys, json
here, dist, modes = sys.argv[1:4]
def last(b, w):
    v = [int.from_bytes(b[i:i+w], 'little') for i in range(0, len(b) - w + 1, w)]
    v = [x for x in v if x not in (0, 2**(8*w) - 1)]
    return v[-1]
vers = {}
for ep, name, ver in ((5, 'keen5', '1.4'), (6, 'keen6e14', '1.4'), (6, 'keen6e15', '1.5')):
    d = f'{here}/data/{name}/'
    vers.setdefault(str(ep), {})[ver] = {
        'EGAGRAPH': last(open(d + f'EGAHEAD.CK{ep}', 'rb').read(), 3),
        'AUDIO': last(open(d + f'AUDIOHHD.CK{ep}', 'rb').read(), 4)}
j = json.loads('{' + modes.rstrip(',') + '}')
j['versions'] = vers
json.dump(j, open(dist + '/episodes.json', 'w'))
PY
ls -la "$DIST"
