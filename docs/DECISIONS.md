# Architecture Decisions and Open Questions

This document tracks questions confirmed with the project lead and
decisions made during implementation. Update it at the start of every
phase when anything changes.

---

## Q1 — Original engine or Stockfish wrapper?
**Decision: Build an original chess engine.**

Implement our own board representation, legal move generation, search
algorithm, and evaluation system. Do not use Stockfish as an engine
wrapper or search backend.

---

## Q2 — Are pretrained Stockfish NNUE weights permitted?
**Decision: CONFIRMED — pretrained Stockfish NNUE weights and GPL-3 license are acceptable.**

Lead approval received: "I confirm pretrained Stockfish NNUE weights and their GPL-3 license are
acceptable for this project." (October 2026)

Attribution requirement: any release must credit the Stockfish project
and comply with GPL-3. See https://github.com/official-stockfish/Stockfish
for the full license text.

Start with an original handcrafted evaluator. Design a modular
`Evaluator` interface so a compatible NNUE implementation can be added
later (Phase 7).

Do not download, commit, or integrate pretrained weights until the lead
grants explicit approval. Before integration, verify licensing, network
architecture compatibility, feature extraction requirements, inference
code, and hardware performance on the target machine.

---

## Q3 — Is C++ required?
**Decision: Use C++17.**

Use C++17 for the engine core with a modular architecture that supports
testing and future extensions.

---

## Q4 — Which operating systems must be supported?
**Decision: Support Ubuntu/Linux and Windows.**

The engine must build and run on both Ubuntu/Linux and Windows because
the project team uses both operating systems.

Requirements:
- Use portable C++17 and avoid unnecessary OS-specific code.
- Use CMake as the cross-platform build system and Ninja as the preferred
  build tool.
- Keep platform-specific process management, paths, and HTTP server
  integration isolated from the chess engine core.
- Ensure UCI communication behaves consistently on both operating systems.
- Provide separate build and test instructions for Ubuntu/Linux and Windows.
- Test the engine on both platforms before declaring cross-platform support
  complete.
- Do not require WSL on Windows as the only way to run the engine.

**Initial targets:** Ubuntu/Linux and Windows 10/11, subject to
confirmation of the team's exact Windows version and compiler availability.

**Status:** Cross-platform requirement decided; Windows version and
toolchain details remain to be confirmed.

---

## Q5 — Minimum CPU, RAM, and storage requirements?
**Decision: Benchmark before setting minimum specifications.**

Avoid inventing hardware requirements. Measure engine performance and
memory consumption on available hardware, then document realistic
minimum and recommended specifications. Use CPU-only execution initially
with conservative memory usage and configurable search limits.

---

## Q6 — Target move-response time?
**Decision: Benchmark first and establish a target later.**

Support bounded searches through UCI limits (`go depth`, `go movetime`,
`go nodes`). Measure response times on target hardware before committing
to a specific performance target.

---

## Q7 — First deliverable: UCI-only or UCI + HTTP?
**Decision: Build and test the standalone UCI executable first, then add
HTTP.**

The initial deliverable must be a working engine that communicates
through UCI. Validate position handling, search, move output, and
protocol compliance before implementing the HTTP adapter.

---

## Q8 — Local process or separate network service?
**Decision: Start with a local engine process and HTTP/JSON adapter.**

The teammate's backend communicates with the HTTP adapter. The adapter
communicates with the engine internally via UCI. Initially run all
components locally. Keep deployment details configurable so the adapter
and engine can later be hosted separately.

Intended architecture:

```
Frontend → Backend → HTTP/JSON Adapter → UCI Interface → Original Chess Engine
```

The engine core must not depend on HTTP.

---

## Q9 — Opening books and endgame tablebases?
**Decision: Do not include them initially.**

Begin without external opening books or endgame tablebases. Reconsider
only after lead approval and after understanding licensing, storage, and
performance implications.

---

## Q10 — Licensing and attribution requirements?
**Decision: Review licensing before adding any external asset or
dependency.**

Do not copy third-party engine code, commit pretrained network weights,
or add external chess assets without checking their licenses and
obtaining required approval. Maintain documentation for every external
dependency and applicable license.

---

## Resolved technical decisions

| ID  | Decision | Rationale | Phase |
|-----|----------|-----------|-------|
| D-1 | CMake + Ninja build system | Cross-platform (Linux + Windows), reproducible, standard for C++ | 0 |
| D-2 | C++17 language standard | `std::optional`, structured bindings; widespread compiler support | 0 |
| D-3 | UCI output on stdout only; diagnostics to stderr | Required by UCI spec; prevents protocol corruption | 0 |
| D-4 | No third-party engine code or weights without approval | Licensing safety | 0 |
| D-5 | 64-square array board representation | Clarity and correctness first; optimize later if benchmarks justify it | 1 |
| D-6 | Abstract `Evaluator` interface | Allows swapping handcrafted → NNUE without touching search | 1 |
| D-7 | Score convention: side-to-move positive | Simplifies negamax; documented in evaluation.hpp when implemented | 1 |
| D-8 | Castling rights stored as a 4-bit flag (KQkq) | Compact, reversible, standard | 1 |
| D-9 | En-passant target stored as `Square` (`NO_SQ` when none) | Matches FEN semantics directly | 1 |
| D-10 | `Move` encoded as from+to+flags in a `uint32_t` | Avoids heap allocation in the search loop | 1 |
| D-11 | Asynchronous worker thread for search | Allows engine to accept `stop`, `isready`, and `quit` during calculation | 4 |
| D-12 | Mutex-synchronized stdout & flushed responses | Prevents line interleaving; satisfies UCI interactive stream requirements | 4 |
| D-13 | Board copy for search worker thread | Completely eliminates data races on `Board` between search and command loop | 4 |
| D-14 | CRLF line ending normalization | Robust cross-platform UCI stream compatibility (Windows / Linux) | 4 |
| D-15 | Zobrist hashing + transposition table | Caches searched positions for cutoffs and move ordering; configurable hash size via UCI `setoption name Hash` | 5 |
| D-16 | Fail-soft alpha-beta with TT storage | Stores exact/lower/upper bounds with mate-score adjustment for position-independent TT entries | 5 |
| D-17 | Generation-based TT replacement | Older-generation entries replaced first; within same generation, deeper entries kept. Simple and effective. | 5 |
| D-18 | Python FastAPI HTTP adapter | Wraps UCI engine in a JSON API; keeps HTTP layer separate from C++ engine core | 6 |
| D-19 | One concurrent search per engine process | Returns 409 if busy; simple and predictable for first deliverable | 6 |
| D-20 | python-chess for FEN validation in adapter | Used only in the HTTP layer for input validation, never in the engine itself | 6 |
| D-21 | Server-side safety timeout (30s) | Prevents unbounded CPU use; sends UCI "stop" and returns partial result | 6 |
| D-22 | nnue-probe library vendored in src/nnue_probe/ | Lightweight HalfKP probe; no reimplementation of Stockfish inference needed | 7 |
| D-23 | EvalFile UCI option for runtime NNUE load | Network selectable without recompiling; fallback to handcrafted on any failure | 7 |
| D-24 | Searcher uses Evaluator* not Evaluator& | Allows live swap of evaluator (handcrafted ↔ NNUE) via set_evaluator() | 7 |

---

## Unresolved assumptions

- Minimum hardware specification: Phase 5 baseline measured on Intel
  i5-8350U with 8 GB RAM. Safe defaults documented in
  [`docs/BENCHMARKS.md`](BENCHMARKS.md). Confirm on actual target hardware
  before finalizing minimum requirements.
- NNUE approval status: **confirmed** (October 2026). Network
  nn-04cf2b4ed1da.nnue loaded via nnue-probe. See `docs/NNUE_GUIDE.md`.
- HTTP API schema: agreed and implemented in Phase 6. See
  [`docs/HTTP_API.md`](HTTP_API.md). Confirm any adjustments with the
  teammate before integration.
- Concurrency model for the HTTP adapter: not yet designed.
- Windows toolchain: exact Windows version (10/11) and compiler (MSVC vs
  MinGW-w64 vs Clang/LLVM) to be confirmed before cross-platform build
  instructions can be written and tested.
- Any OS-specific code paths (e.g. process spawning for the HTTP adapter,
  signal handling for clean shutdown) must be isolated behind a platform
  abstraction; specific approach TBD in the relevant phase.
