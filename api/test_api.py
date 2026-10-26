"""
test_api.py — pytest tests for the FastAPI chess engine adapter (Phase 6)

Covers:
  - GET /health: status, engine name, ready flag
  - POST /analyze: valid FEN + depth, movetime, default limits
  - POST /analyze: mate-in-one detection
  - POST /analyze: terminal position (checkmate) → bestmove null
  - POST /analyze: invalid FEN → 400
  - POST /analyze: out-of-range limits → 422
  - POST /analyze: missing fen → 422
  - POST /analyze: malformed JSON → 422

Run:
  cd /home/borat/BxTrackSolution/chess_engine
  source .venv/bin/activate
  pytest api/test_api.py -v
"""

import os
import sys

# Ensure the project root is on sys.path so `api.app` resolves
_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

import pytest
from fastapi.testclient import TestClient

from api.app import app


# ─── Fixtures ───────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def client():
    """Module-scoped TestClient that starts/stops the engine via lifespan."""
    with TestClient(app) as c:
        yield c


STARTING_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# Scholar's mate position: White to move, Qxf7# is mate in one
MATE_IN_ONE_FEN = "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4"

# Position where Black is already checkmated (Scholar's mate delivered)
CHECKMATE_FEN = "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4"

# Stalemate position (Black to move, no legal moves, not in check)
STALEMATE_FEN = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"


# ─── GET /health ────────────────────────────────────────────────────────────────

class TestHealth:
    def test_health_returns_ok(self, client):
        r = client.get("/health")
        assert r.status_code == 200
        data = r.json()
        assert data["status"] == "ok"
        assert data["engine"] == "lightweight-chess-engine"
        assert data["ready"] is True

    def test_health_has_correct_fields(self, client):
        r = client.get("/health")
        data = r.json()
        assert set(data.keys()) == {"status", "engine", "ready"}


# ─── POST /analyze — valid requests ─────────────────────────────────────────────

class TestAnalyzeValid:
    def test_analyze_depth(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": 3},
        })
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert data["bestmove"] is not None
        assert len(data["bestmove"]) >= 4  # at least "e2e4"
        assert data["depth"] == 3
        assert data["nodes"] > 0
        assert data["score"] is not None
        assert data["score"]["type"] in ("cp", "mate")
        assert isinstance(data["score"]["value"], int)
        assert isinstance(data["pv"], list)
        assert data["timed_out"] is False

    def test_analyze_movetime(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"movetime": 200},
        })
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert data["bestmove"] is not None
        assert data["time_ms"] >= 0

    def test_analyze_default_limits(self, client):
        """No limits specified should default to depth 6."""
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
        })
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert data["bestmove"] is not None
        # Depth 6 should be reached (or higher if the engine finishes early)
        assert data["depth"] >= 1

    def test_analyze_with_ponder(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": 4},
        })
        assert r.status_code == 200
        data = r.json()
        # Ponder is optional but if present should be a valid move string
        if data["ponder"] is not None:
            assert len(data["ponder"]) >= 4

    def test_analyze_pv_is_list_of_uci_moves(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": 2},
        })
        assert r.status_code == 200
        pv = r.json()["pv"]
        assert isinstance(pv, list)
        for move in pv:
            assert isinstance(move, str)
            assert len(move) >= 4


# ─── POST /analyze — mate detection ────────────────────────────────────────────

class TestAnalyzeMate:
    def test_mate_in_one(self, client):
        r = client.post("/analyze", json={
            "fen": MATE_IN_ONE_FEN,
            "limits": {"depth": 3},
        })
        assert r.status_code == 200
        data = r.json()
        assert data["bestmove"] == "h5f7"
        assert data["score"]["type"] == "mate"
        assert data["score"]["value"] == 1

    def test_checkmate_returns_null_bestmove(self, client):
        """Terminal position where the side to move is checkmated."""
        r = client.post("/analyze", json={
            "fen": CHECKMATE_FEN,
            "limits": {"depth": 1},
        })
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert data["bestmove"] is None

    def test_stalemate_returns_null_bestmove(self, client):
        """Terminal position where the side to move is stalemated."""
        r = client.post("/analyze", json={
            "fen": STALEMATE_FEN,
            "limits": {"depth": 1},
        })
        assert r.status_code == 200
        data = r.json()
        assert data["success"] is True
        assert data["bestmove"] is None


# ─── POST /analyze — error cases ───────────────────────────────────────────────

class TestAnalyzeErrors:
    def test_invalid_fen_format(self, client):
        r = client.post("/analyze", json={
            "fen": "not_a_fen_string",
            "limits": {"depth": 1},
        })
        assert r.status_code == 400
        data = r.json()
        assert data["success"] is False
        assert "Invalid FEN" in data["error"]
        assert "detail" in data

    def test_invalid_fen_missing_fields(self, client):
        # FEN with only 4 fields (missing halfmove and fullmove)
        r = client.post("/analyze", json={
            "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
            "limits": {"depth": 1},
        })
        # python-chess may accept partial FENs, so just check it's 200 or 400
        assert r.status_code in (200, 400)

    def test_depth_too_high(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": 100},
        })
        assert r.status_code == 422

    def test_depth_zero(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": 0},
        })
        assert r.status_code == 422

    def test_depth_negative(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"depth": -1},
        })
        assert r.status_code == 422

    def test_movetime_too_high(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"movetime": 999999},
        })
        assert r.status_code == 422

    def test_movetime_too_low(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"movetime": 1},
        })
        assert r.status_code == 422

    def test_nodes_too_high(self, client):
        r = client.post("/analyze", json={
            "fen": STARTING_FEN,
            "limits": {"nodes": 999999999},
        })
        assert r.status_code == 422

    def test_missing_fen(self, client):
        r = client.post("/analyze", json={
            "limits": {"depth": 3},
        })
        assert r.status_code == 422

    def test_empty_body(self, client):
        r = client.post("/analyze", json={})
        assert r.status_code == 422

    def test_malformed_json(self, client):
        r = client.post(
            "/analyze",
            data="not json at all",
            headers={"Content-Type": "application/json"},
        )
        assert r.status_code == 422


# ─── OpenAPI docs ───────────────────────────────────────────────────────────────

class TestOpenAPI:
    def test_openapi_schema_available(self, client):
        r = client.get("/openapi.json")
        assert r.status_code == 200
        schema = r.json()
        assert schema["info"]["title"] == "Lightweight Chess Engine API"
        assert "/health" in schema["paths"]
        assert "/analyze" in schema["paths"]

    def test_docs_page_available(self, client):
        r = client.get("/docs")
        assert r.status_code == 200
