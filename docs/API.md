# Chess Engine HTTP API Specification & Integration Guide

This document defines the **real, observed HTTP API contract** for the chess engine service. It documents actual runtime behavior, captured real JSON payloads, exact error response structures, engine interaction mechanics, and integration requirements for client applications.

---

## 1. Architecture & Protocol Overview

The service acts as an asynchronous HTTP/JSON adapter sitting in front of a compiled C++ UCI chess engine (`build/chess_engine`):

```
Client Application (Web / Desktop / Bot)
       │
       │ HTTP / JSON (REST)
       ▼
 FastAPI Server (`api/app.py` on Uvicorn ASGI)
       │
       │ Threadpool worker offload (`run_in_threadpool`)
       ▼
 Engine Manager (`api/engine_manager.py`)
       │
       │ UCI Protocol (`ucinewgame`, `position fen ...`, `go ...`)
       ▼
 C++ Chess Engine Process (`build/chess_engine`)
```

- **Execution Model**: The ASGI event loop remains completely unblocked. Synchronous engine operations run in background worker threads via `run_in_threadpool`.
- **Concurrency**: Exactly one search runs at any given time (`EngineManager` non-blocking lock). Concurrent requests receive `409 Conflict` immediately (<5ms).
- **Non-blocking Health**: `GET /health` executes on the event loop and answers immediately (<2ms) even while a 30-second search is in flight.
- **Auto-Recovery**: If the engine process crashes or is terminated, `EngineManager` automatically restarts it on the next analysis request (throttled to at most 3 restarts within 60 seconds; returns `503` if threshold is exceeded).
- **Stale Move Guard**: If an engine fails to respond to a search `stop` within a 2.0-second grace period, the process is forcefully killed and flushed so stale `bestmove` lines cannot leak into subsequent requests.
- **TT Pollution Guard**: Before each search, the adapter issues `ucinewgame` and awaits `isready` / `readyok` to prevent cross-request transposition table pollution.

---

## 2. Authentication

Authentication is configured via the server environment variable `API_KEY`:

| `API_KEY` Environment Variable | Access Mode | Behavior |
|:---|:---|:---|
| **Not Set / Empty** | **Open Access** | All endpoints are public; no authentication headers are inspected. |
| **Set** (e.g. `API_KEY=secret-token`) | **Key Protected** | Every route **except `GET /health`** requires the `X-API-Key` header matching the secret. |

### Header Specification
- **Header Name**: `X-API-Key` (case-insensitive in HTTP/1.1; e.g. `x-api-key` is accepted).
- **Comparison**: Evaluated in constant time using `secrets.compare_digest` with UTF-8 byte encoding to prevent timing attacks.
- **Non-ASCII Support**: Non-ASCII header values are safely encoded and rejected with `401 Unauthorized` without raising server 500 exceptions.
- **Unauthenticated Failure**: Returns HTTP `401 Unauthorized` with the uniform top-level error format.

---

## 3. Endpoints

### 3.1. GET /health

Returns operational status and readiness of the underlying C++ engine process.

- **Authentication**: **Always exempt**. Accessible whether `API_KEY` is configured or not.
- **Method**: `GET`
- **Path**: `/health` (also handles `/health/` via redirect)
- **Response Codes**: `200 OK`

#### Captured Response Body (200 OK - Healthy)
```json
{
  "status": "ok",
  "engine": "lightweight-chess-engine",
  "ready": true
}
```

#### Captured Response Body (200 OK - Engine Down / Crashed)
```json
{
  "status": "degraded",
  "engine": "lightweight-chess-engine",
  "ready": false
}
```

| Field | Type | Description |
|:---|:---|:---|
| `status` | `string` | `"ok"` if engine is running and ready; `"degraded"` if process is terminated or unready. |
| `engine` | `string` | Engine identifier (`"lightweight-chess-engine"`). |
| `ready` | `boolean` | `true` if engine process is alive and handshake is complete; `false` otherwise. |

#### Sample `curl`:
```bash
curl -sS http://localhost:8000/health
```

---

### 3.2. POST /analyze

Analyzes a chess position specified by FEN and returns the engine's best move, ponder move, evaluation score, and search statistics.

- **Authentication**: Requires `X-API-Key` if `API_KEY` is set on server.
- **Method**: `POST`
- **Path**: `/analyze`
- **Headers**:
  - `Content-Type: application/json`
  - `X-API-Key: <your-key>` (conditional)
- **Response Codes**: `200 OK`, `400 Bad Request`, `401 Unauthorized`, `404 Not Found`, `405 Method Not Allowed`, `409 Conflict`, `422 Unprocessable Entity`, `503 Service Unavailable`, `504 Gateway Timeout`, `500 Internal Server Error`.

#### Request Body Schema
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "limits": {
    "depth": 6,
    "movetime": 1000,
    "nodes": 50000
  }
}
```

| Field | Type | Required | Default | Validation Range | Description |
|:---|:---|:---:|:---:|:---:|:---|
| `fen` | `string` | **Yes** | — | Valid FEN tokens | Chess position in Forsyth–Edwards Notation. |
| `limits` | `object` | No | `{ "depth": 6 }` | — | Search constraint limits. If omitted or empty, defaults to `depth=6`. |
| `limits.depth` | `integer` | No | `null` | `1 <= depth <= 20` | Maximum search depth in plies. |
| `limits.movetime` | `integer` | No | `null` | `10 <= movetime <= 30000` | Search time limit in milliseconds. |
| `limits.nodes` | `integer` | No | `null` | `1 <= nodes <= 10000000` | Maximum node budget. |

> [!NOTE]
> Extra unrecognized fields in the request body are strictly forbidden and rejected with `422 Unprocessable Entity`.

---

#### Real Captured Responses (POST /analyze)

##### 1. Start Position at Depth 4 (Captured from Engine)
```json
{
  "success": true,
  "bestmove": "b1c3",
  "ponder": "b8c6",
  "score": {
    "type": "cp",
    "value": 86
  },
  "depth": 4,
  "nodes": 8412,
  "nps": 560800,
  "time_ms": 15,
  "pv": [
    "b1c3",
    "b8c6",
    "e2e4",
    "g8f6"
  ],
  "timed_out": false
}
```

##### 2. Start Position at Depth 6 (Captured from Engine)
```json
{
  "success": true,
  "bestmove": "b1c3",
  "ponder": "b8c6",
  "score": {
    "type": "cp",
    "value": 90
  },
  "depth": 6,
  "nodes": 166963,
  "nps": 481161,
  "time_ms": 347,
  "pv": [
    "b1c3",
    "b8c6",
    "d2d4",
    "d7d5",
    "g1f3",
    "g8f6"
  ],
  "timed_out": false
}
```

##### 3. Start Position at Depth 8 (Captured from Engine)
```json
{
  "success": true,
  "bestmove": "d2d4",
  "ponder": "g8f6",
  "score": {
    "type": "cp",
    "value": 111
  },
  "depth": 8,
  "nodes": 6641264,
  "nps": 466807,
  "time_ms": 14227,
  "pv": [
    "d2d4",
    "g8f6",
    "b1c3",
    "d7d5",
    "g1f3",
    "b8c6",
    "c1f4",
    "e7e6"
  ],
  "timed_out": false
}
```

##### 4. Tactical Position (Kiwipete at Depth 4)
```json
{
  "success": true,
  "bestmove": "e2a6",
  "ponder": "b4c3",
  "score": {
    "type": "cp",
    "value": 231
  },
  "depth": 4,
  "nodes": 29044,
  "nps": 349927,
  "time_ms": 83,
  "pv": [
    "e2a6",
    "b4c3",
    "f3c3",
    "h3g2"
  ],
  "timed_out": false
}
```

##### 5. Mate in One Position (`6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1`)
```json
{
  "success": true,
  "bestmove": "a1a8",
  "ponder": null,
  "score": {
    "type": "mate",
    "value": 1
  },
  "depth": 2,
  "nodes": 94,
  "nps": 0,
  "time_ms": 0,
  "pv": [
    "a1a8"
  ],
  "timed_out": false
}
```

##### 6. Terminal Checkmate Position (`rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3`)
```json
{
  "success": true,
  "bestmove": null,
  "ponder": null,
  "score": null,
  "depth": 0,
  "nodes": 0,
  "nps": 0,
  "time_ms": 0,
  "pv": [],
  "timed_out": false
}
```

##### 7. Terminal Stalemate Position (`7k/5Q2/6K1/8/8/8/8/8 b - - 0 1`)
```json
{
  "success": true,
  "bestmove": null,
  "ponder": null,
  "score": null,
  "depth": 0,
  "nodes": 0,
  "nps": 0,
  "time_ms": 0,
  "pv": [],
  "timed_out": false
}
```

---

## 4. FEN Validation Policy

The API validates incoming FEN strings against both syntax and semantic chess rules using python-chess status flags:

### 4.1. Rejected FEN Errors (HTTP 400 Bad Request)
The following hard errors are strictly rejected with HTTP 400, naming the exact failure in the `detail` message:
- Empty board (`STATUS_EMPTY`)
- Missing king (`STATUS_NO_WHITE_KING`, `STATUS_NO_BLACK_KING`)
- Extra kings (`STATUS_TOO_MANY_KINGS`)
- Excessive pieces (`STATUS_TOO_MANY_WHITE_PIECES`, `STATUS_TOO_MANY_BLACK_PIECES`)
- Excessive pawns (`STATUS_TOO_MANY_WHITE_PAWNS`, `STATUS_TOO_MANY_BLACK_PAWNS`)
- Pawns placed on back ranks (`STATUS_PAWNS_ON_BACKRANK` — rank 1 or rank 8)
- Opponent currently in check (`STATUS_OPPOSITE_CHECK` — side not to move is under check)
- Impossible checks (`STATUS_TOO_MANY_CHECKERS`, `STATUS_IMPOSSIBLE_CHECK`)

### 4.2. Tolerated Position Anomalies
The following flags are **tolerated** by the API because the C++ engine cleanly sanitizes them during board loading:
- Inconsistent castling rights (`STATUS_BAD_CASTLING_RIGHTS`): If castling flags are set in FEN but rooks/kings are missing from home squares, the engine automatically revokes the invalid flags.
- Stale or invalid en-passant square (`STATUS_INVALID_EP_SQUARE`): Sanitized without error.

---

## 5. Consistent Error Schema

All error responses across all HTTP status codes return a uniform top-level JSON structure:

```json
{
  "success": false,
  "error": "<Short Error Title>",
  "detail": "<Specific detail string or validation error list>"
}
```

### Real Captured Error Payloads by Status Code

#### 400 Bad Request (Semantic FEN Error)
```json
{
  "success": false,
  "error": "Invalid FEN",
  "detail": "Invalid FEN: Missing white king"
}
```

#### 401 Unauthorized (Missing or Invalid API Key)
```json
{
  "success": false,
  "error": "Unauthorized",
  "detail": "Invalid or missing API key"
}
```

#### 404 Not Found (Invalid Route)
```json
{
  "success": false,
  "error": "Not Found",
  "detail": "Not Found"
}
```

#### 405 Method Not Allowed (e.g. GET /analyze)
```json
{
  "success": false,
  "error": "Method Not Allowed",
  "detail": "Method Not Allowed"
}
```

#### 409 Conflict (Engine Busy)
```json
{
  "success": false,
  "error": "Engine busy",
  "detail": "Another search is in progress. Try again later."
}
```

#### 422 Unprocessable Entity (Validation Error or Extra Field)
```json
{
  "success": false,
  "error": "Unprocessable Entity",
  "detail": [
    {
      "type": "less_than_equal",
      "loc": ["body", "limits", "depth"],
      "msg": "Input should be less than or equal to 20",
      "input": 25,
      "ctx": {"le": 20}
    }
  ]
}
```

#### 503 Service Unavailable (Engine Dead / Restart Limit Exceeded)
```json
{
  "success": false,
  "error": "Engine unavailable",
  "detail": "Engine process crashed repeatedly (exceeded 3 restarts within 60 seconds)"
}
```

#### 504 Gateway Timeout (Search Exceeded 30s Safety Timeout)
```json
{
  "success": false,
  "error": "Analysis timed out",
  "detail": "The search exceeded the server-side timeout."
}
```

---

## 6. Score Perspective and Conventions

Evaluations follow standard UCI conventions: **scores are reported from the perspective of the side to move**, **NOT** normalized to White.

- **Centipawn (`"cp"`)**:
  - Positive score: Side to move is winning ($+100\text{ cp} = 1\text{ pawn}$).
  - Negative score: Side to move is losing.
  - *Example*: In `4k3/8/8/8/8/8/q7/4K3 b - - 0 1`, Black is to move and up a Queen; the score is positive (`score: { "type": "cp", "value": 900 }`).
- **Mate (`"mate"`)**:
  - Positive $+N$: Side to move forces checkmate in $N$ moves/plies.
  - Negative $-N$: Side to move is checkmated in $N$ moves/plies.

---

## 7. Client Integration Guidelines

### 7.1. Timeout Configuration
- The engine enforces a 30.0-second safety timeout.
- Client HTTP requests should set read timeouts to at least **35.0 seconds** to allow structured timeout responses to complete.

### 7.2. Retry Strategy for 409 Conflict
Because only 1 search runs at a time, concurrent callers receive 409 Conflict immediately (<5ms):
```python
import time
import requests

def analyze_with_retry(payload, api_key=None, max_retries=5, base_delay=0.1):
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["X-API-Key"] = api_key
    
    for attempt in range(max_retries):
        resp = requests.post("http://localhost:8000/analyze", json=payload, headers=headers, timeout=35.0)
        if resp.status_code != 409:
            return resp
        # Exponential backoff with jitter
        sleep_time = base_delay * (2 ** attempt) + (attempt * 0.05)
        time.sleep(sleep_time)
    return resp
```

---

## 8. Stockfish Service Compatibility Analysis

| Stockfish / Service Feature | Status | Adapter / Engine Behavior |
|:---|:---:|:---|
| **Analyze by Depth** | **Implemented** | Supported via `limits.depth` (1..20). |
| **Analyze by Movetime** | **Implemented** | Supported via `limits.movetime` (10..30000 ms). |
| **Analyze by Nodes** | **Implemented** | Supported via `limits.nodes` (1..10000000). |
| **Principal Variation (PV)** | **Implemented** | Returned as UCI move string list in `pv`. |
| **Centipawn & Mate Scoring** | **Implemented** | Side-to-move perspective (`cp` and `mate`). |
| **Multi-PV (Multiple Lines)** | **Missing** | Computes single best line only (`MultiPV=1`). |
| **WDL Probability Output** | **Missing** | Win/Draw/Loss model estimates not implemented. |
| **Syzygy Tablebase Probing** | **Missing** | Syzygy probing not supported. |
| **UCI Option Tuning** | **Missing** | Hash size, Threads, and Contempt cannot be configured via API. |
| **Chess960 / Fischer Random**| **Missing** | Standard 8x8 orthodox chess rules only. |
| **Evaluation Graph / Batch** | **Missing** | Single-position analysis only; no batch endpoint. |
| **Streaming / WebSockets**   | **Missing** | Synchronous REST only; no incremental search stream. |

---

## 9. Real Captured curl Commands

### Check Health
```bash
curl -sS -X GET "http://localhost:8000/health"
```

### Analyze Start Position (Default Depth 6)
```bash
curl -sS -X POST "http://localhost:8000/analyze" \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"}'
```

### Analyze with Limits (Depth 4 and Movetime 500ms)
```bash
curl -sS -X POST "http://localhost:8000/analyze" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your_secret_key" \
  -d '{
    "fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "limits": {
      "depth": 4,
      "movetime": 500
    }
  }'
```
