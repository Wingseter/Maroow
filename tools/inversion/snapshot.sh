#!/bin/bash
# snapshot.sh <build-dir> <file>...
# Refresh the pristine baseline. MUST run after every implementation step:
# restoring from a baseline captured earlier silently reverts finished work.
set -euo pipefail
build_dir="$1"; shift
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
store="${build_dir}/inversion-baseline"
mkdir -p "$store"
for f in "$@"; do
  cp "${root}/${f}" "${store}/$(echo "$f" | tr '/' '_')"
done
echo "inversion baseline refreshed for $# file(s) in ${store}"
