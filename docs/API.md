# Chess Engine HTTP API Specification & Integration Guide

This document defines the **real, observed HTTP API contract** for the chess engine service. It documents actual runtime behavior, real JSON payloads, exact error response structures, engine interaction mechanics, and integration requirements for client applications.

---

## 1. Architecture & Protocol Overview

The service acts as an HTTP/JSON adapter sitting in front of a compiled C++ UCI chess engine (`build/chess_engine`):

```
Client Application (Web / Desktop / Bot)
       │
       │ HTTP / JSON (REST)
       ▼
 FastAPI Server (`api/app.py` on Uvicorn)
       │
       │ Thread-safe Queue + Subprocess Pipes (stdin / stdout)
       ▼
 Engine Manager (`api/engine_manager.py`)
       │
       │ UCI Protocol (`ucinewgame`, `position fen ...`, `go ...`)
       ▼
 C++ Chess Engine Process (`build/chess_engine`)
```

- **Execution Model**: Single engine subprocess managed by a background reader thread and a threading lock.
- **Concurrency**: Exactly one search runs at any given time. Concurrent analysis requests return `409 Conflict` (or are serialized behind ASGI event loop handling).
- **Subprocess Lifecycle**: The engine process is launched upon server startup. If the subprocess terminates or crashes, it is **not** automatically respawned; subsequent calls return `503 Service Unavailable`.
- **TT Pollution Guard**: Before each search, the adapter sends `ucinewgame` and waits for `isready` / `readyok` to ensure clean, deterministic evaluations without cross-request state leakage.

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
- **Unauthenticated Failure**: Returns HTTP `401 Unauthorized` (see [Error Response Shapes](#5-error-response-shapes) for the exact payload).

---

## 3. Endpoints

### 3.1. GET /health

Returns operational status and readiness of the underlying C++ engine process.

- **Authentication**: **Always exempt**. Accessible whether `API_KEY` is configured or not.
- **Method**: `GET`
- **Path**: `/health` (also handles `/health/` via standard 307 redirect)
- **Response Codes**: `200 OK`

#### Response Body Schema (200 OK)
```json
{
  "status": "ok",
  "engine": "lightweight-chess-engine",
  "ready": true
}
```

| Field | Type | Description |
|:---|:---|:---|
| `status` | `string` | `"ok"` if engine is running and responding; `"degraded"` if process is unresponsive. |
| `engine` | `string` | Engine identifier (`"lightweight-chess-engine"`). |
| `ready` | `boolean` | `true` if engine responded `readyok` to an `isready` probe; `false` otherwise. |

#### Sample `curl`:
```bash
curl -sS http://localhost:8000/health
```

---

### 3.2. POST /analyze

Analyzes a chess position specified by FEN and returns the engine's best move, principal variation, score evaluation, and search statistics.

- **Authentication**: Requires `X-API-Key` if `API_KEY` is set on server.
- **Method**: `POST`
- **Path**: `/analyze`
- **Headers**:
  - `Content-Type: application/json`
  - `X-API-Key: <your-key>` (conditional)
- **Response Codes**: `200 OK`, `400 Bad Request`, `401 Unauthorized`, `409 Conflict`, `422 Unprocessable Entity`, `503 Service Unavailable`, `504 Gateway Timeout`.

#### Request Body Schema
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "limits": {
    "depth": 8,
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
| `limits.movetime` | `integer` | No | `null` | `1 <= movetime <= 30000` | Search time limit in milliseconds. |
| `limits.nodes` | `integer` | No | `null` | `1 <= nodes <= 10000000` | Maximum node budget. |

> [!NOTE]
> If multiple limit fields are provided (e.g. `depth` and `movetime`), all are sent to the engine as `go depth X movetime Y`, and the engine stops whenever the first constraint is reached.

#### Response Body Schema (200 OK)
```json
{
  "bestmove": "e2e4",
  "depth": 8,
  "seldepth": 11,
  "score": {
    "type": "cp",
    "value": 35
  },
  "nodes": 24192,
  "nps": 1209600,
  "time_ms": 20,
  "pv": ["e2e4", "c7c5", "g1f3", "d7d6"],
  "timed_out": false
}
```

| Field | Type | Description |
|:---|:---|:---|
| `bestmove` | `string` \| `null` | Best move in UCI long algebraic notation (e.g. `"e2e4"`, `"e7e8q"`). Returns `null` if the position is terminal (checkmate or stalemate). |
| `depth` | `integer` | Completed search depth in plies. |
| `seldepth` | `integer` | Maximum selective search depth reached in quiescence search. |
| `score` | `object` | Position evaluation from the **side-to-move's perspective** (see [Score Perspective](#4-score-perspective-and-evaluations)). |
| `score.type` | `string` | `"cp"` for centipawns or `"mate"` for mate distance. |
| `score.value` | `integer` | Evaluation value: centipawns (100 cp = 1 pawn advantage) or plies to mate (positive = winning, negative = losing). |
| `nodes` | `integer` | Total positions visited during search. |
| `nps` | `integer` | Search speed in nodes per second. |
| `time_ms` | `integer` | Search execution wall time in milliseconds as reported by the engine. |
| `pv` | `array[string]` | Principal variation: ordered sequence of expected moves in UCI notation. |
| `timed_out` | `boolean` | `true` if search was forcefully aborted by the server 30-second safety timeout. |

#### Sample `curl`:
```bash
# Standard analysis
curl -sS -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "limits": {"depth": 6}}'

# Analysis with API key
curl -sS -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -H "X-API-Key: my-secret-key" \
  -d '{"fen": "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "limits": {"movetime": 500}}'
```

---

## 4. Score Perspective and Evaluations

### Side-to-Move Convention
Evaluations in the `score` object follow the standard UCI engine convention: **scores are reported from the perspective of the side to move**, **NOT** normalized to White.

- **Centipawn (`"cp"`)**:
  - Positive score: The player whose turn it is possesses the advantage.
  - Negative score: The player whose turn it is is at a disadvantage.
  - *Example*: In `rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2`, White to move has `+25 cp`. If Black were to move in an equivalent mirror position, Black's advantage would also be `+25 cp`.
- **Mate (`"mate"`)**:
  - Positive value $+N$: The side to move can force checkmate in $N$ moves/plies.
  - Negative value $-N$: The side to move will be checkmated in $N$ moves/plies against optimal play.
- **Terminal Positions**:
  - Checkmated position: `bestmove: null`, `score: { "type": "mate", "value": 0 }`.
  - Stalemate / Draw: `bestmove: null`, `score: { "type": "cp", "value": 0 }`.

---

## 5. Error Response Shapes

> [!WARNING]
> **API Schema Inconsistency Alert**:
> Due to how FastAPI and Starlette middleware structure error payloads, different HTTP error codes exhibit different root JSON shapes. Integration clients must handle both top-level and nested `detail` keys.

### 5.1. Summary of Error Response Formats

| Status Code | Error Reason | Response Body Shape | Root Key |
|:---|:---|:---|:---|
| **401 Unauthorized** | Missing or invalid `X-API-Key` | `{"success": false, "error": "...", "detail": "..."}` | Top-level |
| **400 Bad Request** | Unparseable FEN string | `{"detail": {"success": false, "error": "Invalid FEN", "detail": "..."}}` | Nested in `detail` |
| **409 Conflict** | Engine busy with another search | `{"detail": {"success": false, "error": "Engine busy", "detail": "..."}}` | Nested in `detail` |
| **422 Unprocessable** | Request schema / range validation | `{"detail": [{"loc": [...], "msg": "...", "type": "..."}]}` | FastAPI array |
| **404 Not Found** | Unknown route | `{"detail": "Not Found"}` | String in `detail` |
| **405 Method Not Allowed** | Wrong HTTP method (e.g. GET `/analyze`) | `{"detail": "Method Not Allowed"}` | String in `detail` |
| **503 Unavailable** | Engine process crashed or unavailable | `{"detail": {"success": false, "error": "Engine unavailable", "detail": "..."}}` | Nested in `detail` |
| **504 Gateway Timeout** | Search exceeded 30s without move | `{"detail": {"success": false, "error": "Search timed out", "detail": "..."}}` | Nested in `detail` |

---

### 5.2. Exact Payloads by Status Code

#### HTTP 400 Bad Request
Triggered when the `fen` field cannot be parsed by python-chess:
```json
{
  "detail": {
    "success": false,
    "error": "Invalid FEN",
    "detail": "Invalid FEN: expected 8 rows in position part of FEN: 'rnbqkbnr/pppppppp'"
  }
}
```

#### HTTP 401 Unauthorized
Triggered when `API_KEY` is configured on the server and `X-API-Key` is missing or invalid:
```json
{
  "success": false,
  "error": "Unauthorized",
  "detail": "Invalid or missing API key"
}
```

#### HTTP 409 Conflict
Triggered when an incoming analysis request attempts to acquire the engine lock while another search is running:
```json
{
  "detail": {
    "success": false,
    "error": "Engine busy",
    "detail": "Another search is already in progress"
  }
}
```

#### HTTP 422 Unprocessable Entity
Triggered when request JSON violates Pydantic schema constraints (e.g., `depth > 20`, negative `movetime`, or invalid types):
```json
{
  "detail": [
    {
      "type": "less_than_equal",
      "loc": ["body", "limits", "depth"],
      "msg": "Input should be less than or equal to 20",
      "input": 25,
      "ctx": { "le": 20 }
    }
  ]
}
```

#### HTTP 503 Service Unavailable
Triggered when the engine subprocess has crashed, exited, or failed to start:
```json
{
  "detail": {
    "success": false,
    "error": "Engine unavailable",
    "detail": "Engine process is not running"
  }
}
```

#### HTTP 504 Gateway Timeout
Triggered if the engine process does not emit a `bestmove` within the hardcoded 30.0-second safety timeout:
```json
{
  "detail": {
    "success": false,
    "error": "Search timed out",
    "detail": "Search exceeded 30.0s timeout"
  }
}
```

---

## 6. Known Engine Quirks & Limitations Leaking Through API

1. **Semantic FEN Validation Gap**:
   - `python-chess`'s basic `chess.Board(fen)` constructor validates only structural token layout.
   - Semantically impossible positions—such as boards with no kings (`8/8/8/8/8/8/8/8 w - - 0 1`), two white kings, pawns on the 1st or 8th rank, or positions where the opponent is in check—pass validation and return `200 OK` from the C++ engine.
2. **Horizon Blunder at Depth 1**:
   - In specific tactical back-rank positions like `6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1`, requesting `depth: 1` selects `a1a7` instead of delivering immediate mate with `a1a8#` due to quiescence search horizon pruning. Searches at `depth >= 2` find the mate reliably.
3. **Subprocess Resilience & Crash Recovery**:
   - `EngineManager` holds a single `subprocess.Popen` reference. If the engine dies due to an OS signal or crash, the manager does **not** automatically restart the process. All subsequent requests return `503 Service Unavailable` until the web service is restarted.
4. **Extra Body Fields Allowed**:
   - Undeclared fields in the request JSON (e.g. `{"fen": "...", "unknown_param": 123}`) are ignored rather than rejected with `422`, because Pydantic models default to `extra='ignore'`.
5. **Node Count Stopping Precision**:
   - When specifying `limits.nodes`, the engine completes the current iterative deepening ply before halting, or terminates early when the next iteration exceeds the budget. As a result, the returned `nodes` value may be lower than requested (if the engine aborted before starting the next full depth) or slightly over budget.

---

## 7. Recommended Client Integration Patterns

### 7.1. Timeout Configuration
- The engine enforces an internal safety timeout of **30.0 seconds**.
- Client HTTP libraries should configure request timeouts of **at least 35 to 40 seconds** to avoid breaking connections before the server can return structured responses or timeout payloads.

### 7.2. Retry Strategy for Concurrency (409 Conflict)
Because the engine allows only 1 concurrent search, concurrent callers will encounter `409 Conflict`:
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

### 7.3. Client-Side Error Normalizer
To shield frontend or client code from the schema discrepancies between 401 and other errors, use an error extractor:
```typescript
interface ApiError {
  success: false;
  error: string;
  detail: string;
}

function normalizeError(status: number, body: any): ApiError {
  if (body?.detail?.error) {
    return {
      success: false,
      error: body.detail.error,
      detail: body.detail.detail || ""
    };
  }
  if (body?.error) {
    return {
      success: false,
      error: body.error,
      detail: body.detail || ""
    };
  }
  if (Array.isArray(body?.detail)) {
    return {
      success: false,
      error: "Validation Error",
      detail: body.detail.map((e: any) => `${e.loc.join('.')}: ${e.msg}`).join(', ')
    };
  }
  return {
    success: false,
    error: `HTTP ${status}`,
    detail: typeof body?.detail === 'string' ? body.detail : JSON.stringify(body)
  };
}
```

---

## 8. Stockfish Service Compatibility Analysis

For systems integrating this engine as a replacement or companion to a remote Stockfish service (e.g. Lichess, Chess.com, or Stockfish API endpoints), the following matrix details supported vs missing features:

| Stockfish / Service Feature | Status in this API | Engine / Adapter Behavior |
|:---|:---:|:---|
| **Analyze by Depth** | **Supported** | `limits.depth` (1..20). |
| **Analyze by Movetime** | **Supported** | `limits.movetime` (1..30000 ms). |
| **Analyze by Nodes** | **Supported** | `limits.nodes` (1..10000000). |
| **Principal Variation (PV)** | **Supported** | Returned as list of UCI move strings in `pv`. |
| **Centipawn & Mate Scoring** | **Supported** | Returned in `score` object (`type: "cp" \| "mate"`). |
| **Multi-PV (Multiple Lines)** | **Missing** | Engine only computes single best line (`MultiPV=1`). |
| **WDL Probability Output** | **Missing** | Win/Draw/Loss model estimates not implemented. |
| **Syzygy Tablebase Probing** | **Missing** | No Syzygy path configuration or tablebase probing. |
| **UCI Option Tuning** | **Missing** | Hash size, Threads, Contempt cannot be tuned via API. |
| **Chess960 / Fischer Random**| **Missing** | Standard 8x8 orthodox chess rules only. |
| **Evaluation Graph / Batch** | **Missing** | No batch analysis endpoint; requires sequential calls. |
| **Semantic FEN Validation**  | **Partial** | Syntactic token check only; illegal piece counts pass. |
| **Streaming / WebSockets**   | **Missing** | Synchronous REST only; no real-time evaluation stream. |

---

## 9. Comprehensive Route & Curl Reference

### Check Engine Health
```bash
curl -X GET "http://localhost:8000/health"
```

### Analyze Start Position (Default Depth 6)
```bash
curl -X POST "http://localhost:8000/analyze" \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"}'
```

### Analyze with Depth and Movetime Constraints
```bash
curl -X POST "http://localhost:8000/analyze" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your_api_key_here" \
  -d '{
    "fen": "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R b KQkq - 0 5",
    "limits": {
      "depth": 12,
      "movetime": 1500
    }
  }'
```

### Analyze with Node Budget
```bash
curl -X POST "http://localhost:8000/analyze" \
  -H "Content-Type: application/json" \
  -d '{
    "fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "limits": {
      "nodes": 100000
    }
  }'
```
