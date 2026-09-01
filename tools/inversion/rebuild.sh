#!/bin/bash
# rebuild.sh <build-dir> <object-relative-path>...
# Delete the named objects, then build. Never `touch` (AGENTS.md H1).
set -euo pipefail
build_dir="$1"; shift
for o in "$@"; do rm -f "${build_dir}/${o}"; done
cmake --build "${build_dir}" -j8
