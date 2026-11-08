#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Deploy a WASM dist tarball (tools/build-wasm.sh -> dist/, tarred) to keen456x.pro.dnsif.ca.
#   tools/deploy-keen456x.sh <dist.tar.gz>
# The nginx:alpine container 'keen456x' (127.0.0.1:5039) bind-mounts ~/keen456x/site read-only.
# Copy INTO that directory (rsync --delete); never replace the directory itself, or the
# container keeps serving the old, deleted inode. The site contains game data: it is only
# served from pro, never baked into an image.
# Also mirrors to keen456x.box.dnsif.ca: static nginx vhost on box, root /srv/keen456x, reached over
# WireGuard (BOX_HOST, default 10.77.0.1). BOX_HOST= (empty) skips it.
set -euo pipefail
tgz=${1:?usage: $0 dist.tar.gz}
site=$HOME/keen456x/site
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
tar xzf "$tgz" -C "$tmp"
for f in index.html omnispeak.js omnispeak.wasm; do [ -f "$tmp/dist/$f" ] || { echo "dist incomplete: no $f" >&2; exit 1; }; done
ls "$tmp"/dist/k*.data >/dev/null 2>&1 || { echo "dist has no episode .data" >&2; exit 1; }
mkdir -p "$site"
rsync -a --delete "$tmp/dist/" "$site/"
docker restart keen456x >/dev/null
sleep 2
for f in index.html omnispeak.wasm $(cd "$site" && ls k*.data); do
  printf '%s ' "$f"; curl -s -o /dev/null -w '%{http_code}\n' "https://keen456x.pro.dnsif.ca/$f"
done

box=${BOX_HOST-10.77.0.1}
if [ -n "$box" ]; then
  rsync -a --delete "$site/" "$box:/srv/keen456x/"
  for f in index.html omnispeak.wasm $(cd "$site" && ls k*.data); do
    printf 'box %s ' "$f"; curl -s -o /dev/null -w '%{http_code}\n' "https://keen456x.box.dnsif.ca/$f"
  done
fi
