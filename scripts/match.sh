#!/usr/bin/env bash
set -euo pipefail

# match.sh — Automated engine match harness using fastchess
#
# Usage:
#   scripts/match.sh <engine1> <engine2> <tc_or_limit> <games> [concurrency] [sprt] [seed]
#
# Examples:
#   scripts/match.sh engines/baseline_v2/chess_engine engines/2ffb3d9/chess_engine "nodes=20000" 200 2
#   scripts/match.sh engines/2ffb3d9/chess_engine engines/dev/chess_engine "5+0.05" 1000 2 sprt
#   scripts/match.sh engines/2ffb3d9/chess_engine engines/dev/chess_engine "nodes=20000" 300 2

FASTCHESS="${FASTCHESS:-/home/borat/.local/bin/fastchess}"
if [ ! -x "$FASTCHESS" ]; then
    FASTCHESS="$(which fastchess 2>/dev/null || true)"
fi

if [ -z "$FASTCHESS" ] || [ ! -x "$FASTCHESS" ]; then
    echo "Error: fastchess executable not found at /home/borat/.local/bin/fastchess or in PATH" >&2
    exit 1
fi

ENGINE1="${1:?Error: engine1 path required}"
ENGINE2="${2:?Error: engine2 path required}"
LIMIT="${3:-5+0.05}"
GAMES="${4:-100}"
CONCURRENCY="${5:-2}"
SPRT_MODE="${6:-false}"
SEED="${7:-${SEED:-20261023}}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENINGS="${OPENINGS:-$REPO_ROOT/bench/openings_v2.epd}"

if [ ! -f "$OPENINGS" ]; then
    echo "Error: opening book not found at $OPENINGS" >&2
    exit 1
fi

NAME1="$(basename "$(dirname "$ENGINE1")")"
if [ "$NAME1" = "." ] || [ "$NAME1" = "engines" ]; then
    NAME1="$(basename "$ENGINE1")"
fi

NAME2="$(basename "$(dirname "$ENGINE2")")"
if [ "$NAME2" = "." ] || [ "$NAME2" = "engines" ]; then
    NAME2="$(basename "$ENGINE2")"
fi

mkdir -p "$REPO_ROOT/bench/matches"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
PGN_OUT="$REPO_ROOT/bench/matches/${NAME1}_vs_${NAME2}_${TIMESTAMP}.pgn"

# Each round plays 2 paired games (colors swapped on the same opening)
ROUNDS=$(( GAMES / 2 ))
if [ "$ROUNDS" -lt 1 ]; then
    ROUNDS=1
fi

# Configure search limit (time control or fixed nodes)
EACH_LIMIT=()
if [[ "$LIMIT" =~ ^nodes=([0-9]+)$ ]]; then
    EACH_LIMIT+=("nodes=${BASH_REMATCH[1]}")
elif [[ "$LIMIT" =~ ^[0-9]+ ]]; then
    EACH_LIMIT+=("tc=$LIMIT")
else
    EACH_LIMIT+=("$LIMIT")
fi

# Base arguments
FASTCHESS_ARGS=(
    -engine "cmd=$ENGINE1" "name=$NAME1"
    -engine "cmd=$ENGINE2" "name=$NAME2"
    -each "option.Hash=16" "option.Threads=1" "${EACH_LIMIT[@]}"
    -openings "file=$OPENINGS" "format=epd" "order=random"
    -srand "$SEED"
    -rounds "$ROUNDS"
    -repeat
    -concurrency "$CONCURRENCY"
    -pgnout "file=$PGN_OUT" "notation=san"
)

# Adjudication: Explicitly disabled per testing policy (games play to completion)

if [ "$SPRT_MODE" = "sprt" ] || [ "$SPRT_MODE" = "true" ] || [ "${SPRT:-0}" = "1" ]; then
    FASTCHESS_ARGS+=(-sprt "elo0=0" "elo1=10" "alpha=0.05" "beta=0.05")
fi

echo "=================================================="
echo "Starting match: $NAME1 vs $NAME2"
echo "Limit: $LIMIT | Games: $GAMES (Rounds: $ROUNDS x 2) | Concurrency: $CONCURRENCY"
echo "Openings: $OPENINGS (order=random, seed=$SEED)"
echo "Engine options: option.Hash=16, option.Threads=1"
echo "Adjudication: None (draw and resign adjudication disabled)"
if [ "$SPRT_MODE" = "sprt" ] || [ "$SPRT_MODE" = "true" ] || [ "${SPRT:-0}" = "1" ]; then
    echo "SPRT: elo0=0 elo1=10 alpha=0.05 beta=0.05 (max games: $GAMES)"
fi
echo "PGN Output: $PGN_OUT"
echo "=================================================="

# Discard engines' stderr output (diagnostic banners) while preserving fastchess report on stdout
"$FASTCHESS" "${FASTCHESS_ARGS[@]}" 2>/dev/null

echo "PGN saved to $PGN_OUT"

# Comprehensive match analysis from PGN
if [ -f "$PGN_OUT" ]; then
    python3 "$SCRIPT_DIR/analyze_match.py" "$PGN_OUT" "$NAME1" "$NAME2"
fi
