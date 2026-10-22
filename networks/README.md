# networks/

This directory holds NNUE network weight files for Phase 7.

**Status: Active.** Network `nn-04cf2b4ed1da.nnue` (HalfKP 256x2-32-32,
~21 MB) is approved and in use. Lead approval confirmed October 2026.

Network files (`.nnue`, `.bin`) are **not committed to Git** — they are
large binary assets. The `.gitignore` already excludes `networks/*.nnue`.

## Network in use

| Filename | Architecture | Size | Source |
|----------|-------------|------|--------|
| `nn-04cf2b4ed1da.nnue` | HalfKP 256x2-32-32 | ~21 MB | Stockfish networks repo |

Attribution: [official-stockfish/networks](https://github.com/official-stockfish/networks) — GPL-3.0

## How to obtain the network

```bash
bash scripts/download_network.sh
```

Or manually:

```bash
curl -L "https://tests.stockfishchess.org/api/nn/nn-04cf2b4ed1da.nnue" \
     -o networks/nn-04cf2b4ed1da.nnue
```

## How to use

```
setoption name EvalFile value networks/nn-04cf2b4ed1da.nnue
```

The engine falls back to the handcrafted evaluator if the file is missing.

See [`docs/NNUE_GUIDE.md`](../docs/NNUE_GUIDE.md) for full integration details.
