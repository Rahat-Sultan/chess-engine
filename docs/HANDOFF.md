# Integration & Handoff Guide (Phase 8)

This document is addressed to the application backend teammate. It
covers everything needed to integrate the chess engine service into your
application: how to start it, how to call it, what the responses look
like, how errors behave, and how to shut it down cleanly.

---

## 1. What the engine does

The engine accepts a chess position (as a FEN string) and optional
search limits, then returns the best move and evaluation information.
It does **not** own game state, user accounts, move history, or
validation of moves from your application. You pass it a position and
it gives you an AI move recommendation.

Architecture:

```
Your backend  →  POST /analyze  →  FastAPI adapter  →  UCI pipes  →  C++ engine
                 (JSON)                               (subprocess)
```

---

## 2. Prerequisites

On the deployment machine:

```bash
# 1. Build the C++ engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Executable: ./build/chess_engine

# 2. Set up the Python environment
python3 -m venv .venv
source .venv/bin/activate
pip install -r api/requirements.txt

# 3. Download the NNUE network (optional — engine falls back to built-in eval)
bash scripts/download_network.sh
```

---

## 3. Starting the service

```bash
source .venv/bin/activate
uvicorn api.app:app --host 0.0.0.0 --port 8000
```

On startup the adapter:
1. Spawns `./build/chess_engine` as a subprocess.
2. Completes the UCI handshake (`uci` → `uciok`, `isready` → `readyok`).
3. Begins serving HTTP requests.

The subprocess path defaults to `./build/chess_engine`. Override via
environment variable:

```bash
CHESS_ENGINE_PATH=/opt/chess_engine/chess_engine uvicorn api.app:app --port 8000
```

Expected startup log (stderr):

```
INFO:     Started server process [12345]
INFO:     Waiting for application startup.
INFO:     Starting chess engine: ./build/chess_engine
INFO:     Engine ready (handshake complete)
INFO:     Engine ready, serving requests
INFO:     Application startup complete.
INFO:     Uvicorn running on http://0.0.0.0:8000 (Press CTRL+C to quit)
```

---

## 4. Health check

Before sending analysis requests, verify the engine is ready:

```bash
curl http://localhost:8000/health
```

Expected response:

```json
{
  "status": "ok",
  "engine": "lightweight-chess-engine",
  "ready": true
}
```

If `ready` is `false` or the request fails, the engine process has not
started or has crashed. Check stderr logs.

---

## 5. Analyzing a position

### Request

```
POST /analyze
Content-Type: application/json
```

```json
{
  "fen": "<six-field FEN string>",
  "limits": {
    "depth": 6
  }
}
```

### Limits (all optional — defaults to depth 6 if none given)

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `depth` | int | 1–20 | Search depth in plies |
| `movetime` | int | 10–30000 | Time limit in milliseconds |
| `nodes` | int | 1–10000000 | Node count limit |

Only specify one limit type at a time.

### Successful response (HTTP 200)

```json
{
  "success": true,
  "bestmove": "d2d4",
  "ponder": "g8f6",
  "score": {
    "type": "cp",
    "value": 59
  },
  "depth": 6,
  "nodes": 166963,
  "nps": 105140,
  "time_ms": 1588,
  "pv": ["d2d4", "g8f6", "c2c4", "e7e6"],
  "timed_out": false
}
```

| Field | Description |
|-------|-------------|
| `bestmove` | Move to play in UCI notation (`"e2e4"`), or `null` for checkmate/stalemate |
| `ponder` | Opponent's expected reply, if available |
| `score.type` | `"cp"` (centipawns) or `"mate"` (plies to mate) |
| `score.value` | Centipawn value (positive = good for side to move), or mate distance |
| `depth` | Depth reached |
| `pv` | Principal variation — the expected continuation |
| `timed_out` | `true` if the 30-second server safety timeout triggered; result is still valid |

---

## 6. Full curl examples

```bash
# Starting position, depth 6
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
       "limits": {"depth": 6}}' | python3 -m json.tool

# After 1.e4 e5, time-limited (1 second)
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2",
       "limits": {"movetime": 1000}}' | python3 -m json.tool

# Kiwipete benchmark position
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
       "limits": {"depth": 5}}' | python3 -m json.tool

# Scholar mate position — White to move, mate in one
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
       "limits": {"depth": 3}}' | python3 -m json.tool
```

---

## 7. Error behavior

### 7.1 Invalid FEN → HTTP 400

```bash
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "not_a_fen", "limits": {"depth": 1}}'
```

```json
{
  "detail": {
    "success": false,
    "error": "Invalid FEN",
    "detail": "expected 8 rows in position part of fen: 'not_a_fen'"
  }
}
```

### 7.2 Out-of-range limits → HTTP 422

```bash
# depth 100 exceeds maximum of 20
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
       "limits": {"depth": 100}}'
```

```json
{
  "detail": [
    {
      "type": "less_than_equal",
      "loc": ["body", "limits", "depth"],
      "msg": "Input should be less than or equal to 20",
      ...
    }
  ]
}
```

### 7.3 Engine busy → HTTP 409

If you send two concurrent requests, the second returns immediately:

```json
{
  "detail": {
    "success": false,
    "error": "Engine busy",
    "detail": "Another search is in progress. Try again later."
  }
}
```

**Policy:** the adapter allows one concurrent search per engine process.
If you need higher throughput, retry with backoff or wait for the first
request to complete.

### 7.4 Terminal position (checkmate / stalemate) → HTTP 200, bestmove null

```bash
# Black is checkmated — Scholar's mate delivered
curl -s -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4",
       "limits": {"depth": 1}}'
```

```json
{
  "success": true,
  "bestmove": null,
  ...
}
```

`bestmove: null` means the side to move has no legal moves. Your backend
must treat this as a game-over signal — do not attempt to apply a null
move.

### 7.5 Engine unavailable → HTTP 503

Returned if the engine subprocess has crashed or has not started.
Check server logs and restart the service.

### 7.6 Server-side safety timeout → HTTP 504

If a search takes longer than 30 seconds (only possible at very deep
depths or with extremely complex positions), the adapter sends `stop` to
the engine and returns the best move found so far. If no move was found
before the timeout, HTTP 504 is returned.

---

## 8. How to use the NNUE evaluator

The engine ships with both a handcrafted evaluator (default) and a
neural network evaluator (HalfKP 256x2-32-32). To enable NNUE, the
UCI `EvalFile` option must be set before the first search. This is
currently done at the UCI level — the HTTP adapter uses the engine's
default unless you extend it to set this option on startup.

To enable NNUE globally, add this to the adapter startup sequence in
`api/engine_manager.py` after `isready`:

```python
self._send("setoption name EvalFile value networks/nn-04cf2b4ed1da.nnue")
self._send("isready")
self._wait_for("readyok", timeout=timeout)
```

Or download and place the network file and pass the path via an
environment variable if you add that option.

The download script:

```bash
bash scripts/download_network.sh
```

---

## 9. Shutdown

The service shuts down cleanly on `CTRL+C` or `SIGTERM`. The FastAPI
lifespan handler sends `quit` to the engine subprocess and waits up to
3 seconds for it to exit before killing it.

```bash
# Graceful shutdown
kill -SIGTERM <uvicorn_pid>

# Or just CTRL+C in the terminal
```

Expected shutdown log:

```
INFO:     Shutting down
INFO:     Shutting down engine
INFO:     Engine exited cleanly
INFO:     Finished server process [12345]
```

---

## 10. Configuration reference

| Parameter | How to set | Default | Notes |
|-----------|-----------|---------|-------|
| Engine path | `CHESS_ENGINE_PATH` env var | `./build/chess_engine` | Must be an absolute path in production |
| HTTP host | `--host` uvicorn flag | `127.0.0.1` | Use `0.0.0.0` to bind all interfaces |
| HTTP port | `--port` uvicorn flag | `8000` | |
| Hash table | UCI `setoption name Hash value N` | 16 MB | Set in engine_manager.py if needed |
| NNUE network | UCI `setoption name EvalFile value <path>` | disabled | See §8 |
| Max search depth | API limit `depth` field | 6 (default), max 20 | Enforced by API |
| Max movetime | API limit `movetime` field | — | Max 30000 ms |
| Server safety timeout | `DEFAULT_TIMEOUT_SEC` in `api/app.py` | 30 s | Hard server-side cap |

---

## 11. Logs

- Engine diagnostics go to **stderr only** (never stdout — that would
  corrupt the UCI protocol).
- The HTTP adapter logs to Python's `logging` module at INFO level.
- To increase log verbosity: set `LOG_LEVEL=debug` or pass
  `--log-level debug` to uvicorn.

---

## 12. Running the test suite

```bash
# C++ engine tests
ctest --test-dir build --output-on-failure

# UCI smoke test
python3 scripts/uci_smoke_test.py ./build/chess_engine

# API tests
source .venv/bin/activate
pytest api/test_api.py -v

# Performance benchmark
python3 scripts/benchmark.py --depth 6 --hash 16
```

---

## 13. Quick integration checklist

- [ ] C++ engine builds cleanly (`cmake --build build`)
- [ ] `GET /health` returns `{"ready": true}`
- [ ] `POST /analyze` with starting FEN returns a legal UCI move
- [ ] Invalid FEN returns HTTP 400
- [ ] Out-of-range depth returns HTTP 422
- [ ] Checkmate position returns `bestmove: null` (not an error)
- [ ] `CTRL+C` shuts down cleanly (exit code 0)
