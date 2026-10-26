#!/usr/bin/env python3
"""
Unit and integration tests for API authentication and endpoints.
"""

import os
import sys
import warnings
from pathlib import Path

# Suppress Starlette/httpx deprecation warnings
warnings.filterwarnings("ignore")

# Ensure repo root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

# If starlette/fastapi dependencies are in project .venv, re-exec under .venv python
try:
    import starlette
except ModuleNotFoundError:
    venv_py = REPO_ROOT / ".venv" / "bin" / "python"
    if venv_py.is_file() and sys.executable != str(venv_py):
        import subprocess
        sys.exit(subprocess.run([str(venv_py)] + sys.argv).returncode)
    raise

from starlette.testclient import TestClient
from api.app import app

TEST_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"


def test_health_no_auth_required(client: TestClient):
    if "API_KEY" in os.environ:
        del os.environ["API_KEY"]
    resp = client.get("/health")
    assert resp.status_code == 200
    assert resp.json().get("status") == "ok"


def test_api_key_not_set_allows_analyze(client: TestClient):
    if "API_KEY" in os.environ:
        del os.environ["API_KEY"]
    resp = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
    assert resp.status_code == 200
    assert resp.json().get("success") is True
    assert resp.json().get("bestmove") is not None


def test_api_key_set_rejects_without_key(client: TestClient):
    os.environ["API_KEY"] = "super-secret-key-42"
    try:
        resp = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
        assert resp.status_code == 401
        data = resp.json()
        assert data.get("error") == "Unauthorized"
    finally:
        del os.environ["API_KEY"]


def test_api_key_set_rejects_with_wrong_key(client: TestClient):
    os.environ["API_KEY"] = "super-secret-key-42"
    try:
        resp = client.post(
            "/analyze",
            headers={"X-API-Key": "incorrect-key"},
            json={"fen": TEST_FEN, "limits": {"depth": 1}},
        )
        assert resp.status_code == 401
        assert resp.json().get("error") == "Unauthorized"
    finally:
        del os.environ["API_KEY"]


def test_api_key_set_rejects_with_non_ascii_key(client: TestClient):
    os.environ["API_KEY"] = "super-secret-key-42"
    try:
        resp = client.post(
            "/analyze",
            headers={"X-API-Key": "clé-secrète-🔑-неверный-ключ".encode("utf-8")},
            json={"fen": TEST_FEN, "limits": {"depth": 1}},
        )
        assert resp.status_code == 401
        assert resp.json().get("error") == "Unauthorized"
    finally:
        del os.environ["API_KEY"]


def test_api_key_set_accepts_with_valid_key(client: TestClient):
    os.environ["API_KEY"] = "super-secret-key-42"
    try:
        resp = client.post(
            "/analyze",
            headers={"X-API-Key": "super-secret-key-42"},
            json={"fen": TEST_FEN, "limits": {"depth": 1}},
        )
        assert resp.status_code == 200
        assert resp.json().get("success") is True
        assert resp.json().get("bestmove") is not None
    finally:
        del os.environ["API_KEY"]


def test_health_succeeds_even_when_api_key_set(client: TestClient):
    os.environ["API_KEY"] = "super-secret-key-42"
    try:
        resp = client.get("/health")
        assert resp.status_code == 200
        assert resp.json().get("status") == "ok"
    finally:
        del os.environ["API_KEY"]


def test_fen_validation_rejects_hard_errors(client: TestClient):
    invalid_cases = [
        ("8/8/8/8/8/8/8/8 w - - 0 1", "Empty board"),
        ("8/8/8/8/8/8/8/4k3 w - - 0 1", "Missing white king"),
        ("8/8/8/8/8/8/8/4K3 w - - 0 1", "Missing black king"),
        ("k6K/8/8/8/8/8/8/7K w - - 0 1", "Too many kings"),
        ("4k3/8/8/8/8/8/8/P3K3 w - - 0 1", "Pawns on first or last rank"),
        ("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", "Opposite side in check"),
        ("PPPPPPPP/PPPPPP2/4k3/8/8/8/8/4K3 w - - 0 1", "Too many white pawns"),
    ]
    for fen, expected_err in invalid_cases:
        resp = client.post("/analyze", json={"fen": fen, "limits": {"depth": 1}})
        assert resp.status_code == 400, f"Expected 400 for {fen}, got {resp.status_code}"
        data = resp.json()
        detail_str = str(data.get("detail", ""))
        assert expected_err.lower() in detail_str.lower(), f"Expected '{expected_err}' in detail for {fen}, got: {detail_str}"


def test_fen_validation_accepts_valid_and_tolerated_fens(client: TestClient):
    valid_fens = [
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  # startpos
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",  # Kiwipete
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",  # bench 1
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",  # bench 2
        "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",  # bench 3
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",  # bench 4
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",  # bench 5
        "4k3/8/8/8/8/8/8/4K2Q w - - 0 1",  # promoted queen
        "4k3/8/8/8/8/8/8/4K3 w - - 0 1",  # "-" castling and en passant
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",  # inconsistent castling rights (tolerated)
    ]
    for fen in valid_fens:
        resp = client.post("/analyze", json={"fen": fen, "limits": {"depth": 1}})
        assert resp.status_code == 200, f"Expected 200 for {fen}, got {resp.status_code}: {resp.text}"


def test_error_schema_consistent_shape(client: TestClient):
    """Verify that every error code (400, 401, 404, 405, 409, 422, 503, 504, 500) returns top-level shape."""
    from unittest.mock import patch
    from api.engine_manager import EngineBusy, EngineNotReady

    def check_shape(resp, expected_code):
        assert resp.status_code == expected_code, f"Expected {expected_code}, got {resp.status_code}"
        data = resp.json()
        assert isinstance(data, dict), f"Body not dict: {data}"
        assert data.get("success") is False, f"Expected success=False: {data}"
        assert isinstance(data.get("error"), str) and len(data["error"]) > 0, f"Expected non-empty error string: {data}"
        assert "detail" in data, f"Missing detail: {data}"

    # 400
    check_shape(client.post("/analyze", json={"fen": "not_valid"}), 400)

    # 401
    os.environ["API_KEY"] = "secret42"
    try:
        check_shape(client.post("/analyze", json={"fen": TEST_FEN}), 401)
    finally:
        del os.environ["API_KEY"]

    # 404
    check_shape(client.get("/nonexistent_endpoint"), 404)

    # 405
    check_shape(client.get("/analyze"), 405)

    # 409
    with patch("api.app.engine.analyze", side_effect=EngineBusy("Busy test")):
        check_shape(client.post("/analyze", json={"fen": TEST_FEN}), 409)

    # 422
    check_shape(client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 99}}), 422)

    # 503
    with patch("api.app.engine.analyze", side_effect=EngineNotReady("Unavailable test")):
        check_shape(client.post("/analyze", json={"fen": TEST_FEN}), 503)

    # 504
    with patch("api.app.engine.analyze", return_value={"timed_out": True, "bestmove": "(none)"}):
        check_shape(client.post("/analyze", json={"fen": TEST_FEN}), 504)

    # 500
    test_client_no_raise = TestClient(app, raise_server_exceptions=False)
    with patch("api.app.engine.analyze", side_effect=RuntimeError("Unhandled boom")):
        check_shape(test_client_no_raise.post("/analyze", json={"fen": TEST_FEN}), 500)


ALL_TESTS = [
    ("API.HealthNoAuthRequired", test_health_no_auth_required),
    ("API.ApiKeyNotSetAllowsAnalyze", test_api_key_not_set_allows_analyze),
    ("API.ApiKeySetRejectsWithoutKey", test_api_key_set_rejects_without_key),
    ("API.ApiKeySetRejectsWithWrongKey", test_api_key_set_rejects_with_wrong_key),
    ("API.ApiKeySetRejectsWithNonAsciiKey", test_api_key_set_rejects_with_non_ascii_key),
    ("API.ApiKeySetAcceptsWithValidKey", test_api_key_set_accepts_with_valid_key),
    ("API.HealthSucceedsEvenWhenApiKeySet", test_health_succeeds_even_when_api_key_set),
    ("API.FenValidationRejectsHardErrors", test_fen_validation_rejects_hard_errors),
    ("API.FenValidationAcceptsValidAndToleratedFens", test_fen_validation_accepts_valid_and_tolerated_fens),
    ("API.ErrorSchemaConsistentShape", test_error_schema_consistent_shape),
]


def main():
    if "--gtest_list_tests" in sys.argv or "--list-tests" in sys.argv:
        print("API.")
        for name, _ in ALL_TESTS:
            test_subname = name.split(".", 1)[1]
            print(f"  {test_subname}")
        sys.exit(0)

    passed = 0
    failed = 0
    with TestClient(app) as client:
        for name, fn in ALL_TESTS:
            print(f"[ RUN  ] {name}")
            try:
                fn(client)
                print(f"[ PASS ] {name}")
                passed += 1
            except Exception as e:
                print(f"[ FAIL ] {name}: {e}")
                failed += 1

    print(f"\n=== Results: {passed} passed, {failed} failed ===")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()

