#!/bin/bash
# invert.sh <build-dir> <label> <file> <mutation.py> [run-command...]
# mutate -> rebuild -> run -> record -> restore -> rebuild.
set -uo pipefail
build_dir="$1"; label="$2"; file="$3"; mutation="$4"; shift 4
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
copy="${build_dir}/inversion-baseline/$(echo "$file" | tr '/' '_')"

if [ ! -f "$copy" ]; then
  echo "${label}: ABORT -- no baseline for ${file}. Run snapshot.sh first."; exit 3
fi
# GUARD 2: a stale baseline would silently revert finished work on restore.
if ! cmp -s "$copy" "${root}/${file}"; then
  echo "${label}: ABORT -- the baseline for ${file} does not match the tree."
  echo "  Restoring from it would revert finished work. Run snapshot.sh first."; exit 3
fi

# GUARD 3: a restore that did not produce a byte-identical file must be LOUD and
# must exit non-zero. This used to read `cmp -s ... && echo`, with no `set -e` and
# `rebuild.sh` running last -- so a FAILED restore printed nothing, returned
# rebuild.sh's status, and was indistinguishable from a successful one. That is
# worse than the two call-site bugs fixed at 23b326e: those made a restore skip
# recompilation, while this poisons the NEXT inversion, which then runs against a
# tree everyone believes is pristine and attributes its failure to innocent code.
#
# The `cmp` names ABSOLUTE paths on both sides. A relative path follows whatever
# `cd` ran earlier in the same shell, and a cmp of a file against itself always
# passes -- which is how four assertions stayed neutered through two of MAR-188's
# inversions.
restore() {
  local target="${root}/${file}"
  cp "$copy" "$target"
  if ! cmp -s "$copy" "$target"; then
    echo "=== ${label}: RESTORE FAILED -- $(cd "$(dirname "$target")" && pwd)/$(basename "$target") does not match $copy ==="
    echo "  The tree is NOT pristine. Every later measurement is void until this is"
    echo "  repaired by hand; do not run another inversion."
    cmp "$copy" "$target"
    return 4
  fi
  echo "  restore verified by cmp $copy $(cd "$(dirname "$target")" && pwd)/$(basename "$target")"
  "$(dirname "${BASH_SOURCE[0]}")/rebuild.sh" "$build_dir" >/dev/null 2>&1
  return 0
}

python3 "$mutation" "${root}/${file}" || { echo "${label}: MUTATION SCRIPT FAILED"; exit 1; }

# GUARD 1: without this the run below exercises the PREVIOUS binary and passes,
# and the result is recorded as "did not bite" -- a false pass from the harness.
if ! "$(dirname "${BASH_SOURCE[0]}")/rebuild.sh" "$build_dir" > "${build_dir}/inversion-build.log" 2>&1; then
  echo "=== ${label}: BUILD FAILED -- the mutation does not compile. NOT a result. ==="
  grep -E "error:" "${build_dir}/inversion-build.log" | head -5
  restore || exit 4
  exit 2
fi

"$@" > "${build_dir}/inversion-run.log" 2>&1
code=$?
echo "=== ${label}  exit=${code} ==="
if [ "$code" -eq 0 ]; then echo "  *** DID NOT BITE -- the suite still passed ***"; fi
restore || exit 4
exit 0
