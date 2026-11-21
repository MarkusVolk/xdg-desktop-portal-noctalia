#!/usr/bin/env bash
# Mirror noctalia-shell's lib/ into this repository's lib/; of third_party/ only wuffs/, which the codec pack needs.
# Usage: tools/sync-lib.sh [path/to/noctalia-shell]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
source="${1:-$root/../noctalia-shell}/lib"

[[ -f "$source/meson.build" ]] || { echo "no noctalia lib at $source" >&2; exit 1; }
if [[ -n "$(git -C "$root" status --porcelain -- lib)" ]]; then
  echo "lib/ has uncommitted changes; port them to noctalia-shell or discard them first" >&2
  exit 1
fi

rsync -a --delete --include /third_party/ --include "/third_party/wuffs/***" --exclude "/third_party/*" "$source/" "$root/lib/"
git -C "$root" status --short -- lib
