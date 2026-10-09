#!/usr/bin/env bash

# clang-tidy using the compile database from a meson build dir, so tidy
# sees exactly the real compile flags.
# Usage: scripts/clang-tidy.sh [build-dir]  (default: build/debug)
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
build="${1:-build/debug}"

command -v run-clang-tidy >/dev/null 2>&1 || {
    echo "lint: run-clang-tidy not found in PATH" >&2
    exit 1
}

[ -f "$build/compile_commands.json" ] || {
    echo "lint: $build/compile_commands.json not found - run 'make debug' first" >&2
    exit 1
}

exec run-clang-tidy \
    -quiet \
    -hide-progress \
    -config-file "$root/.clang-tidy" \
    -p "$build" \
    src
