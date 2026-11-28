#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Guard for public release assets: fail if any Keen 5/6 game file (or a DOS game executable other than
# the Keen 4 shareware one) is inside <file>. Understands .apk/.zip, .tar.gz, directories and the
# self-extracting Linux keen456 launcher (its embedded payload), recursing into nested .tar.gz/.zip.
# Keen 5/6 are commercial; only their Omnispeak engine tables (EPISODE, EGAHEAD, ... .CK5/.CK6) may ship.
# Prints only counts and verdicts, never the forbidden names, so CI logs stay clean.
#   tools/check-public-artifact.sh FILE|DIR...
set -euo pipefail
# game files are matched by basename: EGAGRAPH/GAMEMAPS/AUDIO for episodes 5 and 6, and the Keen 5/6 executables
BAD='(^|/)((egagraph|gamemaps|audio)\.ck[56]|keen[56][a-z]*\.exe)$'
fail=0
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

listing() { # path -> file list on stdout (recursing into nested archives)
    local p=$1 x
    if [ -d "$p" ]; then (cd "$p" && find . -type f | sed 's|^\./||'); return; fi
    case $p in
    *.apk|*.zip)
        unzip -Z1 "$p"
        x=$(mktemp -d "$tmp/n.XXXX"); unzip -q "$p" -d "$x"; nested "$x" ;;
    *.tar.gz|*.tgz)
        tar tzf "$p"
        x=$(mktemp -d "$tmp/n.XXXX"); tar xzf "$p" -C "$x"; nested "$x" ;;
    *)
        if grep -aq '^__PAYLOAD_BELOW__$' "$p" 2>/dev/null; then
            x=$(mktemp -d "$tmp/n.XXXX")
            local skip; skip=$(awk '/^__PAYLOAD_BELOW__$/{print NR+1; exit}' "$p")
            tail -n +"$skip" "$p" | gzip -dc > "$x/payload.tar"
            tar tf "$x/payload.tar"
            mkdir "$x/p"; tar xf "$x/payload.tar" -C "$x/p"; nested "$x/p"
        else
            basename "$p"
        fi ;;
    esac
}
nested() { # dir: list nested archives and launchers too
    local f
    while IFS= read -r -d '' f; do listing "$f"; done < <(find "$1" -type f \( -name '*.tar.gz' -o -name '*.zip' -o -name '*.apk' -o -name 'keen456' \) -print0)
}

for a in "$@"; do
    [ -e "$a" ] || { echo "FAIL: $a does not exist"; fail=1; continue; }
    list=$(listing "$a")
    n=$(printf '%s\n' "$list" | grep -c . || true)
    bad=$(printf '%s\n' "$list" | grep -ciE "$BAD" || true)
    if [ "$bad" != 0 ]; then
        echo "FAIL: $(basename "$a"): $bad Keen 5/6 game file(s) inside (of $n entries)"; fail=1
    else
        echo "OK: $(basename "$a"): $n entries, no Keen 5/6 game data"
    fi
done
exit $fail
