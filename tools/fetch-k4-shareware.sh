#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Fetch the freely redistributable Keen 4 v1.4 shareware into $KEEN_DATA/keen4 (for CI).
# Source: https://davidgow.net/keen/4keen14.zip (override with K4_URL). Policy: docs/DATA.md.
set -euo pipefail

KD=${KEEN_DATA:-$HOME/keen-data-decomp}
URL=${K4_URL:-https://davidgow.net/keen/4keen14.zip}
DEST="$KD/keen4"

if [ -f "$DEST/KEEN4E.EXE" ]; then
    echo "already present"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "fetching $URL"
curl -fsSL "$URL" -o "$tmp/4keen14.zip"
unzip -q "$tmp/4keen14.zip" -d "$tmp/x"

mkdir -p "$DEST"
for n in KEEN4E.EXE EGAGRAPH.CK4 GAMEMAPS.CK4 AUDIO.CK4; do
    f=$(find "$tmp/x" -iname "$n" -print -quit)
    [ -n "$f" ] || { echo "FAIL: $n not found in $URL" >&2; exit 1; }
    cp "$f" "$DEST/$n"
done

python3 "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../oracle/tools/keendata.py" | grep '^keen4:' | grep -q 'version 1.4' \
    || { echo "FAIL: keendata.py does not report Keen 4 version 1.4 in $DEST" >&2; exit 1; }
echo "Keen 4 shareware v1.4 in $DEST"
