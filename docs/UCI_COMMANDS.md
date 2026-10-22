# UCI Commands Reference

This document covers everything you need to start the engine, interact
with it directly, and understand every command it accepts and every
response it produces. It is written for both interactive use and for
developers integrating the engine into a GUI or application.

---

## 1. Starting the Engine

### Build first (if not already built)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Start the engine

```bash
./build/chess_engine
```

The engine starts silently and waits for UCI commands on stdin. It writes
responses to stdout. Diagnostic messages go to stderr only — they will
never appear in stdout and will not corrupt communication.

You can type commands directly at the terminal, pipe them in from a
script, or drive the engine from a GUI that supports UCI (Arena, CuteChess,
Scid, BanksiaGUI, etc.).

---

## 2. Quick Start — a complete session

Copy and paste this block into your terminal to run a complete search
from the starting position:

```
uci
isready
ucinewgame
position startpos
go depth 6
```

Wait for `bestmove`, then:

```
quit
```

---

## 3. Input Commands (what you send to the engine)

### `uci`

**Purpose:** Begin the UCI handshake. Tells the engine to identify itself
and list its configurable options.

**When to use:** Always send this first, before any other command.

**Syntax:** `uci`

**Response:** The engine replies with:
```
id name LightweightChessEngine 0.1.0-phase7
id author Rahat Sultan
option name Hash type spin default 16 min 1 max 1024
option name Threads type spin default 1 min 1 max 1
option name EvalFile type string default <empty>
uciok
```

`uciok` signals the handshake is complete and the engine is ready to
accept `setoption` and `isready`.

---

### `isready`

**Purpose:** Ask the engine if it is ready to receive position and search
commands. Use this after `uci`, after `setoption`, and before every new
game or position.

**When to use:** After `uci`, and after any `setoption` command that
might take time (e.g. resizing the hash table or loading NNUE).

**Syntax:** `isready`

**Response:** `readyok` — emitted only when the engine has finished all
pending work and is ready.

**Example:**
```
isready
readyok
```

---

### `setoption name <name> value <value>`

**Purpose:** Configure an engine option. Must be sent after `uci` and
before `position` or `go`.

**Syntax:** `setoption name <option name> value <option value>`

**Available options:**

#### `Hash` (integer, MB)
Sets the size of the transposition table in megabytes.

- Default: `16`
- Range: `1` – `1024`
- Effect: takes effect immediately; clears the existing table.
- Larger values improve search quality at deeper depths but consume more RAM.

```
setoption name Hash value 64
```

#### `Threads` (integer)
Configures the number of search threads.

- Default: `1`
- Range: `1` – `1` (multi-threaded search is not implemented in v0.1.0)
- Sending any value above 1 is accepted but has no effect.

```
setoption name Threads value 1
```

#### `EvalFile` (string, file path)
Load a NNUE network file for neural-network position evaluation.

- Default: empty (uses the handcrafted evaluator)
- Value: path to a HalfKP 256x2-32-32 `.nnue` file
- If the file is missing or incompatible, the engine logs an error to
  stderr and continues with the handcrafted evaluator — it does not crash.
- Send an empty string to revert to the handcrafted evaluator.

```
setoption name EvalFile value networks/nn-04cf2b4ed1da.nnue
```

```
setoption name EvalFile value
```
*(empty value reverts to handcrafted)*

---

### `ucinewgame`

**Purpose:** Notify the engine that a new game is starting. Resets the
board to the standard starting position and clears the transposition
table.

**When to use:** Before the first position of every new game. This
prevents cached evaluations from a previous game influencing the new one.

**Syntax:** `ucinewgame`

**Response:** None. Follow with `isready` to confirm the engine is ready.

**Example:**
```
ucinewgame
isready
readyok
```

---

### `position`

**Purpose:** Set the board position the engine will analyse on the next
`go` command.

**Syntax — two forms:**

#### Form 1: Standard starting position
```
position startpos
```

#### Form 2: Standard starting position with moves applied
```
position startpos moves <move1> <move2> ...
```

Moves are in UCI coordinate notation: `e2e4`, `g8f6`, `e7e8q` (promotion).
The engine replays each move in order from the starting position.

#### Form 3: Arbitrary FEN position
```
position fen <fen string>
```

The FEN must have all six fields: piece placement, side to move, castling
rights, en-passant target, halfmove clock, fullmove number.

#### Form 4: FEN with moves applied
```
position fen <fen string> moves <move1> <move2> ...
```

**Response:** None. The board is set silently. Errors (invalid FEN,
illegal move) are logged to stderr and the command is ignored.

**Examples:**
```
position startpos
position startpos moves e2e4 e7e5 g1f3
position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
position fen rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2 moves d7d5
```

---

### `go`

**Purpose:** Start searching the current position. The engine searches on
a worker thread and sends `info` lines as it works, then a final
`bestmove` when done.

**Syntax:** `go [<limit> ...]`

Multiple limit sub-commands can be combined.

#### Sub-commands:

| Sub-command | Argument | Description |
|-------------|---------|-------------|
| `depth N` | integer | Search exactly N plies deep, then stop |
| `movetime N` | milliseconds | Search for at most N milliseconds |
| `nodes N` | integer | Search at most N nodes |
| `infinite` | — | Search until `stop` is received |
| `wtime N` | milliseconds | White's remaining clock time |
| `btime N` | milliseconds | Black's remaining clock time |
| `winc N` | milliseconds | White's increment per move |
| `binc N` | milliseconds | Black's increment per move |
| `movestogo N` | integer | Moves remaining until next time control |

**Time management:** If `wtime`/`btime` are given without `depth`,
`movetime`, or `nodes`, the engine calculates a time allocation
automatically:
- With `movestogo`: allocates `time / (movestogo + 1) + increment`
- Without `movestogo`: allocates `time / 30 + increment`
- Always caps to leave at least 50 ms remaining on the clock.

**Default:** If `go` is sent with no arguments, the engine defaults to
`depth 6`.

**Examples:**
```
go depth 10
go movetime 1000
go nodes 500000
go infinite
go wtime 60000 btime 60000 winc 1000 binc 1000
go wtime 60000 btime 60000 movestogo 20
```

---

### `stop`

**Purpose:** Interrupt a search that is running (started by `go infinite`
or a long `go depth`/`movetime`). The engine stops at the next safe point
and emits a `bestmove` based on the deepest completed iteration.

**When to use:** After `go infinite`, or to cut short a slow search.

**Syntax:** `stop`

**Response:** `bestmove <move>` (possibly with `ponder <move>`)

**Example:**
```
go infinite
(wait a moment)
stop
bestmove e2e4 ponder e7e5
```

---

### `quit`

**Purpose:** Shut down the engine cleanly. Stops any active search,
then exits with code 0.

**Syntax:** `quit`

**Response:** None (process exits).

---

## 4. Output Responses (what the engine sends back)

### `id name <string>`
Engine name. Sent in response to `uci`.

### `id author <string>`
Author name. Sent in response to `uci`.

### `option name <name> type <type> default <value> [min <n>] [max <n>]`
Declares a configurable option. Sent in response to `uci` before `uciok`.

### `uciok`
Confirms the UCI handshake is complete. Sent at the end of the `uci` response.

### `readyok`
Confirms the engine is ready to receive work. Sent in response to `isready`.

### `info ...`
Progress update during search. Sent after each completed depth of
iterative deepening. Fields:

| Field | Description |
|-------|-------------|
| `depth N` | Current search depth in plies |
| `score cp N` | Evaluation in centipawns from the side-to-move's perspective. Positive = good for the side to move. |
| `score mate N` | Engine sees forced mate in N moves. Positive = side to move delivers mate. Negative = side to move gets mated. |
| `time N` | Time elapsed since search started, in milliseconds |
| `nodes N` | Total nodes searched so far |
| `nps N` | Nodes per second |
| `hashfull N` | Transposition table occupancy in per-mille (0–1000). 500 = 50% full. |
| `pv <move> ...` | Principal variation — the best line found so far |

**Example `info` lines from a depth-6 search:**
```
info depth 1 score cp 110 time 0 nodes 21 nps 0 hashfull 0 pv b1c3
info depth 2 score cp 80 time 1 nodes 148 nps 148000 hashfull 0 pv e2e4 b8c6
info depth 3 score cp 116 time 15 nodes 1706 nps 113733 hashfull 0 pv b1c3 b8c6 g1f3
info depth 4 score cp 86 time 70 nodes 8412 nps 120171 hashfull 0 pv b1c3 b8c6 e2e4 g8f6
info depth 5 score cp 112 time 259 nodes 35225 nps 136003 hashfull 0 pv b1c3 b8c6 e2e4 g8f6 g1f3
info depth 6 score cp 90 time 1436 nodes 169908 nps 118320 hashfull 16 pv b1c3 b8c6 d2d4 d7d5 g1f3 g8f6
```

### `bestmove <move> [ponder <move>]`
The engine's recommended move. Sent after search completes or `stop` is received.

- `<move>` is in UCI coordinate notation: `e2e4`, `g7g8q` (queen promotion).
- `(none)` is returned for checkmate and stalemate positions (no legal moves).
- `ponder <move>` is the engine's expected opponent reply, if available.

**Examples:**
```
bestmove d2d4 ponder d7d5
bestmove h5f7
bestmove (none)
```

---

## 5. Move Notation

All moves use UCI coordinate notation:

| Move type | Example | Description |
|-----------|---------|-------------|
| Normal | `e2e4` | Piece on e2 moves to e4 |
| Capture | `d5e6` | Piece on d5 captures on e6 |
| Castling | `e1g1` | White castles kingside (king moves e1→g1) |
| Castling | `e1c1` | White castles queenside (king moves e1→c1) |
| Castling | `e8g8` | Black castles kingside |
| En passant | `e5d6` | Pawn on e5 captures en passant to d6 |
| Promotion | `g7g8q` | Pawn promotes to queen |
| Promotion | `a2a1n` | Pawn promotes to knight |

Promotion suffix characters: `q` queen, `r` rook, `b` bishop, `n` knight.

---

## 6. Complete Session Examples

### Example 1 — Simple depth-limited analysis

```
uci
isready
ucinewgame
position startpos moves e2e4 e7e5
go depth 8
(engine returns info lines then:)
bestmove g1f3 ponder g8f6
quit
```

### Example 2 — Time-controlled play (30 seconds each side)

```
uci
isready
ucinewgame
position startpos
go wtime 30000 btime 30000
bestmove d2d4 ponder d7d5
position startpos moves d2d4 d7d5
go wtime 28500 btime 29800
bestmove c2c4 ponder e7e6
quit
```

### Example 3 — NNUE enabled, custom position

```
uci
setoption name Hash value 32
setoption name EvalFile value networks/nn-04cf2b4ed1da.nnue
isready
ucinewgame
position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
go depth 6
bestmove e2a6 ponder b4c3
quit
```

### Example 4 — Infinite search with manual stop

```
uci
isready
position startpos
go infinite
(engine streams info lines...)
stop
bestmove d2d4 ponder g8f6
quit
```

### Example 5 — Mate-in-one detection

```
uci
isready
position fen r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4
go depth 3
info depth 1 score mate 1 time 0 nodes 9 nps 0 hashfull 0 pv h5f7
info depth 2 score mate 1 time 0 nodes 9 nps 0 hashfull 0 pv h5f7
bestmove h5f7
quit
```

---

## 7. Diagnostic Messages

All diagnostic messages appear on **stderr only**, prefixed with
`info string`. They are never sent to stdout and will not interfere
with UCI communication.

| Stderr message | Meaning |
|---------------|---------|
| `info string NNUE probe: loading <path>` | NNUE file is being loaded |
| `info string NNUE loaded: nn-... (HalfKP...)` | NNUE loaded successfully |
| `info string NNUE load failed — keeping handcrafted evaluator` | File missing or incompatible |
| `info string Switched to NNUE evaluator: ...` | EvalFile option applied |
| `info string EvalFile cleared — using handcrafted evaluator` | Reverted to built-in eval |
| `info string Error: invalid FEN (...)` | Malformed FEN in position command |
| `info string Error: illegal move: <move>` | Move in position command is not legal |
| `info string Warning: unrecognized option '<name>'` | Unknown setoption name |

---

## 8. UCI GUIs

The engine is compatible with any UCI-compliant GUI. Recommended free
options:

| GUI | Platform | Download |
|-----|---------|----------|
| Arena | Windows/Linux | http://www.playwitharena.de |
| CuteChess | Linux/Windows/macOS | https://cutechess.com |
| BanksiaGUI | Linux/Windows/macOS | https://banksiagui.com |
| Scid vs PC | Linux/Windows | http://scidvspc.sourceforge.net |

In each GUI, point the engine path to `./build/chess_engine` (Linux) or
`.\build\chess_engine.exe` (Windows, once a Windows build is verified).
