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


ALL_TESTS = [
    ("API.HealthNoAuthRequired", test_health_no_auth_required),
    ("API.ApiKeyNotSetAllowsAnalyze", test_api_key_not_set_allows_analyze),
    ("API.ApiKeySetRejectsWithoutKey", test_api_key_set_rejects_without_key),
    ("API.ApiKeySetRejectsWithWrongKey", test_api_key_set_rejects_with_wrong_key),
    ("API.ApiKeySetRejectsWithNonAsciiKey", test_api_key_set_rejects_with_non_ascii_key),
    ("API.ApiKeySetAcceptsWithValidKey", test_api_key_set_accepts_with_valid_key),
    ("API.HealthSucceedsEvenWhenApiKeySet", test_health_succeeds_even_when_api_key_set),
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

