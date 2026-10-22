#!/usr/bin/env python3
"""
benchmark.py — Repeatable engine performance benchmark (Phase 5)

Runs the compiled chess_engine executable on a set of standard benchmark
positions at a fixed depth, collecting:

  - Depth reached
  - Nodes searched
  - Nodes per second
  - Elapsed time (ms)
  - Best move + ponder
  - Hash full (per-mille)
  - Peak resident memory (KB, if /usr/bin/time is available)

Usage:
  python3 scripts/benchmark.py [options]

Options:
  --engine PATH      Path to chess_engine executable (default: ./build/chess_engine)
  --depth N          Fixed search depth (default: 8)
  --hash MB          Hash size in MB (default: 16)
  --movetime MS      Time limit per position in ms (overrides --depth)
  --positions N      Number of benchmark positions to run (default: all)
  --json FILE        Write results as JSON to FILE
  --no-memory        Skip memory measurement (faster, less accurate)

Example:
  python3 scripts/benchmark.py --depth 8 --hash 32
  python3 scripts/benchmark.py --movetime 1000 --json results.json
"""

import argparse
import json
import os
import resource
import subprocess
import sys
import time
from pathlib import Path


# ─── Standard benchmark positions ──────────────────────────────────────────────
#
# These are well-known positions used across chess engine testing.
# Each has a name, FEN, and an optional description.

BENCHMARK_POSITIONS = [
    {
        "name": "startpos",
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "desc": "Standard starting position",
    },
    {
        "name": "kiwipete",
        "fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "desc": "Classic perft and tactical benchmark",
    },
    {
        "name": "endgame",
        "fen": "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "desc": "Sparse endgame with promotion tactics",
    },
    {
        "name": "tactical",
        "fen": "r1b1k1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
        "desc": "Middlegame with open lines",
    },
    {
        "name": "middlegame",
        "fen": "r2q1rk1/ppp2ppp/2n1bn2/1B1p4/3P4/1Q3N2/PP3PPP/RNB1K2R w KQkq - 0 1",
        "desc": "Complex middlegame position",
    },
    {
        "name": "forks",
        "fen": "r3k2r/p1pp1pp1/bn2pnp1/3P4/1p2P3/2N5/PP3PPP/R1BQK1NR w KQkq - 0 1",
        "desc": "Position rich in knight forks",
    },
]


def log(msg: str):
    print(f"[BENCH] {msg}", flush=True)


def run_benchmark_position(engine_path, fen, depth=None, movetime=None, hash_mb=16):
    """Run the engine on a single position and return performance data."""

    # Build UCI command sequence
    commands = [
        "uci",
        "setoption name Hash value {}".format(hash_mb),
        "isready",
        "ucinewgame",
        "position fen {}".format(fen),
    ]
    if depth is not None:
        commands.append("go depth {}".format(depth))
    elif movetime is not None:
        commands.append("go movetime {}".format(movetime))
    else:
        commands.append("go depth 8")  # default
    commands.append("quit")
    uci_input = "\n".join(commands) + "\n"

    # Run engine and measure time
    start_wall = time.perf_counter()

    proc = subprocess.Popen(
        [engine_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    stdout, stderr = proc.communicate(input=uci_input, timeout=120)
    wall_elapsed = time.perf_counter() - start_wall

    if proc.returncode != 0:
        return {
            "error": "Engine exited with code {}".format(proc.returncode),
            "stderr": stderr[:500],
        }

    # Parse UCI info lines and bestmove
    result = {
        "depth": 0,
        "score_cp": None,
        "score_mate": None,
        "nodes": 0,
        "nps": 0,
        "time_ms": 0,
        "hashfull": 0,
        "bestmove": "",
        "ponder": "",
        "wall_time_ms": int(wall_elapsed * 1000),
        "pv": [],
    }

    lines = stdout.strip().split("\n")
    last_info = {}

    for line in lines:
        line = line.strip()
        if line.startswith("info") and " pv " in line:
            # Parse the last info line with PV
            tokens = line.split()
            i = 0
            pv_moves = []
            while i < len(tokens):
                if tokens[i] == "depth":
                    last_info["depth"] = int(tokens[i + 1])
                elif tokens[i] == "score":
                    if tokens[i + 1] == "cp":
                        last_info["score_cp"] = int(tokens[i + 2])
                        last_info["score_mate"] = None
                    elif tokens[i + 1] == "mate":
                        last_info["score_mate"] = int(tokens[i + 2])
                        last_info["score_cp"] = None
                    i += 1
                elif tokens[i] == "nodes":
                    last_info["nodes"] = int(tokens[i + 1])
                elif tokens[i] == "nps":
                    last_info["nps"] = int(tokens[i + 1])
                elif tokens[i] == "time":
                    last_info["time_ms"] = int(tokens[i + 1])
                elif tokens[i] == "hashfull":
                    last_info["hashfull"] = int(tokens[i + 1])
                elif tokens[i] == "pv":
                    pv_moves = tokens[i + 1:]
                i += 1
            last_info["pv"] = pv_moves
        elif line.startswith("info"):
            # Parse info lines without PV (less detailed)
            tokens = line.split()
            i = 0
            while i < len(tokens):
                if tokens[i] == "depth" and i + 1 < len(tokens):
                    try:
                        last_info.setdefault("depth", int(tokens[i + 1]))
                    except ValueError:
                        pass
                elif tokens[i] == "nodes" and i + 1 < len(tokens):
                    try:
                        last_info.setdefault("nodes", int(tokens[i + 1]))
                    except ValueError:
                        pass
                elif tokens[i] == "nps" and i + 1 < len(tokens):
                    try:
                        last_info.setdefault("nps", int(tokens[i + 1]))
                    except ValueError:
                        pass
                elif tokens[i] == "time" and i + 1 < len(tokens):
                    try:
                        last_info.setdefault("time_ms", int(tokens[i + 1]))
                    except ValueError:
                        pass
                elif tokens[i] == "hashfull" and i + 1 < len(tokens):
                    try:
                        last_info.setdefault("hashfull", int(tokens[i + 1]))
                    except ValueError:
                        pass
                i += 1
        elif line.startswith("bestmove"):
            tokens = line.split()
            if len(tokens) >= 2:
                result["bestmove"] = tokens[1]
            if len(tokens) >= 4 and tokens[2] == "ponder":
                result["ponder"] = tokens[3]

    # Merge parsed info
    result["depth"] = last_info.get("depth", 0)
    result["score_cp"] = last_info.get("score_cp")
    result["score_mate"] = last_info.get("score_mate")
    result["nodes"] = last_info.get("nodes", 0)
    result["nps"] = last_info.get("nps", 0)
    result["time_ms"] = last_info.get("time_ms", 0)
    result["hashfull"] = last_info.get("hashfull", 0)
    result["pv"] = last_info.get("pv", [])

    return result


def measure_memory_mb():
    """Return current process RSS in MB (best effort)."""
    try:
        rss_kb = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        # On Linux, ru_maxrss is in KB; on macOS it's in bytes
        if sys.platform == "darwin":
            return rss_kb / (1024 * 1024)
        return rss_kb / 1024
    except Exception:
        return 0.0


def main():
    parser = argparse.ArgumentParser(description="Chess engine benchmark")
    parser.add_argument("--engine", default="./build/chess_engine",
                        help="Path to chess_engine executable")
    parser.add_argument("--depth", type=int, default=8,
                        help="Fixed search depth (default: 8)")
    parser.add_argument("--movetime", type=int, default=None,
                        help="Time limit per position in ms (overrides --depth)")
    parser.add_argument("--hash", type=int, default=16,
                        help="Hash size in MB (default: 16)")
    parser.add_argument("--positions", type=int, default=0,
                        help="Number of positions to run (0 = all)")
    parser.add_argument("--json", default=None,
                        help="Write results as JSON to FILE")
    args = parser.parse_args()

    engine_path = args.engine
    if not Path(engine_path).is_file():
        print("Error: engine not found at '{}'".format(engine_path), file=sys.stderr)
        sys.exit(1)

    positions = BENCHMARK_POSITIONS
    if args.positions > 0:
        positions = positions[:args.positions]

    # Determine search mode
    if args.movetime is not None:
        search_mode = "movetime {}ms".format(args.movetime)
    else:
        search_mode = "depth {}".format(args.depth)

    log("Engine: {}".format(engine_path))
    log("Hash: {} MB".format(args.hash))
    log("Search: {}".format(search_mode))
    log("Positions: {}".format(len(positions)))
    log("")

    results = []
    total_nodes = 0
    total_time_ms = 0

    for pos in positions:
        log("Running: {} — {}".format(pos["name"], pos["desc"]))

        r = run_benchmark_position(
            engine_path, pos["fen"],
            depth=args.depth if args.movetime is None else None,
            movetime=args.movetime,
            hash_mb=args.hash,
        )

        if "error" in r:
            log("  ERROR: {}".format(r["error"]))
            results.append({"name": pos["name"], "fen": pos["fen"], **r})
            continue

        total_nodes += r["nodes"]
        total_time_ms += r["time_ms"]

        # Format score
        if r["score_mate"] is not None:
            score_str = "mate {}".format(r["score_mate"])
        elif r["score_cp"] is not None:
            score_str = "cp {}".format(r["score_cp"])
        else:
            score_str = "?"

        # Print result
        log("  depth={}  nodes={:,}  nps={:,}  time={}ms  hashfull={}  score={}  pv={}".format(
            r["depth"], r["nodes"], r["nps"], r["time_ms"],
            r["hashfull"], score_str,
            " ".join(r["pv"][:4]) if r["pv"] else r["bestmove"],
        ))
        log("  bestmove={}  ponder={}".format(r["bestmove"], r["ponder"]))
        log("")

        results.append({
            "name": pos["name"],
            "fen": pos["fen"],
            **r,
        })

    # Summary
    overall_nps = 0
    if total_time_ms > 0:
        overall_nps = (total_nodes * 1000) // total_time_ms

    summary = {
        "engine": engine_path,
        "search_mode": search_mode,
        "hash_mb": args.hash,
        "positions": len(positions),
        "total_nodes": total_nodes,
        "total_time_ms": total_time_ms,
        "overall_nps": overall_nps,
    }

    log("=" * 60)
    log("SUMMARY")
    log("  Total nodes:  {:,}".format(total_nodes))
    log("  Total time:   {}ms".format(total_time_ms))
    log("  Overall NPS:  {:,}".format(overall_nps))
    log("=" * 60)

    if args.json:
        output = {
            "summary": summary,
            "results": results,
        }
        with open(args.json, "w") as f:
            json.dump(output, f, indent=2)
        log("Results written to {}".format(args.json))


if __name__ == "__main__":
    main()
