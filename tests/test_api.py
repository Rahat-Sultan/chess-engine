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
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  # bench 1: startpos
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",  # bench 2: Kiwipete
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",  # bench 3: endgame
        "r1b1k1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",  # bench 4: tactical
        "r2q1rk1/ppp2ppp/2n1bn2/1B1p4/3P4/1Q3N2/PP3PPP/RNB1K2R w KQ - 0 1",  # bench 5: middlegame
        "r3k2r/p1pp1pp1/bn2pnp1/3P4/1p2P3/2N5/PP3PPP/R1BQK1NR w KQkq - 0 1",  # bench 6: forks
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


def test_health_answers_immediately_during_search(client: TestClient):
    import threading
    import time

    health_result = {}

    def run_long():
        client.post("/analyze", json={"fen": TEST_FEN, "limits": {"movetime": 500}})

    def run_health():
        time.sleep(0.05)
        t0 = time.time()
        r = client.get("/health")
        health_result["status"] = r.status_code
        health_result["duration_ms"] = (time.time() - t0) * 1000

    t1 = threading.Thread(target=run_long)
    t2 = threading.Thread(target=run_health)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    assert health_result.get("status") == 200
    assert health_result.get("duration_ms", 9999) < 250, f"Health took too long: {health_result}"


def test_concurrent_search_returns_409_immediately(client: TestClient):
    import threading
    import time

    second_result = {}

    def run_long():
        client.post("/analyze", json={"fen": TEST_FEN, "limits": {"movetime": 500}})

    def run_second():
        time.sleep(0.05)
        t0 = time.time()
        r = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
        second_result["status"] = r.status_code
        second_result["duration_ms"] = (time.time() - t0) * 1000

    t1 = threading.Thread(target=run_long)
    t2 = threading.Thread(target=run_second)
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    assert second_result.get("status") == 409
    assert second_result.get("duration_ms", 9999) < 250, f"Second analyze took too long: {second_result}"


def test_engine_auto_recovery_kill_idle(client: TestClient):
    import os
    import signal
    import time
    from api.app import engine

    # Kill idle engine
    if engine.process and engine.process.poll() is None:
        os.kill(engine.process.pid, signal.SIGKILL)
        time.sleep(0.1)

    # Health while down reports ready=False
    h_down = client.get("/health")
    assert h_down.status_code == 200 and h_down.json().get("ready") is False

    # Next analyze auto-recovers and returns 200
    a_rec = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
    assert a_rec.status_code == 200 and a_rec.json().get("bestmove") is not None

    # Health after recovery reports ready=True
    h_up = client.get("/health")
    assert h_up.status_code == 200 and h_up.json().get("ready") is True


def test_engine_auto_recovery_kill_mid_search(client: TestClient):
    import os
    import signal
    import threading
    import time
    from api.app import engine

    # Start search and kill mid-search
    def kill_target():
        time.sleep(0.1)
        if engine.process and engine.process.poll() is None:
            try:
                os.kill(engine.process.pid, signal.SIGKILL)
            except Exception:
                pass

    t = threading.Thread(target=kill_target)
    t.start()
    s_mid = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"movetime": 5000}})
    t.join()
    assert s_mid.status_code == 503

    # Next request recovers and succeeds with 200
    s_rec = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
    assert s_rec.status_code == 200 and s_rec.json().get("bestmove") is not None


def test_engine_no_stale_move_after_new_position(client: TestClient):
    # Analyze position A (Kiwipete)
    r_k = client.post("/analyze", json={"fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "limits": {"depth": 1}})
    assert r_k.status_code == 200
    bm_k = r_k.json().get("bestmove")

    # Analyze position B (startpos)
    r_s = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 1}})
    assert r_s.status_code == 200
    bm_s = r_s.json().get("bestmove")

    assert bm_k is not None and bm_s is not None
    assert bm_s != bm_k or bm_s in ("e2e4", "d2d4", "g1f3", "b1c3", "c2c4")


def test_mate_in_one_returns_score_mate_1(client: TestClient):
    # Scholar's mate delivery position: White has Qxf7#
    fen_mate1 = "r1bqkb1r/pppp1ppp/2n5/4p2Q/2B1n3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 4"
    resp = client.post("/analyze", json={"fen": fen_mate1, "limits": {"depth": 3}})
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("bestmove") == "h5f7"
    score = data.get("score")
    assert isinstance(score, dict)
    assert score.get("type") == "mate"
    assert score.get("value") == 1


def test_mate_in_two_returns_score_mate_2(client: TestClient):
    # Légal's Trap: 1. Nf6+! gxf6 2. Bxf7#
    fen_mate2 = "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2QK2R w KQkq - 1 10"
    resp = client.post("/analyze", json={"fen": fen_mate2, "limits": {"depth": 4}})
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("bestmove") == "d5f6"
    score = data.get("score")
    assert isinstance(score, dict)
    assert score.get("type") == "mate"
    assert score.get("value") == 2


def test_side_being_mated_returns_negative_mate(client: TestClient):
    # Black is being mated in 1 move (White will play Qb7# next turn)
    fen_being_mated = "k7/2K4p/1Q6/8/8/8/8/8 b - - 0 1"
    resp = client.post("/analyze", json={"fen": fen_being_mated, "limits": {"depth": 3}})
    assert resp.status_code == 200
    data = resp.json()
    score = data.get("score")
    assert isinstance(score, dict)
    assert score.get("type") == "mate"
    assert score.get("value") == -1


def test_search_caps_configurable_via_env(client: TestClient):
    from api.app import MAX_DEPTH, MAX_MOVETIME_MS, MAX_NODES, REQUEST_TIMEOUT_SEC

    # Defaults
    assert MAX_DEPTH == 20
    assert MAX_MOVETIME_MS == 30000
    assert MAX_NODES == 10000000
    assert REQUEST_TIMEOUT_SEC == 30.0

    # Lower MAX_DEPTH to 10
    os.environ["MAX_DEPTH"] = "10"
    try:
        r_over = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 12}})
        assert r_over.status_code == 422
        r_ok = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"depth": 2}})
        assert r_ok.status_code == 200
    finally:
        del os.environ["MAX_DEPTH"]

    # Lower MAX_MOVETIME_MS to 5000
    os.environ["MAX_MOVETIME_MS"] = "5000"
    try:
        r_over = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"movetime": 6000}})
        assert r_over.status_code == 422
    finally:
        del os.environ["MAX_MOVETIME_MS"]

    # Lower MAX_NODES to 1000
    os.environ["MAX_NODES"] = "1000"
    try:
        r_over = client.post("/analyze", json={"fen": TEST_FEN, "limits": {"nodes": 2000}})
        assert r_over.status_code == 422
    finally:
        del os.environ["MAX_NODES"]


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
    ("API.HealthAnswersImmediatelyDuringSearch", test_health_answers_immediately_during_search),
    ("API.ConcurrentSearchReturns409Immediately", test_concurrent_search_returns_409_immediately),
    ("API.EngineAutoRecoveryKillIdle", test_engine_auto_recovery_kill_idle),
    ("API.EngineAutoRecoveryKillMidSearch", test_engine_auto_recovery_kill_mid_search),
    ("API.EngineNoStaleMoveAfterNewPosition", test_engine_no_stale_move_after_new_position),
    ("API.MateInOneReturnsScoreMate1", test_mate_in_one_returns_score_mate_1),
    ("API.MateInTwoReturnsScoreMate2", test_mate_in_two_returns_score_mate_2),
    ("API.SideBeingMatedReturnsNegativeMate", test_side_being_mated_returns_negative_mate),
    ("API.SearchCapsConfigurableViaEnv", test_search_caps_configurable_via_env),
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

