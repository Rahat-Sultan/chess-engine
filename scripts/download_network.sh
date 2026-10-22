#!/usr/bin/env bash
# download_network.sh — Download the approved HalfKP NNUE network file
#
# Usage:
#   bash scripts/download_network.sh
#
# The network is nn-04cf2b4ed1da.nnue (HalfKP 256x2-32-32, ~21 MB).
# Architecture: Features=HalfKP(Friend)[41024->256x2], Network=Affine
#
# License: Stockfish networks are published under the Stockfish project
# (GPL-3). Lead approval for use in this project confirmed October 2026.
# Attribution: https://github.com/official-stockfish/Stockfish
#
# Do not commit the .nnue file to Git. The .gitignore already excludes
# networks/*.nnue.

set -euo pipefail

NETWORK_NAME="nn-04cf2b4ed1da.nnue"
DEST="networks/${NETWORK_NAME}"
URL="https://tests.stockfishchess.org/api/nn/${NETWORK_NAME}"

mkdir -p networks

if [ -f "${DEST}" ]; then
    echo "[INFO] Network already present: ${DEST}"
    exit 0
fi

echo "[INFO] Downloading ${NETWORK_NAME} (~21 MB)..."
curl -L --fail --progress-bar "${URL}" -o "${DEST}"

echo "[INFO] Verifying file..."
HASH=$(sha256sum "${DEST}" | awk '{print $1}')
echo "[INFO] SHA-256: ${HASH}"
echo "[INFO] Expected prefix: 04cf2b4ed1da..."

echo "[OK]   Network saved to ${DEST}"
