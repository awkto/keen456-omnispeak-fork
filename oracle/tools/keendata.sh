# SPDX-License-Identifier: GPL-2.0-or-later
# Shared game-data resolver for shell tools; source it (python twin: oracle/tools/keendata.py). Policy: docs/DATA.md.
#   . "$HERE/tools/keendata.sh"
#   kd_dir 4          -> $KD/keen4
#   kd_exe 4          -> path of KEEN4E.EXE / KEEN5.EXE (case-insensitive), empty if absent
#   kd_require 4      -> SKIP + exit 0 unless the episode's EXE and .CK files are present
#   kd_stage 5 DIR    -> copy the files the Omnispeak fork needs (EGAGRAPH/GAMEMAPS/AUDIO.CK5) into DIR
#   kd_stage 5 DIR    -> (also copies data/keen5/* tables)
# Never defaults to ~/keen-data (live containers mount it). Data is never committed.
KD=${KEEN_DATA:-$HOME/keen-data-decomp}
kd_dir() { echo "$KD/keen$1"; }
kd_find() { # dir name -> case-insensitive match
  find -L "$1" -maxdepth 1 -iname "$2" -print -quit 2>/dev/null
}
kd_exe() {
  local d n f; d=$(kd_dir "$1")
  for n in "KEEN$1E.EXE" "KEEN$1.EXE" "KEEN${1}C.EXE"; do f=$(kd_find "$d" "$n"); [ -n "$f" ] && { echo "$f"; return; }; done
  return 0
}
kd_skip() { echo "SKIP: $*"; exit 0; }
kd_require() { # ep [noexe]
  local d m="" n; d=$(kd_dir "$1")
  [ -d "$d" ] || kd_skip "no Keen $1 game data in $d (set KEEN_DATA; see docs/DATA.md)"
  for n in EGAGRAPH GAMEMAPS AUDIO; do [ -n "$(kd_find "$d" "$n.CK$1")" ] || m="$m $n.CK$1"; done
  [ "${2:-}" = noexe ] || [ -n "$(kd_exe "$1")" ] || m="$m KEEN$1*.EXE"
  [ -z "$m" ] || kd_skip "Keen $1 data incomplete in $d: missing$m"
}
kd_stage() { # ep destdir: engine run dir = the 3 .CK files + the repo's Omnispeak tables data/keen<N>/ (docs/DATA.md)
  local n f o; mkdir -p "$2"
  o=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/data/keen$1
  [ -d "$o" ] || o=${o}e14   # Keen 6: data/keen6e14 (v1.4)
  [ ! -d "$o" ] || cp "$o"/* "$2"/
  for n in EGAGRAPH GAMEMAPS AUDIO; do f=$(kd_find "$(kd_dir "$1")" "$n.CK$1"); cp "$f" "$2/$n.CK$1"; done
}
