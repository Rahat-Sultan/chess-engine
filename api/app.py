"""
app.py — FastAPI HTTP/JSON adapter for the chess engine (Phase 6)

Exposes two endpoints:
  GET  /health   — engine health check
  POST /analyze  — analyze a position and return best move + info

Architecture:
  Frontend → Backend → THIS API → UCI pipes → chess_engine (C++ process)

The adapter spawns the C++ engine as a subprocess and communicates
via UCI protocol. It validates requests, enforces safe limits, and
returns structured JSON.

Run:
  source .venv/bin/activate
  uvicorn api.app:app --host 0.0.0.0 --port 8000

Or:
  python3 -m uvicorn api.app:app --reload
"""

from __future__ import annotations

import logging
import os
import secrets
from contextlib import asynccontextmanager
from typing import Optional

import chess
from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from api.engine_manager import EngineManager, EngineBusy, EngineNotReady, EngineError

# ─── Logging ────────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger("api")

# ─── Configuration ──────────────────────────────────────────────────────────────

ENGINE_PATH = os.environ.get(
    "CHESS_ENGINE_PATH",
    os.path.join(os.path.dirname(__file__), "..", "build", "chess_engine"),
)

# Safe maximums to prevent unbounded CPU use (spec §7.9)
MAX_DEPTH = 20
MAX_MOVETIME_MS = 30_000       # 30 seconds
MAX_NODES = 10_000_000
DEFAULT_TIMEOUT_SEC = 30.0    # server-side safety timeout

# ─── Engine manager (global, managed by lifespan) ──────────────────────────────

engine = EngineManager(ENGINE_PATH)

# ─── Pydantic models ───────────────────────────────────────────────────────────


class AnalyzeLimits(BaseModel):
    """Search limits for the analysis request."""
    depth: Optional[int] = Field(
        default=None, ge=1, le=MAX_DEPTH,
        description="Maximum search depth in plies (1–20)",
    )
    movetime: Optional[int] = Field(
        default=None, ge=10, le=MAX_MOVETIME_MS,
        description="Time limit in milliseconds (10–30000)",
    )
    nodes: Optional[int] = Field(
        default=None, ge=1, le=MAX_NODES,
        description="Node count limit (1–10000000)",
    )


class AnalyzeRequest(BaseModel):
    """Request body for POST /analyze."""
    fen: str = Field(
        ...,
        description="FEN string of the position to analyze (six fields)",
    )
    limits: AnalyzeLimits = Field(
        default_factory=AnalyzeLimits,
        description="Search limits. If none specified, defaults to depth 6.",
    )


class ScoreInfo(BaseModel):
    """Engine evaluation score."""
    type: str = Field(description="'cp' (centipawns) or 'mate' (plies to mate)")
    value: int = Field(description="Score value (centipawns or mate distance)")


class AnalyzeResponse(BaseModel):
    """Successful analysis result."""
    success: bool = True
    bestmove: Optional[str] = Field(
        default=None,
        description="Best move in UCI notation (e.g. 'e2e4'), or null if terminal",
    )
    ponder: Optional[str] = Field(
        default=None,
        description="Ponder move in UCI notation, if available",
    )
    score: Optional[ScoreInfo] = Field(
        default=None,
        description="Position score from the engine's perspective",
    )
    depth: int = Field(default=0, description="Search depth reached (plies)")
    nodes: int = Field(default=0, description="Nodes searched")
    nps: int = Field(default=0, description="Nodes per second")
    time_ms: int = Field(default=0, description="Search time in milliseconds")
    pv: list[str] = Field(
        default_factory=list,
        description="Principal variation in UCI notation",
    )
    timed_out: bool = Field(
        default=False,
        description="True if the server-side safety timeout was triggered",
    )


class HealthResponse(BaseModel):
    """Health check response."""
    status: str = Field(description="'ok' if healthy")
    engine: str = Field(description="Engine name")
    ready: bool = Field(description="True if engine is ready to accept searches")


class ErrorResponse(BaseModel):
    """Error response for failed requests."""
    success: bool = False
    error: str = Field(description="Short error category")
    detail: Optional[str] = Field(default=None, description="Additional detail")


# ─── FastAPI app ────────────────────────────────────────────────────────────────


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Start and stop the engine process with the server."""
    logger.info("Starting chess engine: %s", ENGINE_PATH)
    engine.start()
    logger.info("Engine ready, serving requests")
    yield
    logger.info("Shutting down engine")
    engine.stop()


app = FastAPI(
    title="Lightweight Chess Engine API",
    description=(
        "HTTP/JSON adapter for an original C++ chess engine. "
        "Accepts a FEN position and search limits, returns the best move "
        "and analysis information via UCI internally."
    ),
    version="0.1.0-phase6",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ─── API Key Authentication Middleware ─────────────────────────────────────────


@app.middleware("http")
async def api_key_auth_middleware(request: Request, call_next):
    """
    Optional API-key authentication.
    If the environment variable API_KEY is set:
      - GET /health is always permitted (for container orchestration / healthchecks)
      - OPTIONS preflight is permitted (for CORS)
      - All other endpoints require 'X-API-Key' matching API_KEY via constant-time comparison (401 otherwise)
    If API_KEY is not set:
      - Behavior is unchanged (open access).
    """
    expected_key = os.environ.get("API_KEY")
    if expected_key:
        if request.method == "OPTIONS":
            return await call_next(request)
        if request.method == "GET" and request.url.path.rstrip("/") == "/health":
            return await call_next(request)

        provided_key = request.headers.get("X-API-Key")
        if not provided_key or not secrets.compare_digest(provided_key, expected_key):
            return JSONResponse(
                status_code=401,
                content={
                    "success": False,
                    "error": "Unauthorized",
                    "detail": "Invalid or missing API key",
                },
            )
    return await call_next(request)


# ─── GET /health ───────────────────────────────────────────────────────────────


@app.get("/health", response_model=HealthResponse,
         summary="Check engine health")
async def health():
    """Return the engine's readiness status."""
    return HealthResponse(
        status="ok" if engine.is_ready() else "degraded",
        engine="lightweight-chess-engine",
        ready=engine.is_ready(),
    )


# ─── POST /analyze ────────────────────────────────────────────────────────────


@app.post(
    "/analyze",
    response_model=AnalyzeResponse,
    summary="Analyze a chess position",
    responses={
        400: {"model": ErrorResponse, "description": "Invalid FEN"},
        401: {"model": ErrorResponse, "description": "Unauthorized (missing or invalid API key)"},
        409: {"model": ErrorResponse, "description": "Engine busy"},
        503: {"model": ErrorResponse, "description": "Engine unavailable"},
        504: {"model": ErrorResponse, "description": "Analysis timed out"},
    },
)
async def analyze(request: AnalyzeRequest):
    """
    Analyze a chess position and return the best move.

    Send a FEN string and optional search limits (depth, movetime, or
    nodes). The engine searches and returns the best move, score, depth,
    nodes, and principal variation.
    """
    # ── Validate FEN ────────────────────────────────────────────────────────
    try:
        chess.Board(request.fen)
    except ValueError as e:
        raise HTTPException(
            status_code=400,
            detail={
                "success": False,
                "error": "Invalid FEN",
                "detail": str(e),
            },
        )

    # ── Determine search parameters ─────────────────────────────────────────
    limits = request.limits
    depth = limits.depth
    movetime = limits.movetime
    nodes = limits.nodes

    # Default: depth 6 if no limits specified
    if depth is None and movetime is None and nodes is None:
        depth = 6

    # ── Run the analysis ────────────────────────────────────────────────────
    try:
        raw = engine.analyze(
            fen=request.fen,
            depth=depth,
            movetime=movetime,
            nodes=nodes,
            timeout=DEFAULT_TIMEOUT_SEC,
        )
    except EngineBusy:
        raise HTTPException(
            status_code=409,
            detail={
                "success": False,
                "error": "Engine busy",
                "detail": "Another search is in progress. Try again later.",
            },
        )
    except EngineNotReady:
        raise HTTPException(
            status_code=503,
            detail={
                "success": False,
                "error": "Engine unavailable",
                "detail": "The engine process is not running or not ready.",
            },
        )
    except EngineError as e:
        raise HTTPException(
            status_code=503,
            detail={
                "success": False,
                "error": "Engine error",
                "detail": str(e),
            },
        )

    # ── Build the response ──────────────────────────────────────────────────
    bestmove = raw.get("bestmove")
    if bestmove == "(none)":
        bestmove = None  # terminal position (checkmate/stalemate)

    score = None
    if "score_type" in raw and "score_value" in raw:
        score = ScoreInfo(type=raw["score_type"], value=raw["score_value"])

    timed_out = raw.get("timed_out", False)

    # If the engine timed out and didn't produce a move, return 504
    if timed_out and bestmove is None:
        raise HTTPException(
            status_code=504,
            detail={
                "success": False,
                "error": "Analysis timed out",
                "detail": "The search exceeded the server-side timeout.",
            },
        )

    return AnalyzeResponse(
        success=True,
        bestmove=bestmove,
        ponder=raw.get("ponder"),
        score=score,
        depth=raw.get("depth", 0),
        nodes=raw.get("nodes", 0),
        nps=raw.get("nps", 0),
        time_ms=raw.get("time_ms", 0),
        pv=raw.get("pv", []),
        timed_out=timed_out,
    )
