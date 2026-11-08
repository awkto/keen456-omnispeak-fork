#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Fail if any game data or executable is tracked or staged. Policy: docs/DATA.md. Allowed: tests/dos-golden/*.trc.zst (derived traces) and the Omnispeak tables in data/keen*/.
set -euo pipefail

HERE=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)
cd "$HERE"

files=$( { git ls-files; git diff --cached --name-only; } | sort -u )
offenders=$(printf '%s\n' "$files" \
    | grep -iE '\.(ck4|ck5|ck6|exe|com|trc)$' \
    | grep -viE '^tests/dos-golden/.*\.trc\.zst$' \
    | grep -vE '^data/keen[^/]*/(ACTION|AUDINFOE|AUDIODCT|AUDIOHHD|EGADICT|EGAHEAD|EPISODE|GFXCHUNK|GFXINFOE|MAPHEAD|STRINGS|TILEINFO)\.CK[0-9]$' || true)

if [ -n "$offenders" ]; then
    echo "FAIL: game data or executables are tracked or staged (see docs/DATA.md):" >&2
    printf '%s\n' "$offenders" >&2
    exit 1
fi
echo "OK: no game data tracked"
