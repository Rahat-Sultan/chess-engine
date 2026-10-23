#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXE="$REPO_ROOT/build/generate_openings"
SRC="$SCRIPT_DIR/generate_openings.cpp"
LIB="$REPO_ROOT/build/libchess_core.a"

if [ ! -f "$LIB" ]; then
    echo "Building chess_core static library..."
    ninja -C "$REPO_ROOT/build" chess_core
fi

if [ ! -f "$EXE" ] || [ "$SRC" -nt "$EXE" ] || [ "$LIB" -nt "$EXE" ]; then
    echo "Compiling generate_openings..."
    g++ -std=c++17 -O3 -I "$REPO_ROOT/include" -I "$REPO_ROOT/src" \
        "$SRC" "$LIB" -lpthread -o "$EXE"
fi

"$EXE" "$@"

