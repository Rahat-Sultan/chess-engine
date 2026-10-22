# HTTP API Documentation (Phase 6)

The chess engine HTTP adapter exposes a small JSON API for position
analysis. It wraps the standalone UCI engine in a Python FastAPI service
so the application backend can request analysis without speaking UCI
directly.

Architecture:

```
Backend → POST /analyze → FastAPI adapter → UCI pipes → chess_engine (C++)
```

---

## 1. Setup

### Prerequisites

- The C++ engine must be compiled first:
  ```bash
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```

- Create a Python virtual environment and install dependencies:
  ```bash
  python3 -m venv .venv
  source .venv/bin/activate
  pip install -r api/requirements.txt
  ```

### Starting the server

```bash
source .venv/bin/activate
uvicorn api.app:app --host 0.0.0.0 --port 8000
```

The server starts the engine subprocess automatically and shuts it down
on exit. The default engine path is `./build/chess_engine`. Override with
the `CHESS_ENGINE_PATH` environment variable:

```bash
CHESS_ENGINE_PATH=/path/to/chess_engine uvicorn api.app:app --port 8000
```

### Interactive docs

Once the server is running, open `http://localhost:8000/docs` in a
browser for the Swagger UI, or `http://localhost:8000/redoc` for ReDoc.

---

## 2. Endpoints

### GET /health

Check whether the engine is running and ready.

**Response 200:**

```json
{
  "status": "ok",
  "engine": "lightweight-chess-engine",
  "ready": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `"ok"` if healthy, `"degraded"` otherwise |
| `engine` | string | Engine identifier |
| `ready` | bool | True if the engine can accept searches |

**curl:**

```bash
curl http://localhost:8000/health
```

---

### POST /analyze

Analyze a chess position and return the best move with search info.

#### Request body

```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "limits": {
    "depth": 10
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fen` | string | yes | Six-field FEN string |
| `limits` | object | no | Search limits (see below) |

**Limits object:**

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `depth` | int | 1–20 | Maximum search depth in plies |
| `movetime` | int | 10–30000 | Time limit in milliseconds |
| `nodes` | int | 1–10000000 | Node count limit |

If no limits are specified, the engine defaults to depth 6. Only one
limit type should be specified; if multiple are given, depth takes
priority, then movetime, then nodes.

#### Response 200 (success)

```json
{
  "success": true,
  "bestmove": "d2d4",
  "ponder": "g8f6",
  "score": {
    "type": "cp",
    "value": 90
  },
  "depth": 6,
  "nodes": 166963,
  "nps": 105140,
  "time_ms": 1588,
  "pv": ["d2d4", "g8f6", "c2c4", "e7e6"],
  "timed_out": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Always `true` for 200 responses |
| `bestmove` | string\|null | Best move in UCI notation (e.g. `"e2e4"`), or `null` for terminal positions |
| `ponder` | string\|null | Ponder move, if available |
| `score` | object\|null | Evaluation: `{"type": "cp", "value": N}` or `{"type": "mate", "value": N}` |
| `depth` | int | Search depth reached (plies) |
| `nodes` | int | Nodes searched |
| `nps` | int | Nodes per second |
| `time_ms` | int | Search time in milliseconds |
| `pv` | string[] | Principal variation in UCI notation |
| `timed_out` | bool | `true` if the server-side timeout triggered (result is still valid) |

**Score types:**
- `cp`: Centipawn score from the side-to-move's perspective. Positive = good for the side to move.
- `mate`: Mate in N plies. Positive = the side to move delivers mate. Negative = the side to move is getting mated.

**curl:**

```bash
# Analyze the starting position at depth 6
curl -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "limits": {"depth": 6}}'

# Time-limited analysis (1 second)
curl -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "limits": {"movetime": 1000}}'

# Analyze a custom position (Kiwipete)
curl -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "limits": {"depth": 5}}'
```

---

## 3. Error Responses

All error responses have `success: false`.

### 400 — Invalid FEN

The FEN string could not be parsed or is not a legal position.

```json
{
  "success": false,
  "error": "Invalid FEN",
  "detail": "FEN doesn't have 6 fields, had 3"
}
```

### 422 — Validation Error

The request body failed Pydantic validation (missing `fen`, out-of-range
limits, malformed JSON). FastAPI returns a standard validation error
schema with details about each failed field.

```bash
# Example: depth too high
curl -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "...", "limits": {"depth": 100}}'
# → 422 with validation error details
```

### 409 — Engine Busy

Another search is in progress. The adapter allows one concurrent search.

```json
{
  "success": false,
  "error": "Engine busy",
  "detail": "Another search is in progress. Try again later."
}
```

### 503 — Engine Unavailable

The engine process is not running or crashed.

```json
{
  "success": false,
  "error": "Engine unavailable",
  "detail": "The engine process is not running or not ready."
}
```

### 504 — Analysis Timed Out

The server-side safety timeout (30 seconds) was triggered and the engine
did not return a best move.

```json
{
  "success": false,
  "error": "Analysis timed out",
  "detail": "The search exceeded the server-side timeout."
}
```

---

## 4. Safe Maximums

| Parameter | Max value | Rationale |
|-----------|-----------|-----------|
| Depth | 20 | Depth 8 takes ~60s from startpos on an i5-8350U |
| Movetime | 30000 ms | 30-second cap per request |
| Nodes | 10,000,000 | Bounded compute |
| Server timeout | 30 s | Safety net even if engine hangs |

These limits prevent unbounded CPU use. They are enforced at the API
layer (Pydantic validation) and as a server-side safety timeout.

---

## 5. Concurrency Policy

- One search at a time per engine process.
- If a search is in progress, concurrent requests receive `409 Engine Busy`.
- The engine subprocess is managed by the FastAPI lifespan; it starts
  when the server starts and stops cleanly on shutdown.
- A future version may implement a bounded process pool for higher
  throughput.

---

## 6. OpenAPI Schema

The full OpenAPI 3.1 schema is available at:

```
GET http://localhost:8000/openapi.json
```

Swagger UI: `http://localhost:8000/docs`
ReDoc: `http://localhost:8000/redoc`

---

## 7. Running Tests

```bash
source .venv/bin/activate
pytest api/test_api.py -v
```

Tests cover health checks, valid analysis, mate detection, error cases,
and OpenAPI schema availability.
