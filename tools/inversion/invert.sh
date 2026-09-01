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

restore() {
  cp "$copy" "${root}/${file}"
  cmp -s "$copy" "${root}/${file}" && echo "  restore verified by cmp"
  "$(dirname "${BASH_SOURCE[0]}")/rebuild.sh" "$build_dir" >/dev/null 2>&1
}

python3 "$mutation" "${root}/${file}" || { echo "${label}: MUTATION SCRIPT FAILED"; exit 1; }

# GUARD 1: without this the run below exercises the PREVIOUS binary and passes,
# and the result is recorded as "did not bite" -- a false pass from the harness.
if ! "$(dirname "${BASH_SOURCE[0]}")/rebuild.sh" "$build_dir" > "${build_dir}/inversion-build.log" 2>&1; then
  echo "=== ${label}: BUILD FAILED -- the mutation does not compile. NOT a result. ==="
  grep -E "error:" "${build_dir}/inversion-build.log" | head -5
  restore; exit 2
fi

"$@" > "${build_dir}/inversion-run.log" 2>&1
code=$?
echo "=== ${label}  exit=${code} ==="
if [ "$code" -eq 0 ]; then echo "  *** DID NOT BITE -- the suite still passed ***"; fi
restore
exit 0
