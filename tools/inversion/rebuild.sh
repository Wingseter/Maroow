#!/bin/bash
# rebuild.sh <build-dir> [object-relative-path...]
# Delete objects, then build. Never `touch` (AGENTS.md H1).
#
# With NO object paths, deletes every object under <build-dir>/CMakeFiles. That is
# the case `invert.sh` uses, and before MAR-188 it deleted NOTHING: the `for` loop
# below ran over an empty `$@`, so the build fell back to make's mtime comparison
# -- the exact hazard this harness exists to prevent. Measured at the time of the
# fix: 130 objects before, 130 after, 0 files recompiled.
#
# The `/CMakeFiles` is load-bearing. `find <build-dir> -name '*.o' -delete` also
# deletes vendored SDL3's objects; `libSDL3.a` is then re-created incrementally at
# 96 bytes, which fails the link and produces a phantom two-test regression with
# the story's own sources provably untouched.
set -euo pipefail
build_dir="$1"; shift
if [ "$#" -eq 0 ]; then
  find "${build_dir}/CMakeFiles" -name '*.o' -delete
else
  for o in "$@"; do rm -f "${build_dir}/${o}"; done
fi
cmake --build "${build_dir}" -j8
