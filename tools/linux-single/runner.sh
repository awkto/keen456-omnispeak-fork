#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# keen456: single-file Linux launcher for Commander Keen 4/5/6 on the keen456 Omnispeak fork.
# Self-extracting: this script header is followed by an gzip-compressed tar payload.
# Extracted once to ${XDG_CACHE_HOME:-~/.cache}/keen456/p-<id>/ and reused.
PAYLOAD_ID=@PAYLOAD_ID@
BUILD_ID=@BUILD_ID@
FLAVOR=@FLAVOR@

die() { echo "keen456: $*" >&2; exit 1; }

# ---- locate ourselves (and the busybox-style name we were invoked as)
self_name=${0##*/}
case $0 in
    */*) self=$0 ;;
    *) self=$(command -v -- "$0") || self=$0 ;;
esac
self_real=$(readlink -f -- "$self" 2>/dev/null) || self_real=$self

# ---- extract the payload on first run
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/keen456
ROOT=$CACHE/p-$PAYLOAD_ID
if [ ! -f "$ROOT/.ok" ]; then
    command -v gzip >/dev/null 2>&1 || die "needs 'gzip'"
    command -v tar >/dev/null 2>&1 || die "needs 'tar'"
    mkdir -p "$CACHE" || die "cannot create $CACHE"
    tmpd=$(mktemp -d "$CACHE/.extract.XXXXXX") || die "cannot create temp dir in $CACHE"
    skip=$(awk '/^__PAYLOAD_BELOW__$/{print NR+1; exit}' "$self_real")
    echo "keen456: unpacking to $ROOT (first run only)..." >&2
    if tail -n +"$skip" "$self_real" | gzip -dc | tar -x -C "$tmpd"; then
        : > "$tmpd/.ok"
        rm -rf "$ROOT"
        mv "$tmpd" "$ROOT" || die "cannot move into place"
    else
        rm -rf "$tmpd"; die "unpacking failed (truncated download?)"
    fi
fi

# ---- usage
list_present() {
    out=
    for e in 4 5 6; do
        if find_data "$e" >/dev/null 2>&1; then out="$out $e"; fi
    done
    echo "${out:- none}"
}
usage() {
    cat <<USAGE
keen456 $BUILD_ID ($FLAVOR) - Commander Keen 4/5/6, smooth, widescreen

Usage: keen456 [4|5|6] [options] [/ENGINEARG ...]
       (or run through a symlink named keen4, keen5 or keen6)
Episodes available:$(list_present)

Options:
  --wide[=16:9|16:10|fit]   widescreen view (default: --wide=16:9)
  --no-wide                 original 4:3
  --smooth=off|grid|pixel|subpixel   display-rate smoothing (default: subpixel)
  --no-camera               turn off camera smoothing (default: on)
  --filter=NAME             off scanlines crt curved rgb soft amber green (default: off)
  --pixels=NAME             crisp light low medium high (default: crisp)
  --classic-controls        no modern pogo (default: Alt = pogo/jump, 180 ms hold)
  --vanilla                 original behaviour: 4:3, no smoothing, no fixes
  --xray                    show hidden passages see-through (F10+H toggles in game)
  --zoom=0|1|2              start zoomed out: 0 normal, 1 = 1.25x, 2 = 1.5x more of the level
  --camera=0|1|2|3          camera dead zone: 0 vanilla, 1 scaled with the zoom (default),
                            2 loose, 3 very loose (Keen gets near the edge before it scrolls)
  --fullscreen              start fullscreen (default: windowed; F11 toggles)
  --border                  draw the original cyan EGA border (default: off)
  --data DIR                game files: DIR containing keen4/ keen5/ keen6/ or the .CKx files
                            (or env KEEN_DATA; or put them in ~/.local/share/keen456/data)
  -h, --help                this text
In game: V cycles the screen filter, Z the zoom, C the camera dead zone (all remembered).
Other /ENGINEARGS (e.g. /NOVSYNC /INTEGER /FIX name) go straight to the engine.
Preferences: put options in ${XDG_CONFIG_HOME:-~/.config}/keen456/args (one per line).
Saves/config: ${XDG_DATA_HOME:-~/.local/share}/keen456/keen<N>/
Examples:  keen456 5 --filter=crt --wide=16:10     keen456 6 --vanilla
USAGE
}

# ---- find the game files for episode $1; prints the directory
ci_file() { # dir NAME -> prints matching path case-insensitively
    [ -f "$1/$2" ] && { echo "$1/$2"; return 0; }
    for f in "$1"/*; do
        [ -f "$f" ] || continue
        b=${f##*/}
        [ "$(printf %s "$b" | tr a-z A-Z)" = "$2" ] && { echo "$f"; return 0; }
    done
    return 1
}
has_data() { # dir ep
    for n in EGAGRAPH GAMEMAPS AUDIO; do ci_file "$1" "$n.CK$2" >/dev/null || return 1; done
}
find_data() { # ep
    ep=$1
    for base in "$data_opt" "$KEEN_DATA" "$ROOT/data" "${XDG_DATA_HOME:-$HOME/.local/share}/keen456/data"; do
        [ -n "$base" ] || continue
        for d in "$base/keen$ep" "$base/KEEN$ep" "$base"; do
            [ -d "$d" ] && has_data "$d" "$ep" && { echo "$d"; return 0; }
        done
    done
    return 1
}

# ---- parse options: config file first, then the real command line
data_opt=
cfg=${XDG_CONFIG_HOME:-$HOME/.config}/keen456/args
if [ -f "$cfg" ]; then
    set -f
    # shellcheck disable=SC2046
    set -- $(sed 's/#.*//' "$cfg") "$@"
    set +f
fi

ep=
case $self_name in keen4|keen5|keen6) ep=${self_name#keen} ;; esac
wide=16:9 smooth=subpixel camera=1 filter= pixels= zoom= deadzone= modern=1 vanilla= xray= fs= border= explicit_wide= explicit_smooth= explicit_cam= explicit_ctl=

n=$#
while [ "$n" -gt 0 ]; do
    a=$1; shift; n=$((n - 1))
    case $a in
        4|5|6|keen4|keen5|keen6) ep=${a#keen} ;;
        -h|--help|-\?) usage; exit 0 ;;
        --wide) wide=16:9; explicit_wide=1 ;;
        --wide=16:9|--wide=16:10|--wide=fit) wide=${a#--wide=}; explicit_wide=1 ;;
        --wide=*) die "bad $a (use 16:9, 16:10 or fit)" ;;
        --no-wide) wide=; explicit_wide=1 ;;
        --smooth=off|--smooth=grid|--smooth=pixel|--smooth=subpixel) smooth=${a#--smooth=}; explicit_smooth=1 ;;
        --smooth=*) die "bad $a (use off, grid, pixel or subpixel)" ;;
        --no-camera) camera=; explicit_cam=1 ;;
        --camera) camera=1; explicit_cam=1 ;;
        --filter=off|--filter=scanlines|--filter=crt|--filter=curved|--filter=rgb|--filter=soft|--filter=amber|--filter=green) filter=${a#--filter=} ;;
        --filter=*) die "bad $a (use off scanlines crt curved rgb soft amber green)" ;;
        --pixels=crisp|--pixels=light|--pixels=low|--pixels=medium|--pixels=high) pixels=${a#--pixels=} ;;
        --pixels=*) die "bad $a (use crisp light low medium high)" ;;
        --zoom=0|--zoom=1|--zoom=2) zoom=${a#--zoom=} ;;
        --zoom=*) die "bad $a (use 0, 1 or 2)" ;;
        --camera=0|--camera=1|--camera=2|--camera=3) deadzone=${a#--camera=} ;;
        --camera=*) die "bad $a (use 0, 1, 2 or 3)" ;;
        --classic-controls) modern=; explicit_ctl=1 ;;
        --vanilla) vanilla=1 ;;
        --xray) xray=1 ;;
        --fullscreen) fs=1 ;;
        --border) border=1 ;;
        --no-border) border= ;;
        --data=*) data_opt=${a#--data=} ;;
        --data) [ $# -gt 0 ] || die "--data needs a directory"; data_opt=$1; shift; n=$((n - 1)) ;;
        *) set -- "$@" "$a" ;;   # raw engine arg: keep, in order
    esac
done
# raw args are now in "$@" (rotated back in order, since each loop pass appended at the end)

if [ -n "$vanilla" ]; then
    [ -n "$explicit_wide" ] || wide=
    [ -n "$explicit_smooth" ] || smooth=off
    [ -n "$explicit_cam" ] || camera=
    [ -n "$explicit_ctl" ] || modern=
fi

[ -n "$KEEN_DATA" ] && [ -z "$data_opt" ] && data_opt=$KEEN_DATA
KEEN_DATA=

if [ -z "$ep" ]; then
    usage >&2
    echo >&2
    echo "keen456: no episode given, starting Keen 4" >&2
    ep=4
fi

# ---- game data + engine tables for the chosen episode
ddir=$(find_data "$ep") || {
    echo "keen456: no game files for Keen $ep found." >&2
    echo "  Need EGAGRAPH.CK$ep, GAMEMAPS.CK$ep and AUDIO.CK$ep (v1.4 or v1.5 from your copy)." >&2
    echo "  Use:  keen456 $ep --data DIR   (DIR holds keen$ep/ or the files)" >&2
    echo "  or put them in ${XDG_DATA_HOME:-$HOME/.local/share}/keen456/data/keen$ep/" >&2
    exit 1
}
tabs=$ROOT/tables/k$ep
if [ "$ep" = 6 ]; then
    eg=$(ci_file "$ddir" EGAGRAPH.CK6)
    sz=$(wc -c < "$eg")
    if [ "$sz" = 464662 ]; then tabs=$ROOT/tables/k6e15; else tabs=$ROOT/tables/k6e14; fi
fi
run=$CACHE/run/keen$ep
mkdir -p "$run" || die "cannot create $run"
rm -f "$run"/* 2>/dev/null
for f in "$tabs"/*; do ln -sf "$f" "$run/${f##*/}"; done
for n in EGAGRAPH GAMEMAPS AUDIO; do ln -sf "$(ci_file "$ddir" "$n.CK$ep")" "$run/$n.CK$ep"; done

save=${XDG_DATA_HOME:-$HOME/.local/share}/keen456/keen$ep
mkdir -p "$save" || die "cannot create $save"

# ---- translate friendly options into engine args (raw args go last so they win)
G=
if [ "$smooth" = off ]; then G="$G /NOINTERP"; else G="$G /INTERP $smooth"; fi
if [ -n "$vanilla" ]; then G="/PROFILE vanilla$G"; else G="/PROFILE enhanced$G"; fi
[ -n "$camera" ] && [ "$smooth" != off ] && G="$G /FIX camera-smoothing"
if [ -n "$modern" ] && [ -z "$vanilla" ]; then G="$G /MODERNCONTROLS /POGOHOLD 180"; else G="$G /NOMODERNCONTROLS"; fi
if [ -n "$wide" ]; then G="$G /ASPECT $wide /FIX widescreen"; else G="$G /ASPECT auto"; fi
[ -n "$filter" ] && G="$G /FILTER $filter"
[ -n "$pixels" ] && G="$G /PIXELS $pixels"
[ -n "$xray" ] && G="$G /XRAY"
[ -n "$zoom" ] && G="$G /ZOOM $zoom"
[ -n "$deadzone" ] && G="$G /CAMERA $deadzone"
[ -n "$fs" ] && G="$G /FULLSCREEN"
[ -n "$border" ] || G="$G /NOBORDER"
set -f
# shellcheck disable=SC2086
set -- /EPISODE "$ep" /USERPATH "$save" $G "$@"
set +f

cd "$run" || die "cannot enter $run"
LD_LIBRARY_PATH=$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export LD_LIBRARY_PATH
[ -n "$KEEN456_DEBUG" ] && echo "keen456: exec omnispeak $*" >&2
miss=$(ldd "$ROOT/bin/omnispeak" 2>/dev/null | awk '/not found/{printf "%s ", $1}')
[ -n "$miss" ] && echo "keen456: missing system libraries: $miss(install your distro's SDL2 runtime deps: alsa, pulse/pipewire, X11 or wayland)" >&2
exec "$ROOT/bin/omnispeak" "$@"
exit 1
__PAYLOAD_BELOW__
