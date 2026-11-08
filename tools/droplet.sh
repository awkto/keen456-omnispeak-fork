#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Throwaway DigitalOcean droplets for heavy harness work (golden traces, benches), so the
# dev host stays light. Every droplet is tagged claude-test and is destroyed when done.
#
#   tools/droplet.sh up <name> [size]   create (default s-4vcpu-8gb, syd1), wait for ssh, print IP
#   tools/droplet.sh ip <name>          print the public IP
#   tools/droplet.sh push <name>        rsync repo (oracle/ + engine sources) + $KEEN_DATA (+ ~/bcc) to /root on the droplet
#   tools/droplet.sh down <name>        destroy (only droplets tagged claude-test)
#   tools/droplet.sh list               list claude-test droplets
#
# Rules: names start with keen-; at most 4 keen-* droplets at a time (user OK 2026-11-06); never touch other droplets.
# Game data and Borland only ever go onto these temporary droplets and die with them.
set -euo pipefail
CTX=${DO_CONTEXT:-teamadf}; TAG=claude-test; REGION=${DO_REGION:-syd1}
SSHKEY=${DO_SSH_KEY:-46383777}            # "altan" key = ~/.ssh/id_ed25519
. "$(dirname "$0")/../oracle/tools/keendata.sh"; DATA=$KD
REPO=$(cd "$(dirname "$0")/.." && pwd)
do_() { doctl --context "$CTX" "$@"; }
ip_of() { do_ compute droplet list --tag-name "$TAG" --format Name,PublicIPv4 --no-header | awk -v n="$1" '$1==n{print $2}'; }
SSH=(ssh -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10)

case ${1:-} in
up)
  name=$2; size=${3:-s-4vcpu-8gb}
  case $name in keen-*) ;; *) echo "name must start with keen- (this project's droplets)" >&2; exit 1;; esac
  # Other projects' sessions share the claude-test tag; this project keeps to 4 of its own.
  n=$(do_ compute droplet list --tag-name "$TAG" --no-header --format Name | grep -c '^keen-' || true)
  [ "$n" -ge 4 ] && { echo "already $n keen-* droplets; limit is 4" >&2; exit 1; }
  do_ compute droplet create "$name" --tag-name "$TAG" --size "$size" --region "$REGION" \
     --image ubuntu-24-04-x64 --ssh-keys "$SSHKEY" --wait --format Name,PublicIPv4 --no-header >/dev/null
  ip=""; for _ in $(seq 30); do ip=$(ip_of "$name"); [ -n "$ip" ] && break; sleep 5; done
  for _ in $(seq 60); do "${SSH[@]}" root@"$ip" true 2>/dev/null && break; sleep 5; done
  "${SSH[@]}" root@"$ip" 'cloud-init status --wait >/dev/null 2>&1; export DEBIAN_FRONTEND=noninteractive;
     apt-get -qq update && apt-get -qq install -y dosbox xvfb python3 rsync make zstd >/dev/null'
  echo "$ip" ;;
ip) ip_of "$2" ;;
push)
  ip=$(ip_of "$2"); R=(rsync -azL --delete -e "${SSH[*]}")
  "${R[@]}" --exclude .git --exclude "bin*" --exclude "obj*" --exclude "dist" "$REPO/" root@"$ip":/root/keen456-omnispeak-fork/
  "${R[@]}" "$DATA/" root@"$ip":/root/keen-data-decomp/
  [ -d "$HOME/bcc/BC30" ] && "${R[@]}" --exclude '*.7z' --exclude '*.ZIP' "$HOME/bcc/" root@"$ip":/root/bcc/
  echo pushed ;;
down)
  case $2 in keen-*) ;; *) echo "refusing: not a keen-* droplet" >&2; exit 1;; esac
  id=$(do_ compute droplet list --tag-name "$TAG" --format ID,Name --no-header | awk -v n="$2" '$2==n{print $1}')
  [ -n "$id" ] || { echo "no $TAG droplet named $2" >&2; exit 1; }
  do_ compute droplet delete -f "$id"; echo "destroyed $2" ;;
list) do_ compute droplet list --tag-name "$TAG" --format ID,Name,PublicIPv4,Memory,VCPUs --no-header | awk '$2 ~ /^keen-/' ;;
*) sed -n '3,13p' "$0"; exit 2 ;;
esac
