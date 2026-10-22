# Known Limitations (v0.1.0)

This document records honest, tested limitations of the engine at the
v0.1.0 release. Every item below is either measured, observed during
testing, or a known gap from the spec. Use this list when planning the
next development cycle.

---

## 1. Playing Strength

**Not benchmarked against Stockfish or any rated engine.**

The engine implements correct chess — all legal moves, all special rules,
correct search — but playing strength has not been measured. Known factors
that limit strength at v0.1.0:

- No killer move heuristic or history heuristic in move ordering. Only
  MVV-LVA captures and quiet moves are sorted. This leaves significant
  alpha-beta tree reduction on the table.
- No null-move pruning, late-move reductions (LMR), or aspiration windows.
  These are standard search improvements that are safe to add after the
  correctness baseline is confirmed.
- No opening book. The engine calculates from move 1, including
  well-known opening theory that a book would handle instantly.
- No endgame tablebases (Syzygy/Gaviota). Endgame play will be imperfect
  in positions that tablebases solve trivially.
- Quiescence search only captures promotions and captures; checks in
  quiescence are not considered, which can miss some tactical motifs.

---

## 2. Search Performance

**Measured baseline (Intel i5-8350U, Release build, 16 MB hash):**
- ~90,000–176,000 NPS depending on position complexity.
- Depth 6 takes 0.1–17 seconds depending on the position.
- Depth 8 from the starting position takes ~60 seconds.

**Implications:**

- Depth 8+ is not safe for real-time play without a `movetime` limit.
  Always use `movetime` or `go nodes` for time-controlled games.
- A 1-second time limit typically reaches depth 4–6.
- The engine uses a single thread. NPS scales linearly with clock speed
  but cannot use multiple cores.

---

## 3. Single-Threaded Only

The search is fully single-threaded. The UCI protocol handler runs on
the main thread; the search runs on a worker thread. There is no
parallel search (SMP / YBWC / Lazy SMP).

Multi-threaded search would require significant redesign of the board
make/unmake and transposition table access. The `Threads` UCI option is
accepted and stored but has no effect beyond 1.

---

## 4. HTTP Adapter: One Concurrent Search

The HTTP adapter enforces one search at a time per engine process. A
second concurrent request receives HTTP 409 immediately.

For a production deployment with multiple simultaneous users, a process
pool (one engine process per concurrent slot) is needed. This is not
implemented in v0.1.0.

---

## 5. HTTP Adapter: Python/C++ Process Boundary

The adapter communicates with the engine over stdin/stdout pipes. This
adds ~10–50 ms of overhead per request (process spawn is done once at
startup; per-request overhead is pipe I/O only).

The adapter is not suitable for extremely low-latency use cases where
sub-millisecond response times are required.

---

## 6. NNUE Evaluator

- NNUE is opt-in via `setoption name EvalFile`. It is not enabled by
  default because the 21 MB network file is not committed to Git.
- The HTTP adapter does not automatically enable NNUE. A teammate who
  wants NNUE-quality evaluation must extend `engine_manager.py` to set
  `EvalFile` after startup (see `docs/HANDOFF.md §8`).
- Incremental accumulator updates are not implemented. The full feature
  vector is recomputed at every leaf node. This is correct but slower
  than an incremental implementation. At v0.1.0, NNUE and handcrafted
  eval produce the same NPS (~100K), so the cost is hidden by move
  generation. It will matter at higher depths.
- Only the HalfKP 256x2-32-32 architecture (nn-04cf2b4ed1da.nnue) has
  been tested. Other `.nnue` files will fail to load unless they use the
  same architecture.

---

## 7. Board Representation

The engine uses a 64-square array. This is correct and easy to reason
about but slower than a bitboard representation for move generation and
attack detection. On the benchmark hardware, this is not a bottleneck at
the current search depth. At depth 10+ with multi-threaded search it
would become significant.

---

## 8. No Repetition Detection

The engine does not detect threefold repetition. It handles the 50-move
rule (halfmove clock ≥ 100 → draw) but will not claim a draw by
repetition. In practice this means:

- The engine may repeat a position indefinitely if it evaluates a
  repetitive line as best.
- Your backend must detect and enforce the threefold-repetition rule
  independently if required by your application.

---

## 9. No Draw Offer or Claim Handling in UCI

The UCI commands `draw` and `ponderhit` are not implemented. The engine
handles all required UCI commands (`uci`, `isready`, `ucinewgame`,
`position`, `go`, `stop`, `quit`, `setoption`) but `draw` and
`ponderhit` are silently ignored.

---

## 10. Windows Not Yet Tested

The spec targets Ubuntu/Linux and Windows. All development and testing
was done on Ubuntu 26.04. The CMake build system and code are written
to be portable (C++17, no Linux-specific APIs in the engine core), but
no Windows build has been attempted.

The Python adapter uses `subprocess.Popen` with `text=True` and
line-buffered stdio, which should work on Windows, but this has not
been verified.

---

## 11. No Persistent Logging

The engine and adapter log to stderr and Python's `logging` module
respectively. There is no log file rotation, structured logging, or
integration with a centralised log aggregator. For production
deployment, redirect stderr to a log file and add a log rotation policy.

---

## 12. No Authentication on the HTTP API

The `/analyze` endpoint has no authentication, rate limiting, or IP
allowlisting. It must not be exposed to the public internet without a
reverse proxy that enforces access control.

---

## Summary Table

| Limitation | Severity | Effort to fix | Phase |
|-----------|----------|--------------|-------|
| No killer/history heuristics | Medium | Low | Next |
| No null-move pruning / LMR | Medium | Medium | Next |
| No opening book | Low | Low | Future |
| No endgame tablebases | Low | Medium | Future |
| Single-threaded search | Medium | High | Future |
| One HTTP concurrent search | Medium | Medium | Future |
| No threefold repetition | Medium | Low | Next |
| NNUE incremental updates | Low | Medium | Future |
| 64-square array (not bitboards) | Low | High | Future |
| Windows not tested | Medium | Low | Next |
| No API authentication | High | Low | Before production |
| No log rotation | Low | Low | Before production |
