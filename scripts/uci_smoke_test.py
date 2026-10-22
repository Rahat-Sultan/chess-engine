#!/usr/bin/env python3
"""
uci_smoke_test.py — End-to-end UCI protocol smoke test

Exercises the compiled chess_engine executable over standard UCI pipes:
  1. Handshake: uci -> uciok
  2. Ping: isready -> readyok
  3. Option setting: setoption name Hash value 32
  4. Game init: ucinewgame
  5. Position setup and search: position startpos moves e2e4 e7e5 -> go depth 3 -> bestmove
  6. Interruptible search: go infinite -> stop -> bestmove
  7. Clean shutdown: quit -> exit code 0
"""

import sys
import subprocess
import time
from pathlib import Path


def log(msg: str):
    print(f"[TEST] {msg}", flush=True)


def fail(msg: str):
    print(f"\n[FAIL] {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def read_until(proc: subprocess.Popen, expected_prefix: str, timeout_sec: float = 5.0) -> list:
    lines = []
    start = time.time()
    while True:
        if time.time() - start > timeout_sec:
            fail(f"Timed out waiting for prefix '{expected_prefix}'. Received so far: {lines}")
        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                fail(f"Engine process terminated prematurely with code {proc.returncode}")
            time.sleep(0.01)
            continue
        line = line.strip()
        lines.append(line)
        if line.startswith(expected_prefix):
            return lines


def send(proc: subprocess.Popen, cmd: str):
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()


def run_tests(engine_path: str):
    log(f"Starting UCI smoke test with executable: {engine_path}")
    if not Path(engine_path).is_file():
        fail(f"Engine executable not found at '{engine_path}'")

    proc = subprocess.Popen(
        [engine_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    try:
        # 1. UCI Handshake
        log("Testing 'uci' handshake...")
        send(proc, "uci")
        lines = read_until(proc, "uciok")
        assert any(l.startswith("id name") for l in lines), "Missing id name"
        assert any(l.startswith("id author") for l in lines), "Missing id author"
        assert any("uciok" in l for l in lines), "Missing uciok"
        log("  -> Handshake OK")

        # 2. isready
        log("Testing 'isready'...")
        send(proc, "isready")
        read_until(proc, "readyok")
        log("  -> readyok OK")

        # 3. setoption
        log("Testing 'setoption'...")
        send(proc, "setoption name Hash value 32")
        send(proc, "isready")
        read_until(proc, "readyok")
        log("  -> setoption OK")

        # 4. ucinewgame & position startpos moves ...
        log("Testing 'position startpos moves ...' and 'go depth 3'...")
        send(proc, "ucinewgame")
        send(proc, "position startpos moves e2e4 e7e5")
        send(proc, "go depth 3")
        lines = read_until(proc, "bestmove")
        bm_line = [l for l in lines if l.startswith("bestmove")][0]
        log(f"  -> Received {bm_line}")
        parts = bm_line.split()
        assert len(parts) >= 2, f"Malformed bestmove line: {bm_line}"
        assert parts[1] != "(none)", "Should not be (none) in normal position"
        log("  -> go depth OK")

        # 5. go infinite + stop
        log("Testing 'go infinite' + 'stop'...")
        send(proc, "position startpos")
        send(proc, "go infinite")
        time.sleep(0.05)
        send(proc, "stop")
        lines = read_until(proc, "bestmove")
        bm_line = [l for l in lines if l.startswith("bestmove")][0]
        log(f"  -> Received {bm_line}")
        log("  -> stop OK")

        # 6. quit & clean exit
        log("Testing 'quit' and clean shutdown...")
        send(proc, "quit")
        ret = proc.wait(timeout=3.0)
        assert ret == 0, f"Expected returncode 0, got {ret}"
        log("  -> Clean shutdown OK")

    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    print("\n[PASS] All UCI smoke tests passed successfully!")


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "./build/chess_engine"
    run_tests(path)
