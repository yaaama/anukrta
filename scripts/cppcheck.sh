#!/usr/bin/env bash

# cppcheck over src/ (vendor excluded).
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

# Resolution order: explicit argument > debug variant DB > root symlink
# (the `bear` target). Fail loud with the fix, never guess.
if [ $# -ge 1 ]; then
    comp_db=$1
elif [ -f build/debug/compile_commands.json ]; then
    comp_db=build/debug/compile_commands.json
elif [ -f compile_commands.json ]; then
    comp_db=compile_commands.json
else
    echo "cppcheck.sh: no compile database found" >&2
    echo "  run 'make debug' first, or pass one explicitly:" >&2
    echo "  ./scripts/cppcheck.sh build/release/compile_commands.json" >&2
    exit 1
fi

if [ ! -f "$comp_db" ]; then
    echo "cppcheck.sh: '$comp_db' is not a regular file" >&2
    exit 1
fi

command -v cppcheck >/dev/null 2>&1 || {
    echo "cppcheck: cppcheck not found in PATH" >&2; exit 1
}

        # --template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
exec cppcheck \
    -q \
    --enable=all \
    --disable=style,unusedFunction \
    --check-level=exhaustive \
    --inconclusive \
    --project="$comp_db" \
    --suppress=missingIncludeSystem \
    -D__GNUC__ \
    -i vendor
