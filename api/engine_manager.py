"""
engine_manager.py — Manages the chess engine subprocess via UCI protocol

Responsibilities:
  - Spawn and supervise the compiled chess_engine executable.
  - Perform the UCI handshake (uci/uciok, isready/readyok).
  - Send position and go commands, parse info/bestmove responses.
  - Enforce one-search-at-a-time via a threading.Lock.
  - Support search timeouts by sending "stop" to the engine.
  - Shut down cleanly (quit → process exit).

Thread safety:
  - analyze() acquires a non-blocking lock; returns EngineBusy if held.
  - All engine I/O happens under the lock, so no concurrent access.
"""

from __future__ import annotations

import logging
import queue
import subprocess
import threading
import time
from typing import Optional

logger = logging.getLogger("engine_manager")


# ─── Exceptions ────────────────────────────────────────────────────────────────

class EngineBusy(Exception):
    """Another search is already in progress."""


class EngineNotReady(Exception):
    """Engine process is not running or not ready."""


class EngineError(Exception):
    """Generic engine communication failure."""


# ─── EngineManager ──────────────────────────────────────────────────────────────

class EngineManager:
    """Manages a single chess engine subprocess via UCI."""

    def __init__(self, engine_path: str):
        self.engine_path = engine_path
        self.process: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._ready = False
        self._line_queue: queue.Queue[str | None] = queue.Queue()
        self._reader_thread: Optional[threading.Thread] = None

    # ── Lifecycle ───────────────────────────────────────────────────────────────

    def start(self, timeout: float = 10.0) -> None:
        """Spawn the engine and complete the UCI handshake."""
        logger.info("Starting engine: %s", self.engine_path)
        self.process = subprocess.Popen(
            [self.engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,  # line-buffered
        )
        self._start_reader()

        # UCI handshake
        self._send("uci")
        self._wait_for("uciok", timeout=timeout)
        self._send("isready")
        self._wait_for("readyok", timeout=timeout)
        self._ready = True
        logger.info("Engine ready (handshake complete)")

    def stop(self) -> None:
        """Shut down the engine cleanly."""
        self._ready = False
        if self.process is None:
            return
        try:
            self._send("quit")
            self.process.wait(timeout=3)
            logger.info("Engine exited cleanly")
        except Exception:
            logger.warning("Engine did not exit cleanly, killing")
            self.process.kill()
            try:
                self.process.wait(timeout=2)
            except Exception:
                pass
        finally:
            self.process = None

    def is_ready(self) -> bool:
        """True if the engine process is alive and handshake is done."""
        return (
            self._ready
            and self.process is not None
            and self.process.poll() is None
        )

    # ── Analysis ───────────────────────────────────────────────────────────────

    def analyze(
        self,
        fen: str,
        depth: Optional[int] = None,
        movetime: Optional[int] = None,
        nodes: Optional[int] = None,
        timeout: float = 30.0,
    ) -> dict:
        """
        Analyze a position and return the search result.

        Parameters:
            fen       — FEN string of the position to analyze.
            depth     — maximum search depth (plies).
            movetime   — time limit in milliseconds.
            nodes     — node count limit.
            timeout   — server-side safety timeout in seconds.

        Returns a dict with keys:
            bestmove, ponder (optional), score_type, score_value,
            depth, nodes, nps, time_ms, pv, timed_out.

        Raises:
            EngineBusy    — another search is in progress.
            EngineNotReady — engine is not running.
            EngineError   — communication failure.
        """
        if not self._lock.acquire(blocking=False):
            raise EngineBusy("Another search is in progress")

        try:
            if not self.is_ready():
                raise EngineNotReady("Engine is not running")

            # Reset engine state between searches
            self._send("ucinewgame")
            self._send("isready")
            self._wait_for("readyok")

            # Set up the position
            self._send(f"position fen {fen}")

            # Build the go command
            go_cmd = "go"
            if depth is not None:
                go_cmd += f" depth {depth}"
            elif movetime is not None:
                go_cmd += f" movetime {movetime}"
            elif nodes is not None:
                go_cmd += f" nodes {nodes}"
            else:
                go_cmd += " depth 6"  # sensible default

            self._send(go_cmd)
            return self._read_search_result(timeout=timeout)
        finally:
            self._lock.release()

    # ── Internal: I/O ───────────────────────────────────────────────────────────

    def _send(self, cmd: str) -> None:
        """Send a UCI command to the engine."""
        if self.process is None or self.process.stdin is None:
            raise EngineError("Engine process has no stdin")
        logger.debug("→ %s", cmd)
        try:
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            raise EngineError(f"Failed to send command: {e}") from e

    def _start_reader(self) -> None:
        """Start a daemon thread that reads engine stdout into a queue."""

        def _reader():
            assert self.process is not None
            try:
                for line in self.process.stdout:
                    self._line_queue.put(line.strip())
            except (ValueError, OSError):
                pass
            # Sentinel: signal end of stream
            self._line_queue.put(None)

        self._reader_thread = threading.Thread(target=_reader, daemon=True)
        self._reader_thread.start()

    def _readline(self, timeout: float = 5.0) -> Optional[str]:
        """Read one line from the engine output queue."""
        try:
            return self._line_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def _wait_for(self, expected: str, timeout: float = 5.0) -> str:
        """Read lines until one starts with *expected*."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise EngineError(f"Timeout waiting for '{expected}'")
            line = self._readline(timeout=remaining)
            if line is None:
                raise EngineError("Engine closed stdout (process died?)")
            logger.debug("← %s", line)
            if line.startswith(expected):
                return line

    def _read_search_result(self, timeout: float = 30.0) -> dict:
        """
        Read info lines until bestmove. Sends 'stop' on timeout.

        The engine always responds to 'stop' with a bestmove line,
        so we use a timer to enforce the server-side timeout.
        """
        last_info: dict = {}
        timed_out = False
        stop_sent = False

        def _on_timeout():
            nonlocal timed_out, stop_sent
            timed_out = True
            if not stop_sent:
                stop_sent = True
                logger.info("Search timeout — sending stop")
                try:
                    self._send("stop")
                except EngineError:
                    pass

        timer = threading.Timer(timeout, _on_timeout)
        timer.start()

        try:
            while True:
                line = self._readline(timeout=10.0)
                if line is None:
                    # No output for 10 seconds
                    if timed_out:
                        raise EngineError(
                            "Engine did not respond after stop"
                        )
                    continue

                logger.debug("← %s", line)

                if line.startswith("info"):
                    parsed = self._parse_info(line)
                    if parsed:
                        # Keep the most complete info (one with PV)
                        if "pv" in parsed:
                            last_info = parsed
                        else:
                            last_info.update(parsed)
                elif line.startswith("bestmove"):
                    timer.cancel()
                    result = self._parse_bestmove(line)
                    result.update(last_info)
                    result["timed_out"] = timed_out
                    return result
        finally:
            timer.cancel()

    # ── Internal: parsing ───────────────────────────────────────────────────────

    @staticmethod
    def _parse_info(line: str) -> dict:
        """Parse a UCI 'info' line into a dict."""
        tokens = line.split()
        info: dict = {}
        i = 0
        while i < len(tokens):
            tok = tokens[i]
            if tok == "depth" and i + 1 < len(tokens):
                info["depth"] = int(tokens[i + 1])
                i += 2
            elif tok == "score" and i + 2 < len(tokens):
                info["score_type"] = tokens[i + 1]
                info["score_value"] = int(tokens[i + 2])
                i += 3
            elif tok == "nodes" and i + 1 < len(tokens):
                info["nodes"] = int(tokens[i + 1])
                i += 2
            elif tok == "nps" and i + 1 < len(tokens):
                info["nps"] = int(tokens[i + 1])
                i += 2
            elif tok == "time" and i + 1 < len(tokens):
                info["time_ms"] = int(tokens[i + 1])
                i += 2
            elif tok == "hashfull" and i + 1 < len(tokens):
                info["hashfull"] = int(tokens[i + 1])
                i += 2
            elif tok == "pv":
                info["pv"] = tokens[i + 1:]
                break
            else:
                i += 1
        return info

    @staticmethod
    def _parse_bestmove(line: str) -> dict:
        """Parse a UCI 'bestmove' line into a dict."""
        tokens = line.split()
        result: dict = {}
        if len(tokens) >= 2:
            result["bestmove"] = tokens[1]
        if len(tokens) >= 4 and tokens[2] == "ponder":
            result["ponder"] = tokens[3]
        return result
