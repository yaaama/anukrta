#!/usr/bin/env bash

# valgrind memcheck on the built binary.
# Usage: scripts/valgrind.sh [build-dir] [program args...]
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

if [ "$#" -gt 0 ]; then
    build=$1
    shift
else
    build=build/debug
fi
bin="$build/anukrta"

command -v valgrind >/dev/null 2>&1 || {
    echo "memcheck: valgrind not found in PATH" >&2
    exit 1
}
[ -x "$bin" ] || {
    echo "memcheck: $bin not found - build first (make debug)" >&2
    exit 1
}

exec valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    "$bin" "$@"
