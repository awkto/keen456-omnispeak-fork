#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Guard for the public GitHub Pages artifact (#99): fail if any Keen 5/6 game file or its contents is in <dir>.
# Keen 5/6 are commercial; only their Omnispeak engine tables (data/keen5, keen6e14/15) may be shipped.
set -euo pipefail
D=${1:?usage: $0 dist-dir}
fail=0
# 1. file names
bad=$(find "$D" -type f \( -iname 'EGAGRAPH.CK[56]' -o -iname 'GAMEMAPS.CK[56]' -o -iname 'AUDIO.CK[56]' -o -iname 'KEEN[56]*.EXE' \) -print)
[ -z "$bad" ] || { echo "FAIL: game files in artifact:"; echo "$bad"; fail=1; }
# 2. packaged .data/.js: names listed in the package metadata and size = exactly the engine tables
for ep in 5 6; do
  for name in k$ep k${ep}e15; do
    [ -f "$D/$name.js" ] || continue
    if grep -qiE 'EGAGRAPH|GAMEMAPS|AUDIO\.CK' "$D/$name.js"; then echo "FAIL: $name.js lists a game file"; fail=1; fi
    tabs=$(cat "$(dirname "$0")/../data/$( [ "$name" = k5 ] && echo keen5 || { [ "$name" = k6 ] && echo keen6e14 || echo keen6e15; } )"/*.CK$ep | wc -c)
    sz=$(stat -c %s "$D/$name.data")
    [ "$sz" = "$tabs" ] || { echo "FAIL: $name.data is $sz bytes, engine tables are $tabs"; fail=1; }
  done
done
# 3. content: nothing else may be big enough to hold a game file, and no *.data besides k4 may exceed the tables
for f in "$D"/k5*.data "$D"/k6*.data; do [ -f "$f" ] && [ "$(stat -c %s "$f")" -gt 200000 ] && { echo "FAIL: $f suspiciously large"; fail=1; }; done
# 4. the episodes manifest must say tables-only
grep -q '"5": "tables"' "$D/episodes.json" && grep -q '"6": "tables"' "$D/episodes.json" || { echo "FAIL: episodes.json does not mark 5/6 as tables"; fail=1; }
[ "$fail" = 0 ] && echo "OK: no Keen 5/6 game data in $D"
exit $fail
