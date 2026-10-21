# Lightweight Chess Engine --- Project Specification & Implementation Plan

**Purpose:** A practical, step-by-step engineering plan for building a
lightweight chess engine that communicates through UCI, can later be
exposed through an HTTP/JSON API, and is designed to run on modest
CPU-only computers.

**Primary developer:** Chess Engine Intern\
**Frontend/backend owner:** Teammate\
**Development environment:** Ubuntu Linux, VS Code, Google Antigravity\
**Document status:** Initial technical specification --- confirm open
questions with the senior before making irreversible architecture
decisions.

------------------------------------------------------------------------

## 1. Executive Summary

The project is to build a chess engine as a separate component from the
chess application's frontend and backend.

The engine's responsibilities are to:

1.  Accept a chess position and search limits.
2.  Generate and validate legal moves.
3.  Search possible continuations.
4.  Evaluate positions and select a move.
5.  Communicate through the Universal Chess Interface (UCI).
6.  Eventually be callable through a small HTTP service that accepts
    JSON and returns JSON.
7.  Run on modest CPU-only computers, with memory and compute limits
    that can be configured.

The engine must **not** own user accounts, multiplayer sessions, the
frontend, or persistent application game history. The application
backend owns the game state and sends the current position to the engine
when it needs an AI decision.

### Recommended strategy

Build a small, correct, modular engine first. Start with a conventional
evaluation function and alpha-beta search. Keep the evaluator behind a
clear interface so a compatible NNUE evaluator can be added later
without replacing the search architecture.

Do not try to match Stockfish's strength in the first milestone.
Correctness, a clear protocol, predictable resource use, reproducible
tests, and easy integration are the initial goals.

------------------------------------------------------------------------

## 2. Important Decisions and Open Questions

Confirm these with the senior before committing to a final design:

-   [ ] Is the requirement to build an original chess engine, or is
    wrapping Stockfish acceptable?
-   [ ] Are pretrained Stockfish NNUE weights permitted, or must the
    evaluation model also be original?
-   [ ] Is the engine expected to be written in C++? This plan
    recommends C++ for the search engine.
-   [ ] Which target operating systems must be supported: Ubuntu/Linux
    only, Windows too, or macOS as well?
-   [ ] What are the minimum target CPU, RAM, and storage requirements?
-   [ ] What response-time target is expected for a move?
-   [ ] Should HTTP support be part of the first deliverable, or should
    the first deliverable be a standalone UCI executable?
-   [ ] Will the engine run as a local process beside the backend, or as
    a separate network service?
-   [ ] Are external opening books or endgame tablebases permitted?
-   [ ] What licensing and attribution requirements apply to
    dependencies and any reused model weights?

**Default assumption until confirmed:** build an original C++ engine;
use a simple original evaluator first; support UCI first; add an HTTP
adapter afterward; do not bundle Stockfish code or weights unless
explicitly approved and the applicable license and compatibility
requirements have been checked.

------------------------------------------------------------------------

## 3. Scope and Non-Goals

### In scope

-   Chess position representation.
-   FEN parsing and serialization.
-   Legal move generation and move application/undo.
-   Special rules: castling, en passant, promotion, check, checkmate,
    stalemate, and draw conditions.
-   Search and position evaluation.
-   UCI protocol handling.
-   Resource limits and clean shutdown.
-   Unit, perft, protocol, and integration tests.
-   Optional NNUE evaluator integration after the base engine works.
-   Later HTTP/JSON wrapper and integration documentation.

### Out of scope for the engine

-   Frontend chessboard and animations.
-   User accounts, authentication, matchmaking, or chat.
-   Persistent user/game database.
-   Multiplayer synchronization.
-   Frontend game rules or UI state.
-   Replacing the teammate's backend.

The backend remains the source of truth for a real application game. The
engine analyzes the position it is given and proposes a move. The
backend must validate the returned move and apply it to the
authoritative game state.

------------------------------------------------------------------------

## 4. High-Level System Design

``` mermaid
flowchart TD
    FE[Chess Frontend] <-->|HTTP JSON / WebSocket| BE[Chess Backend]
    BE -->|HTTP JSON request| API[Engine HTTP Adapter - later phase]
    API -->|UCI commands and responses| UCI[UCI Controller]
    UCI --> CORE[Chess Engine Core]
    CORE --> BOARD[Board and Legal Move Generator]
    CORE --> SEARCH[Search: Iterative Deepening + Alpha-Beta]
    SEARCH --> EVAL[Evaluation Interface]
    EVAL --> SIMPLE[Handcrafted Evaluator - initial]
    EVAL -. later .-> NNUE[Compatible NNUE Evaluator]
    CORE --> RES[Resource Limits and Search State]
```

### Component responsibilities

**Frontend** - Displays the board and game state. - Sends player actions
to the backend. - Does not call the engine directly.

**Application backend (teammate-owned)** - Owns sessions, turns, rules
enforcement, and saved move history. - Validates player moves. - Calls
the engine only when an AI move is required. - Applies and validates the
engine's returned move. - Handles application-level timeouts and errors.

**Engine HTTP adapter (later phase)** - Exposes a documented HTTP/JSON
contract. - Validates request shape and search limits. - Converts the
request into UCI commands or calls a managed engine process. - Returns
structured JSON results. - Does not become the source of truth for game
history.

**UCI controller** - Reads UCI commands such as `uci`, `isready`,
`position`, `go`, `stop`, and `quit`. - Writes responses such as
`uciok`, `readyok`, `info`, and `bestmove`. - Keeps protocol output
separate from diagnostic logs.

**Chess engine core** - Represents positions. - Generates legal moves. -
Searches candidate continuations. - Evaluates positions. - Returns a
best move and optional analysis.

------------------------------------------------------------------------

## 5. Statelessness and State Ownership

The HTTP interface can be designed to behave statelessly between
requests: each analysis request supplies a FEN position and limits, and
the response contains the result. The backend stores the game history.

However, the running engine process may retain useful internal data
between searches, including a transposition table, hash entries, and
evaluator caches. Therefore:

-   **API-level statelessness:** each request includes enough
    information to identify the position to analyze.
-   **Engine process state:** internal caches and configuration may
    persist between requests.
-   **Game state:** the application backend owns the authoritative game
    and history.
-   **Persistence:** do not save games to disk from the engine unless a
    later requirement explicitly asks for it.

Do not describe NNUE as a statelessness mechanism. NNUE is a
position-evaluation technique. Statelessness is an API/session design
choice.

For the initial local implementation, one long-lived engine process is
acceptable. For HTTP concurrency, do not let multiple requests write to
the same UCI process at once without explicit request serialization and
cancellation handling. A simple first version may permit one active
search per engine process and return a clear busy response or queue
requests. Later, use a bounded process pool if needed.

------------------------------------------------------------------------

## 6. UCI Protocol Requirements

UCI is a line-based text protocol between an application and a chess
engine. It is separate from HTTP and JSON.

### Essential commands

  -------------------------------------------------------------------------
  Command                               Purpose
  ------------------------------------- -----------------------------------
  `uci`                                 Ask the engine to identify itself
                                        and list options.

  `isready`                             Ask whether the engine is ready to
                                        accept work.

  `ucinewgame`                          Notify the engine that a new game
                                        is starting; clear appropriate
                                        search state if implemented.

  `position startpos`                   Set the standard starting position.

  `position startpos moves e2e4 e7e5`   Set the starting position and
                                        replay the supplied moves.

  `position fen <six FEN fields>`       Set a position directly using FEN.

  `go depth 10`                         Search to a maximum depth.

  `go movetime 1000`                    Search with a time limit in
                                        milliseconds.

  `go nodes 100000`                     Search with a node limit.

  `go infinite`                         Search until stopped.

  `stop`                                Stop the current search and return
                                        a final `bestmove`.

  `quit`                                Shut down cleanly.

  `setoption name Hash value 64`        Set a supported engine option.

  `setoption name Threads value 1`      Configure thread count if
                                        implemented.
  -------------------------------------------------------------------------

### Expected responses

-   `id name ...`
-   `id author ...`
-   `option ...` for supported options
-   `uciok`
-   `readyok`
-   `info depth ... score cp ... nodes ... pv ...` as available
-   `bestmove e2e4` and optionally `ponder ...`

The engine must not emit debug messages to stdout while speaking UCI,
because that can corrupt protocol communication. Send diagnostics to
stderr or a configured log.

### Position handling

The `position` command tells the engine which position to analyze. It
does not save a user's game into an application database. If moves are
supplied after `startpos`, the engine must replay them legally to
establish the current position. FEN describes one position using six
fields: piece placement, side to move, castling rights, en-passant
target, halfmove clock, and fullmove number.

The engine should reject malformed positions or invalid move sequences
safely, without crashing. Exact error behavior should be documented; UCI
does not provide a universal rich error-response format for all invalid
position inputs.

------------------------------------------------------------------------

## 7. Low-Level Design: Modules

Keep modules small, testable, and connected through explicit interfaces.

### 7.1 `types` --- Core types

Responsibilities: - Define `Color`, `PieceType`, `Piece`, `Square`,
`Move`, `Score`, and search limits. - Define safe conversions between
internal squares and UCI coordinates. - Avoid magic integers scattered
throughout the code.

Suggested types: - `Color`: White / Black - `PieceType`: Pawn / Knight /
Bishop / Rook / Queen / King - `Move`: from square, to square, promotion
piece, and flags as needed - `SearchLimits`: depth, movetime, node
limit, optional infinite mode - `SearchResult`: best move, optional
ponder move, score, depth, nodes, elapsed time, principal variation

Acceptance criteria: - Square mapping is tested for `a1`, `h1`, `a8`,
and `h8`. - UCI move parsing/formatting is tested, including promotions.

### 7.2 `board` --- Position representation

Responsibilities: - Store pieces and side to move. - Track castling
rights, en-passant square, halfmove clock, and fullmove number. - Parse
and emit FEN. - Apply and undo moves. - Track enough information to
restore a position exactly after undo.

Possible representation: - Begin with a simple 64-square array for
clarity, or use bitboards if the developer is comfortable with bitwise
operations. - Keep representation behind a `Board` interface so it can
be optimized later.

Low-spec considerations: - Avoid unnecessary allocations in the search
loop. - Reuse move buffers where practical. - Ensure make/unmake
operations are reversible and fast.

Acceptance criteria: - Standard starting FEN round-trips correctly. -
FEN parsing validates all six fields. - Applying and undoing a move
restores the exact previous position and metadata.

### 7.3 `movegen` --- Move generation and legality

Responsibilities: - Generate pseudo-legal moves. - Reject moves that
leave the moving side's king in check, or generate legal moves
directly. - Handle castling, en passant, and promotion correctly. -
Provide check detection and legal move enumeration.

Acceptance criteria: - Perft counts match known reference counts at
multiple depths for standard test positions. - Special-rule test
positions cover castling, en passant, promotion, checks, and pins. - No
move may leave the moving side's king in check.

### 7.4 `notation` --- UCI and FEN notation

Responsibilities: - Parse UCI coordinate moves such as `e2e4`,
`g7g8q`. - Convert internal moves to UCI notation. - Parse FEN and
format FEN through the board module or a cleanly separated parser. -
Optional: produce SAN for debug output, but SAN is not required for UCI.

Acceptance criteria: - Normal and promotion moves round-trip. - Invalid
coordinates and malformed move strings fail safely.

### 7.5 `search` --- Search algorithm

Initial algorithm: - Negamax with alpha-beta pruning. - Iterative
deepening. - Basic move ordering. - Quiescence search to reduce
horizon-effect problems. - Stop checks for time, node, and explicit stop
limits.

Later candidates: - Transposition table. - Killer/history move
heuristics. - Aspiration windows. - Principal variation search. -
Null-move pruning, late-move reductions, and other selective techniques
only after correctness tests.

Responsibilities: - Search legal continuations. - Call the evaluator at
leaf positions. - Return a best move and principal variation. - Respect
search limits and stop requests.

Acceptance criteria: - `go depth N` terminates and returns a legal
`bestmove`. - `stop` interrupts search promptly and still returns
`bestmove` if a legal move exists. - Search does not corrupt the board
during make/unmake. - A position with no legal moves is handled
correctly; checkmate and stalemate are distinguished.

### 7.6 `evaluation` --- Evaluation interface

Define a stable interface, for example:

``` cpp
class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual int evaluate(const Board& board) = 0;
};
```

The actual interface may be adapted to the board representation and
incremental-evaluation needs. Avoid introducing virtual-call overhead in
the deepest loop without measuring it; templates or a compact function
interface may be preferable.

Initial handcrafted evaluator: - Material values. - Piece-square tables
or simple positional bonuses. - Mobility or king safety only when basic
evaluation is reliable. - Consistent score convention: document whether
scores are from side-to-move perspective or White's perspective.

Later NNUE evaluator: - Implement compatible feature encoding. - Load
the correct network file safely. - Implement the architecture and
inference math expected by that network. - Consider incremental
accumulator updates on make/unmake. - Provide a fallback or clear
startup error if NNUE is unavailable; do not silently invent a fake NNUE
result.

Acceptance criteria: - Evaluator returns consistent scores for identical
positions. - Side-to-move score perspective is documented and tested. -
NNUE file incompatibility produces a useful error. - A small network or
simple evaluator remains available for low-memory builds if the design
supports it.

### 7.7 `transposition` --- Search cache

Responsibilities: - Cache previously searched positions using a position
hash (commonly Zobrist hashing). - Store key verification, depth, score,
bound type, and best move as appropriate. - Respect configured memory
limits. - Clear or replace entries safely when starting a new game or
resizing the table.

Do not confuse the transposition table with saved game history. It is an
optimization cache, not authoritative application data.

### 7.8 `uci` --- Protocol controller

Responsibilities: - Read one command per line from stdin. - Parse
command names and arguments. - Dispatch to position, option, readiness,
search, stop, new-game, and quit handlers. - Emit valid UCI responses. -
Coordinate search and stop behavior without deadlocks. - Keep stdout
protocol-clean.

Concurrency: - Search may run in a worker thread so the controller can
receive `stop`. - Protect shared board/search state carefully. - Do not
run two searches simultaneously against one mutable board unless
explicitly designed for it.

Acceptance criteria: - Compatible with a basic UCI handshake. -
Correctly handles `uci`, `isready`, `position`, `go`, `stop`, and
`quit`. - No debug logs appear on stdout. - Stops and exits cleanly.

### 7.9 `http_api` --- Later HTTP adapter

Recommended first API:

`GET /health`

``` json
{
  "status": "ok",
  "engine": "lightweight-chess-engine",
  "ready": true
}
```

`POST /analyze`

``` json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "limits": {
    "depth": 10
  }
}
```

Example response:

``` json
{
  "success": true,
  "bestmove": "e2e4",
  "ponder": "e7e5",
  "score": {
    "type": "cp",
    "value": 25
  },
  "depth": 10,
  "nodes": 12345,
  "pv": ["e2e4", "e7e5", "g1f3"]
}
```

The values above are examples, not promised engine output.

API rules: - Validate JSON, FEN, and all limits. - Set safe maximum
depth, time, and nodes to avoid unbounded CPU use. - Return a documented
4xx response for invalid input and a 5xx response for internal
failures. - Include a request timeout and a clean cancellation
strategy. - Do not expose arbitrary shell commands or accept executable
paths from clients. - Do not allow unbounded simultaneous searches. - Do
not include stack traces or secrets in responses. - Keep engine
diagnostics in logs, not in the UCI response stream.

Suggested status codes: - `200`: successful analysis. - `400` or `422`:
invalid request, FEN, or limits. - `409`: engine busy, if queuing is not
implemented. - `503`: engine unavailable or shutting down. - `504`:
analysis timed out, if the API contract uses this behavior.

The exact HTTP contract must be agreed with the teammate before
integration.

------------------------------------------------------------------------

## 8. NNUE: Reusing a Pretrained Network

NNUE means "Efficiently Updatable Neural Network." It is a neural
evaluation technique, not a search algorithm and not a mechanism for
making the API stateless.

### What NNUE does

-   Search explores candidate moves.
-   The evaluator estimates the position.
-   NNUE uses a trained neural network to evaluate positions.
-   Incremental updates can reuse calculations after moves rather than
    recomputing every feature from scratch.

### Can we reuse an existing network?

Potentially, yes. The official Stockfish networks repository provides
trained network files. However, a Stockfish `.nnue` file is **not a
universal neural model**. It requires compatible feature encoding,
architecture, inference code, and supported file format. Downloading a
network alone does not make an unrelated engine NNUE-capable.

Before reuse: 1. Confirm with the senior that pretrained weights are
permitted. 2. Review the current license and attribution requirements
for the exact file and implementation. 3. Confirm architecture/feature
compatibility. 4. Check the model's file size and RAM implications. 5.
Benchmark on the lowest-spec target machine. 6. Keep the handcrafted
evaluator available until NNUE works and passes tests.

Official resources: - Stockfish network repository:
https://github.com/official-stockfish/networks - NNUE technical
documentation:
https://official-stockfish.github.io/docs/nnue-pytorch-wiki/docs/nnue.html -
Stockfish network testing page: https://tests.stockfishchess.org/nns

Do not copy Stockfish source code into an original engine without
reviewing its GPL licensing implications. The licensing of a network
file and the licensing of source code are separate questions. Ask the
project lead to approve the reuse plan.

### Low-spec strategy

-   Start with CPU-only operation.
-   Default to one search thread.
-   Make hash-table size configurable and small by default.
-   Bound search by depth, nodes, or time.
-   Avoid requiring a GPU.
-   Measure peak resident memory and response latency.
-   Consider a smaller compatible network only if its architecture is
    supported.
-   Do not promise a minimum hardware requirement until measurements are
    taken.

------------------------------------------------------------------------

## 9. Tools and Development Requirements

### Required tools

  -----------------------------------------------------------------------
  Tool                                Purpose
  ----------------------------------- -----------------------------------
  Ubuntu Linux                        Initial development and testing
                                      environment

  VS Code                             Editor and terminal

  Google Antigravity                  AI-assisted implementation, code
                                      review, and test generation

  Git                                 Version control and safe
                                      checkpoints

  C++ compiler (`g++` or `clang++`)   Compile the native engine

  CMake                               Reproducible build configuration

  Ninja or Make                       Build execution

  Python 3                            Test scripts and later HTTP wrapper

  `pytest`                            Automated Python/API tests if
                                      Python wrappers are used

  `curl`                              Exercise HTTP endpoints

  `python-chess`                      External reference for testing
                                      legal moves, perft, FEN, and UCI
                                      integration

  Stockfish                           Optional reference engine for
                                      comparison and UCI interoperability
                                      tests
  -----------------------------------------------------------------------

### Suggested Ubuntu installation

Run only after confirming the machine's package manager and policies
allow these packages:

``` bash
sudo apt update
sudo apt install build-essential cmake ninja-build git python3 python3-venv python3-pip curl
```

Optional Python test tools in a virtual environment:

``` bash
mkdir -p ~/chess-engine
cd ~/chess-engine
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install python-chess pytest
```

Use `python-chess` as a testing/reference library. If the assignment
requires original move generation, do not use it internally to choose
legal moves in the final engine; use it to compare and test your
implementation.

### Optional HTTP tools

-   FastAPI and Uvicorn if the adapter is written in Python.
-   A lightweight C++ HTTP library if the API must be native; select one
    only after agreeing on the architecture.
-   OpenAPI docs for the endpoint contract.
-   Sanitizers such as AddressSanitizer and UndefinedBehaviorSanitizer
    for C++ debugging.

Keep dependencies minimal. Pin dependency versions for repeatable builds
and document how to reproduce the build.

------------------------------------------------------------------------

## 10. Proposed Repository Structure

Start with this structure and adjust only when implementation needs
justify it:

``` text
chess-engine/
├── README.md
├── LICENSE
├── .gitignore
├── CMakeLists.txt
├── docs/
│   ├── ARCHITECTURE.md
│   ├── UCI_PROTOCOL.md
│   ├── HTTP_API.md
│   ├── TESTING.md
│   └── DECISIONS.md
├── include/
│   └── chess/
│       ├── types.hpp
│       ├── board.hpp
│       ├── movegen.hpp
│       ├── notation.hpp
│       ├── evaluation.hpp
│       ├── search.hpp
│       ├── transposition.hpp
│       └── uci.hpp
├── src/
│   ├── main.cpp
│   ├── board.cpp
│   ├── movegen.cpp
│   ├── notation.cpp
│   ├── evaluation.cpp
│   ├── search.cpp
│   ├── transposition.cpp
│   └── uci.cpp
├── tests/
│   ├── unit/
│   ├── perft/
│   ├── uci/
│   └── integration/
├── scripts/
│   ├── run_perft.py
│   ├── uci_smoke_test.py
│   └── benchmark.py
├── api/                  # Add in the HTTP phase
│   ├── app.py
│   └── requirements.txt
└── networks/             # Only if approved; avoid committing large files
    └── README.md
```

Do not create every file empty on day one. Build the minimum needed for
each phase and keep the repository compiling at each checkpoint.

------------------------------------------------------------------------

## 11. Step-by-Step Implementation Plan

### Phase 0 --- Requirements and environment

Tasks: - \[ \] Confirm original-engine vs Stockfish-wrapper
requirement. - \[ \] Confirm whether pretrained NNUE weights are
permitted. - \[ \] Confirm language, OS targets, minimum hardware, and
response-time goals. - \[ \] Install compiler, CMake, Git, Python, and
test tools. - \[ \] Create a Git repository and initial README. - \[ \]
Write down the build and run commands.

Deliverable: requirements checklist and a reproducible empty C++ build.

Exit criteria: a clean clone can build a minimal executable.

### Phase 1 --- Types, board, and FEN

Tasks: - \[ \] Define core types and coordinate mapping. - \[ \]
Implement board representation. - \[ \] Parse and serialize FEN. - \[ \]
Implement side-to-move, castling rights, en-passant target, and move
clocks. - \[ \] Add tests for standard and custom FEN positions.

Deliverable: program can load a FEN and print the same position back.

Exit criteria: FEN round-trip and square mapping tests pass.

### Phase 2 --- Move generation and rules

Tasks: - \[ \] Generate pseudo-legal moves. - \[ \] Implement check
detection. - \[ \] Filter illegal moves. - \[ \] Implement make/unmake
move. - \[ \] Add castling, en passant, promotion, checkmate, and
stalemate. - \[ \] Add perft tests and compare against trusted reference
results.

Deliverable: a legal-move generator.

Exit criteria: selected perft positions match known counts; special-rule
tests pass.

### Phase 3 --- Basic evaluation and search

Tasks: - \[ \] Implement material evaluation. - \[ \] Add simple
piece-square tables. - \[ \] Implement negamax and alpha-beta pruning. -
\[ \] Add iterative deepening. - \[ \] Add move ordering. - \[ \] Add
quiescence search. - \[ \] Add node/time/depth limits and
cancellation. - \[ \] Return a legal best move and principal variation.

Deliverable: the engine can select a move from a supplied position.

Exit criteria: it returns a legal move for normal positions and handles
mate/stalemate positions correctly.

### Phase 4 --- UCI protocol

Tasks: - \[ \] Implement `uci`, `isready`, `ucinewgame`, `position`,
`go`, `stop`, and `quit`. - \[ \] Implement supported `setoption`
commands. - \[ \] Emit `info` lines with depth, score, nodes, time, and
PV where available. - \[ \] Emit `bestmove` after a completed or stopped
search. - \[ \] Keep stdout protocol-clean. - \[ \] Test with a simple
UCI script and, if available, a GUI or protocol harness.

Deliverable: standalone UCI-compatible executable.

Exit criteria: a script can set a position, request analysis, stop a
search, and receive a valid best move.

### Phase 5 --- Resource and performance baseline

Tasks: - \[ \] Default to one thread. - \[ \] Add configurable hash
size. - \[ \] Test shallow search limits. - \[ \] Measure nodes per
second, response time, and memory. - \[ \] Check CPU behavior on the
lowest-spec target machine available. - \[ \] Add benchmark positions
and repeatable benchmark scripts.

Deliverable: baseline performance report and documented safe defaults.

Exit criteria: engine stays within defined resource limits and shuts
down cleanly.

### Phase 6 --- HTTP/JSON adapter

Tasks: - \[ \] Agree on request and response schemas with the backend
teammate. - \[ \] Add `GET /health`. - \[ \] Add `POST /analyze`. - \[
\] Validate FEN and limits. - \[ \] Call the UCI process safely. - \[ \]
Add request timeouts, busy/concurrency policy, and cancellation
behavior. - \[ \] Add clear error responses. - \[ \] Publish OpenAPI
documentation and sample `curl` commands.

Deliverable: a working local HTTP service.

Exit criteria: the teammate can submit a position and receive a JSON
result without interacting with UCI directly.

### Phase 7 --- NNUE integration (optional, after approval)

Tasks: - \[ \] Confirm network reuse is allowed. - \[ \] Select a
compatible network and verify licensing. - \[ \] Implement or integrate
compatible feature encoding and inference. - \[ \] Add network loading
and error handling. - \[ \] Add incremental evaluation updates if
appropriate. - \[ \] Compare NNUE and handcrafted evaluation. - \[ \]
Benchmark memory and speed on modest hardware.

Deliverable: optional NNUE evaluator behind the same evaluation
interface.

Exit criteria: results are reproducible, no unsupported fallback is
hidden, and resource usage is documented.

### Phase 8 --- Integration and handoff

Tasks: - \[ \] Run a clean build and full test suite. - \[ \] Provide
teammate with endpoint docs and examples. - \[ \] Demonstrate
invalid-input and timeout behavior. - \[ \] Document launch, shutdown,
configuration, and logs. - \[ \] Tag a release and record known
limitations.

Deliverable: integration-ready engine service and handoff documentation.

------------------------------------------------------------------------

## 12. Testing Strategy

Correctness comes before playing strength.

### Unit tests

-   Square and move parsing.
-   FEN parsing/serialization.
-   Make/unmake restoration.
-   Attack and check detection.
-   Evaluation score conventions.
-   Search limit behavior.

### Perft tests

Perft counts the number of legal move sequences to a given depth. It is
one of the most useful ways to find move-generation bugs. Use trusted
reference positions and counts; do not rely only on games played by the
engine.

### UCI tests

-   Handshake returns `uciok`.
-   `isready` returns `readyok`.
-   Starting position search returns a legal move.
-   FEN position search returns a legal move.
-   `stop` interrupts a long search and returns `bestmove`.
-   `quit` exits cleanly.
-   No debug output contaminates stdout.

### HTTP tests

-   Valid FEN returns JSON with the documented schema.
-   Invalid JSON/FEN/limits return documented errors.
-   Timeouts do not leave runaway searches.
-   Concurrent requests follow the declared policy.
-   A restarted engine service can recover from a process failure.

### Strength tests

-   Compare against Stockfish at controlled limits.
-   Use multiple positions or self-play games, not one tactical puzzle.
-   Treat playing strength as a measured outcome, not a guaranteed
    result.

------------------------------------------------------------------------

## 13. Low-Spec Computer Requirements and Performance Policy

Do not invent a minimum hardware specification before benchmarking.
Define an initial target and measure it.

Recommended initial defaults: - CPU-only. - One search thread. - Modest
configurable hash allocation, such as 16--64 MB as an initial test
range. - Bounded search time, depth, or nodes. - No mandatory GPU. - No
mandatory database. - No mandatory opening book or endgame tablebase. -
Network weights optional until approved and measured.

Record: - CPU model and supported instruction sets. - Operating system
and compiler flags. - Peak memory use. - Search depth, nodes,
nodes/second, and elapsed time. - NNUE model size and load time if
used. - Whether the executable uses architecture-specific optimizations.

For distribution, consider a portable baseline build and an optimized
build only after correctness tests pass. Avoid compiling the entire
application with aggressive CPU-specific flags if it must run on unknown
computers.

------------------------------------------------------------------------

## 14. Google Antigravity Working Instructions

Use Antigravity as an implementation assistant, not as the source of
truth for the architecture. Keep this specification in the repository,
for example as `docs/PROJECT_SPEC.md`, and ask Antigravity to read it
before changing code.

### Suggested initial prompt for Antigravity

> You are assisting with a lightweight, original chess-engine project.
> Read `docs/PROJECT_SPEC.md` before making changes.
>
> The project is separate from the application's frontend and backend.
> The backend is owned by another teammate. Our engine must first work
> as a standalone C++ UCI executable and may later be exposed through an
> HTTP/JSON adapter.
>
> Priorities, in order: correctness, modularity, low resource usage,
> protocol compliance, testability, and then playing strength.
>
> Do not generate the entire project in one pass. Implement only the
> current phase, explain the design briefly, add tests, build and run
> them, and report exact commands and results. Do not claim tests passed
> unless they were actually run.
>
> Keep the evaluation function behind an interface so a compatible NNUE
> evaluator can be added later. Do not assume that an arbitrary `.nnue`
> file is compatible. Do not download or bundle network weights or copy
> third-party engine code without explicit approval and license review.
>
> Keep UCI protocol output on stdout and diagnostics on stderr. Never
> allow arbitrary shell commands from HTTP input. Add bounded search
> limits and a documented concurrency policy when the HTTP adapter is
> implemented.
>
> Preserve a compiling, tested repository after each phase. If a
> requirement is ambiguous, document the question instead of inventing
> an answer.

### Antigravity task loop

For every task, ask it to: 1. State the intended changes and files
before editing. 2. Implement one small milestone. 3. Build the project.
4. Run the relevant tests. 5. Inspect compiler warnings and failures. 6.
Summarize changed files, commands run, actual results, and remaining
risks. 7. Stop and ask before introducing major dependencies or changing
architecture.

Use Git checkpoints before large changes. Review generated code
yourself, especially move generation, legality, search termination, and
concurrency.

------------------------------------------------------------------------

## 15. Git and Build Workflow

Suggested initial commands:

``` bash
mkdir -p ~/chess-engine
cd ~/chess-engine
git init
```

Create a `.gitignore` that excludes build output, local environments,
logs, generated binaries, and large network files unless the team
explicitly decides to track them.

A typical CMake workflow:

``` bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run tests through the configured test runner, for example:

``` bash
ctest --test-dir build --output-on-failure
```

These commands become usable after `CMakeLists.txt` and the test suite
have been implemented. Do not expect them to work before that.

Commit after each passing milestone with clear messages, such as: -
`chore: initialize C++ project` - `feat: implement FEN parser` -
`test: add board and perft tests` - `feat: add alpha-beta search` -
`feat: implement UCI command handling` -
`feat: expose analysis HTTP endpoint`

------------------------------------------------------------------------

## 16. Definition of Done

The initial project is ready for backend integration when:

-   [ ] A clean checkout builds using documented commands.
-   [ ] The executable supports the agreed UCI command set.
-   [ ] FEN positions are handled correctly.
-   [ ] Legal move generation passes the agreed tests.
-   [ ] Search respects depth/time/node limits.
-   [ ] `stop` and `quit` work without hangs.
-   [ ] The engine returns a legal move or correctly reports no legal
    move for terminal positions.
-   [ ] The HTTP adapter uses an agreed JSON contract.
-   [ ] Invalid requests and engine failures return documented errors.
-   [ ] Concurrency and timeouts are bounded and tested.
-   [ ] Low-spec resource measurements are documented.
-   [ ] Any reused code and NNUE weights have been reviewed for
    compatibility and licensing.
-   [ ] The teammate has working API examples and setup instructions.

------------------------------------------------------------------------

## 17. Final Engineering Principles

1.  **Correctness before strength.** A fast engine that makes illegal
    moves is not acceptable.
2.  **Separate responsibilities.** Backend owns the game; engine owns
    search and move selection.
3.  **UCI and HTTP are different layers.** UCI connects the adapter to
    the engine; HTTP connects the teammate's backend to the engine
    service.
4.  **NNUE is optional at first.** Build a clean evaluator interface
    before adding a neural network.
5.  **Stateless API does not mean stateless process.** Requests can be
    self-contained while internal caches persist.
6.  **Measure low-spec performance.** Set limits and benchmark on real
    target machines.
7.  **Use Antigravity incrementally.** Small changes, builds, tests, and
    reviewed Git commits are safer than one giant code-generation
    prompt.
8.  **Confirm requirements early.** In particular, clarify whether a
    pretrained network or any existing engine code is allowed.

The first concrete task is **Phase 0: confirm requirements and set up a
reproducible C++ build**. Do not begin NNUE integration until the basic
engine, evaluator interface, protocol, and licensing requirements are
understood.
