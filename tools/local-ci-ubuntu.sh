#!/usr/bin/env bash
# Ubuntu half of tools/local-ci.ps1 (also usable on its own on any Ubuntu 24.04 machine).
# Usage: local-ci-ubuntu.sh <log directory> [--snap]
# Environment: WORDFORGE_BUILD_DIR (default ~/wordforge-build-<checkout hash>), WORDFORGE_DEPS_DIR,
# PKG_CONFIG_PATH (defaults to the deps folder's pkgconfig when that exists).
set -uo pipefail
log_dir=${1:?log directory required}
snap=${2:-}
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# One build folder per checkout: CMake rejects a cache made from another source folder.
build_dir=${WORDFORGE_BUILD_DIR:-"$HOME/wordforge-build-$(printf %s "$source_dir" | sha256sum | cut -c1-8)"}
deps_dir=${WORDFORGE_DEPS_DIR:-}
if [[ -z "$deps_dir" ]]; then
  for candidate in /opt/wordforge-deps "$HOME/.local/share/wordforge-deps"; do
    [[ -d "$candidate/skia" ]] && deps_dir=$candidate && break
  done
fi
[[ -n "$deps_dir" && -d "$deps_dir/pkgconfig" ]] &&
  export PKG_CONFIG_PATH="$deps_dir/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/wordforge-runtime}
mkdir -p "$XDG_RUNTIME_DIR" "$log_dir" && chmod 700 "$XDG_RUNTIME_DIR"

status=0
echo "== Ubuntu: configure and build ($build_dir)"
if ! { cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release &&
       cmake --build "$build_dir" --parallel "$(nproc)"; } >"$log_dir/ubuntu-build.txt" 2>&1; then
  echo "BUILD FAILED; see ubuntu-build.txt"
  grep -E "error|FAILED" "$log_dir/ubuntu-build.txt" | head -20
  exit 1
fi
echo "== Ubuntu: tests"
ctest --test-dir "$build_dir" -V >"$log_dir/ubuntu-tests.txt" 2>&1 || status=1
grep -E "tests passed|FAIL!|Failed  " "$log_dir/ubuntu-tests.txt" | tail -5

if [[ "$snap" == "--snap" && $status -eq 0 ]]; then
  echo "== Ubuntu: snap package"
  if python3 "$source_dir/tools/package-snap.py" --build-dir "$build_dir" \
       --skia-dir "$deps_dir/skia" --output-dir "$build_dir/snap" >"$log_dir/ubuntu-snap.txt" 2>&1; then
    grep -E "^built:|SHA256" "$log_dir/ubuntu-snap.txt"
  else
    echo "SNAP FAILED; see ubuntu-snap.txt"
    status=1
  fi
fi
exit $status
