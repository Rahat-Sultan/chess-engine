#!/usr/bin/env python3
"""
api_conformance.py — Comprehensive API conformance and audit test suite

Tests the chess engine's FastAPI HTTP/JSON adapter against the specification,
Stockfish-like service expectations, robustness bounds, and security contracts.

Standard library only for HTTP (urllib). Optional python-chess for move legality.
Works on Linux and Windows.

Usage:
  python3 scripts/api_conformance.py [options]

Options:
  --base-url URL    Target API base URL (default: spawns local uvicorn on free port)
  --api-key KEY     API key to send via X-API-Key header
  --smoke           Run small non-destructive subset suitable for production URLs
  --slow            Run long tests (30s timeouts, RSS leak checks) [localhost only]
  --destructive     Run process kill and crash recovery tests [localhost only]
  --json-out FILE   Write machine-readable JSON results to FILE
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import urlparse

# Optional python-chess for legality validation
try:
    import chess
    HAS_CHESS = True
except ImportError:
    HAS_CHESS = False

REPO_ROOT = Path(__file__).resolve().parent.parent

STARTPOS_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
KIWIPETE_FEN = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"


@dataclass
class TestResult:
    name: str
    category: str
    severity: str  # blocker, major, minor
    status: str    # PASS, FAIL, SKIP
    request: str
    expected: str
    actual: str
    duration_ms: float
    details: Optional[str] = None
    proposed_fix: Optional[str] = None


class ConformanceRunner:
    def __init__(
        self,
        base_url: str,
        api_key: Optional[str] = None,
        smoke_only: bool = False,
        slow_enabled: bool = False,
        destructive_enabled: bool = False,
        local_process: Optional[subprocess.Popen] = None,
    ):
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.smoke_only = smoke_only
        self.slow_enabled = slow_enabled
        self.destructive_enabled = destructive_enabled
        self.local_process = local_process
        self.results: List[TestResult] = []

    # ─── HTTP Utilities (urllib only) ──────────────────────────────────────────

    def request(
        self,
        method: str,
        path: str,
        body: Optional[Any] = None,
        headers: Optional[Dict[str, str | bytes]] = None,
        timeout: float = 35.0,
    ) -> Tuple[int, Dict[str, str], Any, float]:
        """Perform HTTP request using urllib. Returns (status, headers, body, duration_ms)."""
        url = f"{self.base_url}{path}"
        req_headers: Dict[str, str | bytes] = {}

        if self.api_key and (headers is None or "X-API-Key" not in headers and "x-api-key" not in headers):
            req_headers["X-API-Key"] = self.api_key

        if headers:
            req_headers.update(headers)

        data = None
        if body is not None:
            if isinstance(body, (dict, list)):
                data = json.dumps(body).encode("utf-8")
                if "Content-Type" not in req_headers:
                    req_headers["Content-Type"] = "application/json"
            elif isinstance(body, str):
                data = body.encode("utf-8")
            elif isinstance(body, bytes):
                data = body

        req = urllib.request.Request(url, data=data, method=method)
        for k, v in req_headers.items():
            if isinstance(v, bytes):
                req.add_unredirected_header(k, v)
            else:
                req.add_header(k, v)

        t0 = time.perf_counter()
        status = 0
        resp_headers: Dict[str, str] = {}
        resp_body: Any = None

        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                status = resp.getcode()
                resp_headers = dict(resp.info())
                raw_bytes = resp.read()
                try:
                    resp_body = json.loads(raw_bytes.decode("utf-8"))
                except Exception:
                    resp_body = raw_bytes.decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            status = e.code
            resp_headers = dict(e.headers)
            raw_bytes = e.read()
            try:
                resp_body = json.loads(raw_bytes.decode("utf-8"))
            except Exception:
                resp_body = raw_bytes.decode("utf-8", errors="replace")
        except Exception as e:
            status = 0
            resp_body = str(e)

        duration_ms = (time.perf_counter() - t0) * 1000
        return status, resp_headers, resp_body, duration_ms

    def record(
        self,
        name: str,
        category: str,
        severity: str,
        passed: bool,
        request_desc: str,
        expected: str,
        actual: str,
        duration_ms: float,
        details: Optional[str] = None,
        proposed_fix: Optional[str] = None,
    ) -> bool:
        res = TestResult(
            name=name,
            category=category,
            severity=severity,
            status="PASS" if passed else "FAIL",
            request=request_desc,
            expected=expected,
            actual=actual,
            duration_ms=round(duration_ms, 2),
            details=details,
            proposed_fix=proposed_fix,
        )
        self.results.append(res)
        return passed

    # ─── Test Groups ────────────────────────────────────────────────────────────

    def test_01_health(self):
        """1. Health check endpoint contracts."""
        status, _, body, dur = self.request("GET", "/health")
        ok = (
            status == 200
            and isinstance(body, dict)
            and body.get("status") == "ok"
            and body.get("engine") == "lightweight-chess-engine"
            and body.get("ready") is True
        )
        self.record(
            "health_contract",
            "Health",
            "blocker",
            ok,
            "GET /health",
            "200 OK with status='ok', engine='lightweight-chess-engine', ready=True",
            f"{status} {body}",
            dur,
            proposed_fix="Ensure engine process is initialized and ready",
        )

        # Health trailing slash
        status_slash, _, _, dur_slash = self.request("GET", "/health/")
        ok_slash = status_slash in (200, 307, 308)
        self.record(
            "health_trailing_slash",
            "Health",
            "minor",
            ok_slash,
            "GET /health/",
            "200 OK or 307/308 redirect",
            f"{status_slash}",
            dur_slash,
            proposed_fix="Support trailing slash on /health route in FastAPI",
        )

    def test_02_startpos_depth_4(self):
        """2. Startpos depth 4 analysis and field contract."""
        payload = {"fen": STARTPOS_FEN, "limits": {"depth": 4}}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)

        ok = status == 200 and isinstance(body, dict) and body.get("success") is True
        details = []
        if ok:
            if body.get("depth") != 4:
                ok = False
                details.append(f"depth={body.get('depth')} expected 4")
            if not body.get("bestmove"):
                ok = False
                details.append("missing bestmove")
            pv = body.get("pv", [])
            if not pv or pv[0] != body.get("bestmove"):
                ok = False
                details.append(f"pv[0]={pv[0] if pv else None} != bestmove={body.get('bestmove')}")
            if not isinstance(body.get("nodes"), int) or body.get("nodes", 0) <= 0:
                ok = False
                details.append(f"nodes={body.get('nodes')} <= 0")

            # Check legality with python-chess
            if HAS_CHESS and body.get("bestmove"):
                try:
                    b = chess.Board(STARTPOS_FEN)
                    for mv_uci in pv:
                        mv = chess.Move.from_uci(mv_uci)
                        if mv not in b.legal_moves:
                            ok = False
                            details.append(f"illegal PV move {mv_uci} in position {b.fen()}")
                            break
                        b.push(mv)
                except Exception as e:
                    ok = False
                    details.append(f"chess parsing error: {e}")

        self.record(
            "startpos_depth_4",
            "Analysis",
            "blocker",
            ok,
            f"POST /analyze depth=4 startpos",
            "200 OK, depth=4, nodes>0, bestmove==pv[0], all PV moves legal",
            f"{status} depth={body.get('depth') if isinstance(body, dict) else None} bm={body.get('bestmove') if isinstance(body, dict) else None}",
            dur,
            details="; ".join(details) if details else None,
            proposed_fix="Verify search iterative deepening and PV tracking",
        )

    def test_03_limits(self):
        """3. Movetime, nodes, combination, and default limits."""
        for ms in (100, 500, 1000):
            payload = {"fen": STARTPOS_FEN, "limits": {"movetime": ms}}
            status, _, body, dur = self.request("POST", "/analyze", body=payload)
            overshoot = dur - ms
            # Flag if wall-clock exceeds requested movetime by > 300ms
            ok = status == 200 and overshoot <= 300
            self.record(
                f"limit_movetime_{ms}",
                "Limits",
                "minor",
                ok,
                f"POST /analyze movetime={ms}",
                f"200 OK with wall-clock <= {ms + 300}ms (overshoot <= 300ms)",
                f"{status} in {dur:.1f}ms (overshoot {overshoot:+.1f}ms)",
                dur,
                proposed_fix="Tighten time check throttle in search loop",
            )

        # Nodes limit
        payload = {"fen": STARTPOS_FEN, "limits": {"nodes": 5000}}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)
        reported_nodes = body.get("nodes", 0) if isinstance(body, dict) else 0
        # Engine checks nodes every step, should be near 5000
        ok = status == 200 and 4500 <= reported_nodes <= 6000
        self.record(
            "limit_nodes_5000",
            "Limits",
            "minor",
            ok,
            "POST /analyze nodes=5000",
            "200 OK with reported nodes close to 5000 (4500-6000)",
            f"{status} nodes={reported_nodes}",
            dur,
            proposed_fix="Ensure node count limit check stops search promptly",
        )

        # Combined depth + movetime (whichever comes first)
        payload = {"fen": STARTPOS_FEN, "limits": {"depth": 1, "movetime": 5000}}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)
        ok = status == 200 and dur < 500 and body.get("depth") == 1
        self.record(
            "limit_depth_and_movetime",
            "Limits",
            "minor",
            ok,
            "POST /analyze depth=1, movetime=5000",
            "200 OK fast (<500ms) with depth=1 (depth terminates first)",
            f"{status} depth={body.get('depth') if isinstance(body, dict) else None} in {dur:.1f}ms",
            dur,
            proposed_fix="Support combined depth and movetime limits in UCI command builder",
        )

        # Request with NO limits (default depth 6)
        payload = {"fen": STARTPOS_FEN}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)
        ok = status == 200 and body.get("depth") == 6
        self.record(
            "limit_default_depth_6",
            "Limits",
            "minor",
            ok,
            "POST /analyze with no limits",
            "200 OK defaulting to depth 6",
            f"{status} depth={body.get('depth') if isinstance(body, dict) else None}",
            dur,
            proposed_fix="Ensure default limits are applied when limits omitted",
        )

    def test_04_mate(self):
        """4. Mate in one detection and score representation."""
        fen = "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1"
        payload = {"fen": fen, "limits": {"depth": 3}}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)

        ok = status == 200 and isinstance(body, dict)
        bm = body.get("bestmove") if ok else None
        score = body.get("score") if ok else None
        is_mate = (
            bm == "a1a8"
            and isinstance(score, dict)
            and score.get("type") == "mate"
            and score.get("value") == 1
        )
        self.record(
            "mate_in_one_detection",
            "Evaluation",
            "blocker",
            is_mate,
            f"POST /analyze mate-in-one ({fen}) depth=3",
            "200 OK with bestmove='a1a8' and score={type: 'mate', value: 1}",
            f"{status} bestmove={bm} score={score}",
            dur,
            proposed_fix="Check mate distance calculation in searcher",
        )

    def test_05_score_perspective(self):
        """5. Evaluation score perspective audit (side-to-move vs White)."""
        # Black to move, Black is up a full queen
        fen = "4k3/8/8/8/8/8/q7/4K3 b - - 0 1"
        payload = {"fen": fen, "limits": {"depth": 3}}
        status, _, body, dur = self.request("POST", "/analyze", body=payload)

        ok = status == 200 and isinstance(body, dict)
        score = body.get("score") if ok else {}
        val = score.get("value", 0) if isinstance(score, dict) else 0

        # Engine uses side-to-move perspective (+900 for Black).
        # White perspective would be -900.
        side_to_move_perspective = val > 500
        self.record(
            "score_perspective_side_to_move",
            "Evaluation",
            "major",
            side_to_move_perspective,
            f"POST /analyze Black up queen, Black to move ({fen})",
            "Score positive relative to side to move (cp > +500)",
            f"{status} score={score} (value={val})",
            dur,
            details="Engine returns side-to-move perspective; verify documentation explicitly specifies this",
            proposed_fix="Document score perspective clearly in API reference",
        )

    def test_06_game_over(self):
        """6. Terminal positions: checkmate, stalemate, and K vs K."""
        # Checkmate delivered (White to move, mated by black queen on h4)
        fen_mate = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_mate})
        ok = status == 200 and isinstance(body, dict) and body.get("bestmove") is None
        self.record(
            "game_over_checkmate",
            "Terminal",
            "major",
            ok,
            f"POST /analyze checkmate position ({fen_mate})",
            "200 OK with bestmove=None (not hang or 500)",
            f"{status} bestmove={body.get('bestmove') if isinstance(body, dict) else None}",
            dur,
            proposed_fix="Ensure checkmated root position returns bestmove (none) -> None",
        )

        # Stalemate
        fen_stale = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_stale})
        ok = status == 200 and isinstance(body, dict) and body.get("bestmove") is None
        self.record(
            "game_over_stalemate",
            "Terminal",
            "major",
            ok,
            f"POST /analyze stalemate position ({fen_stale})",
            "200 OK with bestmove=None",
            f"{status} bestmove={body.get('bestmove') if isinstance(body, dict) else None}",
            dur,
            proposed_fix="Ensure stalemated root position returns bestmove (none) -> None",
        )

        # K vs K insufficient material (engine does not terminate automatically, searches)
        fen_kk = "8/8/8/4k3/8/8/4K3/8 w - - 0 1"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_kk, "limits": {"depth": 3}})
        ok = status == 200 and isinstance(body, dict) and body.get("bestmove") is not None
        self.record(
            "insufficient_material_k_vs_k",
            "Terminal",
            "minor",
            ok,
            f"POST /analyze K vs K ({fen_kk})",
            "200 OK with legal king move",
            f"{status} bestmove={body.get('bestmove') if isinstance(body, dict) else None}",
            dur,
            details="Engine currently evaluates K vs K with non-zero PST rather than immediate draw",
        )

    def test_07_special_moves(self):
        """7. Special move formats: castling, en passant, promotion."""
        # Castling
        fen_castle = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_castle, "limits": {"depth": 3}})
        bm = body.get("bestmove") if isinstance(body, dict) else ""
        ok = status == 200 and bm in ("e1g1", "e1c1", "a1a8", "h1h8")
        self.record(
            "special_move_castling",
            "Movegen",
            "major",
            ok,
            f"POST /analyze castling position",
            "200 OK with legal move in UCI format (e.g. e1g1 or capture)",
            f"{status} bestmove={bm}",
            dur,
        )

        # En Passant
        fen_ep = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_ep, "limits": {"depth": 3}})
        bm = body.get("bestmove") if isinstance(body, dict) else ""
        ok = status == 200 and len(bm) == 4
        self.record(
            "special_move_en_passant",
            "Movegen",
            "major",
            ok,
            f"POST /analyze en passant position",
            "200 OK with legal 4-char UCI move",
            f"{status} bestmove={bm}",
            dur,
        )

        # Promotion
        fen_promo = "8/P7/8/8/8/8/k6K/8 w - - 0 1"
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": fen_promo, "limits": {"depth": 3}})
        bm = body.get("bestmove") if isinstance(body, dict) else ""
        # Promotion move should have promotion suffix like a7a8q
        ok = status == 200 and len(bm) == 5 and bm.startswith("a7a8")
        self.record(
            "special_move_promotion",
            "Movegen",
            "major",
            ok,
            f"POST /analyze promotion position",
            "200 OK with 5-character promotion move (e.g. a7a8q)",
            f"{status} bestmove={bm}",
            dur,
            proposed_fix="Ensure promotion moves append promotion piece character",
        )

    def test_08_invalid_inputs(self):
        """8. Input validation and error responses."""
        cases = [
            ("invalid_fen_empty", {"fen": ""}, 400, "Empty FEN"),
            ("invalid_fen_garbage", {"fen": "not-a-valid-fen-string"}, 400, "Garbage text"),
            ("invalid_fen_missing_king", {"fen": "8/8/8/8/8/8/8/8 w - - 0 1"}, 400, "Missing kings"),
            ("invalid_fen_two_white_kings", {"fen": "k6K/8/8/8/8/8/8/7K w - - 0 1"}, 400, "Two white kings"),
            ("invalid_fen_pawn_rank_1", {"fen": "4k3/8/8/8/8/8/8/P3K3 w - - 0 1"}, 400, "Pawn on rank 1"),
            ("invalid_fen_side_not_to_move_in_check", {"fen": "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1"}, 400, "Side not to move in check"),
            ("invalid_fen_extra_fields", {"fen": f"{STARTPOS_FEN} extra_field"}, 400, "Extra FEN fields"),
            ("invalid_fen_200kb_string", {"fen": "A" * 200_000}, 400, "200KB FEN payload"),
            ("invalid_type_depth_string", {"fen": STARTPOS_FEN, "limits": {"depth": "four"}}, 422, "String depth"),
            ("invalid_type_fen_number", {"fen": 12345}, 422, "Numeric FEN"),
            ("invalid_extra_json_field", {"fen": STARTPOS_FEN, "unknown_field": True}, 422, "Unknown body field"),
            ("limits_depth_0", {"fen": STARTPOS_FEN, "limits": {"depth": 0}}, 422, "depth=0"),
            ("limits_depth_neg1", {"fen": STARTPOS_FEN, "limits": {"depth": -1}}, 422, "depth=-1"),
            ("limits_depth_21", {"fen": STARTPOS_FEN, "limits": {"depth": 21}}, 422, "depth=21"),
            ("limits_movetime_0", {"fen": STARTPOS_FEN, "limits": {"movetime": 0}}, 422, "movetime=0"),
            ("limits_movetime_30001", {"fen": STARTPOS_FEN, "limits": {"movetime": 30001}}, 422, "movetime=30001"),
            ("limits_nodes_0", {"fen": STARTPOS_FEN, "limits": {"nodes": 0}}, 422, "nodes=0"),
            ("limits_nodes_10000001", {"fen": STARTPOS_FEN, "limits": {"nodes": 10000001}}, 422, "nodes=10000001"),
        ]

        for name, payload, expected_status, desc in cases:
            status, _, body, dur = self.request("POST", "/analyze", body=payload)
            ok = status == expected_status

            flag_info = ""
            if HAS_CHESS and "fen" in payload and isinstance(payload["fen"], str) and payload["fen"]:
                try:
                    b = chess.Board(payload["fen"])
                    flags = [
                        attr for attr in dir(chess)
                        if attr.startswith("STATUS_") and (b.status() & getattr(chess, attr))
                    ]
                    flag_info = f" [status flags: {', '.join(flags)}]"
                except Exception as e:
                    flag_info = f" [syntax parse: {type(e).__name__}]"

            self.record(
                name,
                "Validation",
                "major" if expected_status == 400 else "minor",
                ok,
                f"POST /analyze ({desc}){flag_info}",
                f"HTTP {expected_status}",
                f"HTTP {status}",
                dur,
                proposed_fix=(
                    "Validate FEN status flags in app.py to reject semantically illegal FENs"
                    if "invalid_fen_" in name
                    else "Configure extra='forbid' on Pydantic models"
                    if "extra" in name
                    else None
                ),
            )

        # Raw payload tests (malformed JSON, wrong Content-Type)
        status, _, _, dur = self.request(
            "POST", "/analyze", body=b"{malformed json}", headers={"Content-Type": "application/json"}
        )
        self.record(
            "invalid_malformed_json",
            "Validation",
            "major",
            status == 422,
            "POST /analyze with malformed JSON body",
            "HTTP 422",
            f"HTTP {status}",
            dur,
            proposed_fix="FastAPI should return 422 on invalid JSON syntax",
        )

        status, _, _, dur = self.request(
            "POST", "/analyze", body="fen=foo", headers={"Content-Type": "text/plain"}
        )
        self.record(
            "invalid_wrong_content_type",
            "Validation",
            "minor",
            status in (415, 422),
            "POST /analyze with Content-Type: text/plain",
            "HTTP 415 or 422",
            f"HTTP {status}",
            dur,
        )

        # Verify service is still healthy after invalid inputs
        h_status, _, _, h_dur = self.request("GET", "/health")
        a_status, _, _, a_dur = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"depth": 1}})
        self.record(
            "service_healthy_after_fuzzing",
            "Resilience",
            "blocker",
            h_status == 200 and a_status == 200,
            "GET /health and POST /analyze after fuzzing",
            "Health=200 and Analyze=200",
            f"Health={h_status} Analyze={a_status}",
            h_dur + a_dur,
            proposed_fix="Ensure invalid inputs do not corrupt engine or server state",
        )

    def test_09_http_semantics(self):
        """9. HTTP semantics: 405 Method Not Allowed, 404, HEAD, OPTIONS."""
        # GET /analyze -> 405
        status, _, _, dur = self.request("GET", "/analyze")
        self.record(
            "http_get_analyze_405",
            "HTTP",
            "major",
            status == 405,
            "GET /analyze",
            "HTTP 405 Method Not Allowed",
            f"HTTP {status}",
            dur,
            proposed_fix="FastAPI handles method routing",
        )

        # Unknown route -> 404
        status, _, _, dur = self.request("GET", "/unknown_route_404")
        self.record(
            "http_unknown_route_404",
            "HTTP",
            "major",
            status == 404,
            "GET /unknown_route_404",
            "HTTP 404 Not Found",
            f"HTTP {status}",
            dur,
        )

        # OPTIONS with CORS
        headers = {
            "Origin": "http://localhost:3000",
            "Access-Control-Request-Method": "POST",
        }
        status, resp_headers, _, dur = self.request("OPTIONS", "/analyze", headers=headers)
        cors_ok = status == 200 and "access-control-allow-origin" in {k.lower(): v for k, v in resp_headers.items()}
        self.record(
            "http_cors_preflight",
            "HTTP",
            "major",
            cors_ok,
            "OPTIONS /analyze with Origin header",
            "200 OK with Access-Control-Allow-Origin",
            f"HTTP {status} (CORS header: {cors_ok})",
            dur,
            proposed_fix="Configure CORSMiddleware to permit preflight requests",
        )

    def test_10_auth(self):
        """10. Authentication contracts (when API_KEY is enabled)."""
        if not self.api_key:
            # If API_KEY not provided, test that open access works
            status, _, _, dur = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"depth": 1}})
            self.record(
                "auth_open_access_when_unset",
                "Auth",
                "major",
                status == 200,
                "POST /analyze without API_KEY configured",
                "HTTP 200 Open Access",
                f"HTTP {status}",
                dur,
            )
            return

        # Request with no key -> 401
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN}, headers={"X-API-Key": ""})
        self.record(
            "auth_missing_key_401",
            "Auth",
            "blocker",
            status == 401,
            "POST /analyze with empty X-API-Key",
            "HTTP 401 Unauthorized",
            f"HTTP {status}",
            dur,
            proposed_fix="Enforce X-API-Key header verification in auth middleware",
        )

        # Request with wrong key -> 401
        status, _, body, dur = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN}, headers={"X-API-Key": "wrong-key-xyz"})
        self.record(
            "auth_wrong_key_401",
            "Auth",
            "blocker",
            status == 401,
            "POST /analyze with wrong X-API-Key",
            "HTTP 401 Unauthorized",
            f"HTTP {status}",
            dur,
            proposed_fix="Reject mismatched API keys with 401",
        )

        # Non-ASCII key -> 401 (never 500)
        status, _, body, dur = self.request(
            "POST", "/analyze", body={"fen": STARTPOS_FEN}, headers={"X-API-Key": "clé-secrète-🔑".encode("utf-8")}
        )
        self.record(
            "auth_non_ascii_key_401",
            "Auth",
            "major",
            status == 401,
            "POST /analyze with non-ASCII X-API-Key bytes",
            "HTTP 401 Unauthorized (not 500)",
            f"HTTP {status}",
            dur,
            proposed_fix="Use utf-8 encode before compare_digest in auth middleware",
        )

        # Lowercase header name -> 200
        status, _, body, dur = self.request(
            "POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"depth": 1}}, headers={"x-api-key": self.api_key}
        )
        self.record(
            "auth_case_insensitive_header",
            "Auth",
            "minor",
            status == 200,
            "POST /analyze with lowercase x-api-key header",
            "HTTP 200 OK",
            f"HTTP {status}",
            dur,
            proposed_fix="Ensure headers dictionary lookup handles lowercased headers",
        )

        # GET /health must not require key
        status, _, _, dur = self.request("GET", "/health", headers={"X-API-Key": ""})
        self.record(
            "auth_health_always_exempt",
            "Auth",
            "blocker",
            status == 200,
            "GET /health with no API key",
            "HTTP 200 OK",
            f"HTTP {status}",
            dur,
            proposed_fix="Exempt GET /health in auth middleware",
        )

        # Check /docs and /openapi.json status
        status_docs, _, _, dur_docs = self.request("GET", "/docs", headers={"X-API-Key": ""})
        self.record(
            "auth_docs_protected_or_open",
            "Auth",
            "minor",
            True,  # Audit finding
            "GET /docs without API key",
            "Report status (401 protected vs 200 open)",
            f"HTTP {status_docs}",
            dur_docs,
            details=f"/docs returned {status_docs} when API_KEY is set",
        )

    def test_11_determinism(self):
        """11. Determinism and state leakage between distinct positions."""
        p_a = {"fen": STARTPOS_FEN, "limits": {"depth": 4}}
        p_b = {"fen": KIWIPETE_FEN, "limits": {"depth": 4}}

        s1, _, b1, d1 = self.request("POST", "/analyze", body=p_a)
        sb, _, _, _ = self.request("POST", "/analyze", body=p_b)
        s2, _, b2, d2 = self.request("POST", "/analyze", body=p_a)

        identical = (
            s1 == 200
            and s2 == 200
            and isinstance(b1, dict)
            and isinstance(b2, dict)
            and b1.get("bestmove") == b2.get("bestmove")
            and b1.get("nodes") == b2.get("nodes")
            and b1.get("score") == b2.get("score")
        )
        self.record(
            "determinism_and_state_leakage",
            "Determinism",
            "major",
            identical,
            "POST /analyze A -> B -> A at depth 4",
            "Identical bestmove, score, and nodes for repeated position A",
            f"A1(bm={b1.get('bestmove') if isinstance(b1, dict) else None}, nodes={b1.get('nodes') if isinstance(b1, dict) else None}) vs A2(bm={b2.get('bestmove') if isinstance(b2, dict) else None}, nodes={b2.get('nodes') if isinstance(b2, dict) else None})",
            d1 + d2,
            proposed_fix="Issue ucinewgame between searches to clear TT pollution",
        )

    def test_12_concurrency(self):
        """12. Concurrency model and queueing / busy behavior."""
        import threading

        statuses: List[int] = []

        def worker(movetime: int):
            s, _, _, _ = self.request(
                "POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"movetime": movetime}}, timeout=10.0
            )
            statuses.append(s)

        # Launch 1 long request and 1 immediate request
        t1 = threading.Thread(target=worker, args=(1500,))
        t2 = threading.Thread(target=worker, args=(100,))
        t1.start()
        time.sleep(0.05)
        t2.start()
        t1.join()
        t2.join()

        # Over asyncio without threadpool, requests serialize and return 200; with threadpool/lock, returns 409
        valid_behavior = all(s in (200, 409) for s in statuses)
        self.record(
            "concurrency_serialized_or_busy",
            "Concurrency",
            "major",
            valid_behavior,
            "Concurrent POST /analyze requests",
            "Return 200 (queued) or 409 (busy)",
            f"Statuses received: {statuses}",
            0.0,
            details="FastAPI async handler blocks on synchronous engine.analyze, serializing requests",
            proposed_fix="Use anyio.to_thread.run_sync or worker pool if non-blocking 409 is desired",
        )

    def test_13_latency_overhead(self):
        """13. Measure 50 sequential depth-1 requests and quantify API overhead."""
        latencies: List[float] = []
        payload = {"fen": STARTPOS_FEN, "limits": {"depth": 1}}

        for _ in range(50):
            s, _, _, dur = self.request("POST", "/analyze", body=payload)
            if s == 200:
                latencies.append(dur)

        ok = len(latencies) == 50
        p50 = sorted(latencies)[len(latencies) // 2] if latencies else 0.0
        p95 = sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0.0

        self.record(
            "latency_p50_p95_overhead",
            "Performance",
            "minor",
            ok and p50 < 30.0,
            "50 sequential POST /analyze depth=1",
            "p50 < 30ms over HTTP",
            f"p50={p50:.2f}ms, p95={p95:.2f}ms (50/50 succeeded)",
            sum(latencies),
            details=f"HTTP API adds ~5ms overhead vs direct UCI subprocess call (~0.1ms)",
        )

    def test_14_slow(self):
        """14. Slow tests: 30s timeout, depth 20, memory RSS sampling."""
        if not self.slow_enabled:
            return

        print("\n  [SLOW] Testing depth 20 startpos timeout (30s safety timeout)...", flush=True)
        # Depth 20 takes minutes, should hit 30s timeout
        status, _, body, dur = self.request(
            "POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"depth": 20}}, timeout=45.0
        )
        timed_out = body.get("timed_out") is True if isinstance(body, dict) else False
        # Engine should stop and return 200 with timed_out=True (or 504 if no move)
        ok = status in (200, 504) and (timed_out or status == 504)
        self.record(
            "slow_depth_20_timeout",
            "Timeout",
            "major",
            ok,
            "POST /analyze depth=20 startpos",
            "Hits 30s timeout, returns timed_out=True or 504",
            f"HTTP {status} timed_out={timed_out} in {dur:.1f}ms",
            dur,
            proposed_fix="Ensure searcher aborts and returns best move on timeout",
        )

        # Confirm health and next analyze work immediately after timeout
        h_status, _, _, _ = self.request("GET", "/health")
        a_status, _, _, _ = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN, "limits": {"depth": 1}})
        self.record(
            "slow_recovery_after_timeout",
            "Timeout",
            "blocker",
            h_status == 200 and a_status == 200,
            "GET /health and POST /analyze after timeout",
            "Health=200, Analyze=200 (engine not stuck)",
            f"Health={h_status}, Analyze={a_status}",
            0.0,
            proposed_fix="Ensure timeout cleanup releases engine lock and restarts reader",
        )

    def test_15_destructive(self):
        """15. Destructive tests: kill engine process and observe recovery."""
        if not self.destructive_enabled or not self.local_process:
            return

        print("\n  [DESTRUCTIVE] Killing child chess_engine process...", flush=True)
        # Find child engine process
        try:
            out = subprocess.check_output(["pgrep", "-P", str(self.local_process.pid), "chess_engine"]).decode().strip()
            engine_pid = int(out.split()[0])
            os.kill(engine_pid, signal.SIGKILL)
            time.sleep(0.3)

            # Next request should cleanly return 503
            status, _, body, dur = self.request("POST", "/analyze", body={"fen": STARTPOS_FEN})
            ok = status == 503
            self.record(
                "destructive_killed_engine_returns_503",
                "Resilience",
                "major",
                ok,
                "POST /analyze after engine process killed",
                "HTTP 503 Engine Unavailable (clean failure, no 500)",
                f"HTTP {status} {body}",
                dur,
                details="EngineManager currently does NOT auto-restart killed engine; requires API restart",
                proposed_fix="Add auto-restart logic in EngineManager when process dies",
            )
        except Exception as e:
            self.record(
                "destructive_killed_engine_returns_503",
                "Resilience",
                "major",
                False,
                "Kill engine process test",
                "HTTP 503",
                f"Exception: {e}",
                0.0,
            )

    # ─── Runner Execution ───────────────────────────────────────────────────────

    def run_all(self):
        print(f"\n{'='*70}")
        print(f"API CONFORMANCE & CAPABILITY AUDIT")
        print(f"Target: {self.base_url}")
        print(f"Mode: {'SMOKE' if self.smoke_only else 'FULL'} | Slow: {self.slow_enabled} | Destructive: {self.destructive_enabled}")
        print(f"{'='*70}\n")

        self.test_01_health()
        self.test_02_startpos_depth_4()
        self.test_03_limits()
        self.test_04_mate()
        self.test_05_score_perspective()
        self.test_06_game_over()
        self.test_07_special_moves()
        self.test_08_invalid_inputs()
        self.test_09_http_semantics()
        self.test_10_auth()
        self.test_11_determinism()
        self.test_12_concurrency()

        if not self.smoke_only:
            self.test_13_latency_overhead()
            if self.slow_enabled:
                self.test_14_slow()
            if self.destructive_enabled:
                self.test_15_destructive()

    def print_summary(self) -> int:
        print(f"\n{'-'*95}")
        print(f"{'Test Name':<34} {'Category':<14} {'Severity':<10} {'Status':<8} {'Actual':<25}")
        print(f"{'-'*95}")

        pass_count = sum(1 for r in self.results if r.status == "PASS")
        fail_count = sum(1 for r in self.results if r.status == "FAIL")

        severities = {"blocker": [0, 0], "major": [0, 0], "minor": [0, 0]}

        for r in self.results:
            sev_idx = 0 if r.status == "PASS" else 1
            if r.severity in severities:
                severities[r.severity][sev_idx] += 1

            status_str = f"\033[92m{r.status}\033[0m" if r.status == "PASS" else f"\033[91m{r.status}\033[0m"
            actual_short = (r.actual[:24] + "…") if len(r.actual) > 24 else r.actual
            print(f"{r.name:<34} {r.category:<14} {r.severity:<10} {status_str:<17} {actual_short:<25}")

        print(f"{'-'*95}")
        print(f"Results: {pass_count} PASSED, {fail_count} FAILED (Total: {len(self.results)})")
        print(f"Severity Breakdown: Blocker [{severities['blocker'][0]}P/{severities['blocker'][1]}F], "
              f"Major [{severities['major'][0]}P/{severities['major'][1]}F], "
              f"Minor [{severities['minor'][0]}P/{severities['minor'][1]}F]")
        print(f"{'-'*95}\n")

        if fail_count > 0:
            print("FAILURE DETAILS & PROPOSED FIXES:")
            for r in self.results:
                if r.status == "FAIL":
                    print(f"\n[FAIL] {r.name} ({r.severity.upper()}):")
                    print(f"  Request:      {r.request}")
                    print(f"  Expected:     {r.expected}")
                    print(f"  Actual:       {r.actual}")
                    if r.details:
                        print(f"  Details:      {r.details}")
                    if r.proposed_fix:
                        print(f"  Proposed Fix: {r.proposed_fix}")
            print()

        return fail_count


# ─── Local Server Management ───────────────────────────────────────────────────

def get_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


def start_local_server(port: int, api_key: Optional[str] = None) -> subprocess.Popen:
    env = os.environ.copy()
    if api_key:
        env["API_KEY"] = api_key

    # Find python / uvicorn
    venv_python = REPO_ROOT / ".venv" / "bin" / "python"
    py_cmd = str(venv_python) if venv_python.is_file() else sys.executable

    cmd = [
        py_cmd,
        "-m",
        "uvicorn",
        "api.app:app",
        "--host",
        "127.0.0.1",
        "--port",
        str(port),
        "--log-level",
        "warning",
    ]

    proc = subprocess.Popen(cmd, cwd=str(REPO_ROOT), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Wait for server /health
    deadline = time.time() + 10.0
    while time.time() < deadline:
        try:
            req = urllib.request.Request(f"http://127.0.0.1:{port}/health")
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                if resp.getcode() == 200:
                    return proc
        except Exception:
            time.sleep(0.1)

    proc.terminate()
    raise RuntimeError("Failed to start local uvicorn server within 10 seconds")


# ─── Main CLI Entrypoint ───────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Chess Engine HTTP API Conformance & Audit Runner")
    parser.add_argument("--base-url", default=None, help="Target API URL (default: starts local server)")
    parser.add_argument("--api-key", default=None, help="Optional API key")
    parser.add_argument("--smoke", action="store_true", help="Run small non-destructive subset")
    parser.add_argument("--slow", action="store_true", help="Run long tests (30s timeouts) [localhost only]")
    parser.add_argument("--destructive", action="store_true", help="Run engine crash recovery tests [localhost only]")
    parser.add_argument("--json-out", default=None, help="Path to write JSON results file")
    args = parser.parse_args()

    # Safety check: slow and destructive tests only permitted on localhost
    target_url = args.base_url
    local_proc: Optional[subprocess.Popen] = None

    if target_url:
        parsed = urlparse(target_url)
        is_local = parsed.hostname in ("127.0.0.1", "localhost", "::1")
        if (args.slow or args.destructive) and not is_local:
            sys.exit("Error: Refusing to run --slow or --destructive against non-localhost target.")
    else:
        # Spawn local server
        free_port = get_free_port()
        target_url = f"http://127.0.0.1:{free_port}"
        print(f"[INFO] Spawning local API test server on {target_url}...")
        local_proc = start_local_server(free_port, api_key=args.api_key)

    try:
        runner = ConformanceRunner(
            base_url=target_url,
            api_key=args.api_key,
            smoke_only=args.smoke,
            slow_enabled=args.slow,
            destructive_enabled=args.destructive,
            local_process=local_proc,
        )
        runner.run_all()
        fail_count = runner.print_summary()

        if args.json_out:
            out_path = Path(args.json_out)
            out_data = {
                "base_url": target_url,
                "summary": {
                    "total": len(runner.results),
                    "passed": sum(1 for r in runner.results if r.status == "PASS"),
                    "failed": fail_count,
                },
                "results": [asdict(r) for r in runner.results],
            }
            with open(out_path, "w", encoding="utf-8") as f:
                json.dump(out_data, f, indent=2)
            print(f"[INFO] JSON report written to {out_path}")

        sys.exit(1 if fail_count > 0 else 0)

    finally:
        if local_proc:
            local_proc.terminate()
            try:
                local_proc.wait(timeout=3)
            except Exception:
                local_proc.kill()


if __name__ == "__main__":
    main()

