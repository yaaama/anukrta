#!/usr/bin/env bash

# clang-format all project sources in place.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

command -v clang-format >/dev/null 2>&1 || {
    echo "format: clang-format not found in PATH" >&2
    exit 1
}

# Headers aren't in the meson source lists, so still discover here
# but only our code, never vendor/.
find "${root}/src" \( -name '*.c' -o -name '*.h' -o -name '*.inl' \) -print0 |
    xargs -0 -r -P "$(nproc 2>/dev/null || echo 4)" clang-format -i --verbose
