# Lightweight Chess Engine

An original, modular chess engine written in C++.  
It communicates through the Universal Chess Interface (UCI) and is
designed to run on modest CPU-only hardware.  
A later phase will add an HTTP/JSON adapter for backend integration.

---

## Project status

| Phase | Description                          | Status      |
|-------|--------------------------------------|-------------|
| 0     | Environment & build scaffold         | ✅ Done     |
| 1     | Types, board representation, FEN     | ✅ Done     |
| 2     | Move generation & rules              | ✅ Done     |
| 3     | Evaluation & search                  | ✅ Done     |
| 4     | UCI protocol controller              | ✅ Done     |
| 5     | Resource limits & performance tuning | 🔲 Pending  |
| 6     | HTTP/JSON adapter                    | 🔲 Pending  |
| 7     | NNUE integration (optional)          | 🔲 Pending  |
| 8     | Integration & handoff                | 🔲 Pending  |

See [`docs/PROJECT_SPEC.md`](docs/PROJECT_SPEC.md) for the full
specification and [`docs/DECISIONS.md`](docs/DECISIONS.md) for open
architecture questions.

---

## Requirements

| Tool          | Minimum version | Notes                                      |
|---------------|-----------------|--------------------------------------------|
| g++ / clang++ | C++17           | `build-essential` on Ubuntu                |
| CMake         | 3.20            | Install via `pip install --user cmake` if  |
|               |                 | the system package is too old              |
| Ninja         | any             | `pip install --user ninja` or `apt`        |
| Git           | any             |                                            |
| Python 3      | 3.8+            | For test scripts                           |

### Ubuntu quick-install

```bash
sudo apt update
sudo apt install build-essential git python3 python3-pip curl
# cmake and ninja via pip if the apt versions are too old:
pip3 install --user --break-system-packages cmake ninja
```

Add `~/.local/bin` to your `PATH` if it is not already there:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

---

## Build

```bash
# Configure (first time or after CMakeLists changes)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# The executable is at:
./build/chess_engine
```

### Debug build with sanitizers

```bash
cmake -S . -B build-debug -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_ASAN=ON \
      -DENABLE_UBSAN=ON
cmake --build build-debug
./build-debug/chess_engine
```

---

## Run

```bash
# Start the engine (reads UCI commands from stdin)
./build/chess_engine

# Quick smoke-test: type these commands at the prompt
uci
isready
quit
```

Expected output:

```
id name LightweightChessEngine
id author Chess Engine Intern
uciok
readyok
```

---

## Tests

```bash
# Run the full test suite via CTest
ctest --test-dir build --output-on-failure

# Python UCI smoke-test (Phase 4+)
python3 scripts/uci_smoke_test.py ./build/chess_engine

# Perft verification (Phase 2+)
python3 scripts/run_perft.py ./build/chess_engine
```

---

## Repository layout

```
chess_engine/
├── CMakeLists.txt          # Top-level build definition
├── README.md
├── LICENSE
├── .gitignore
├── docs/
│   ├── PROJECT_SPEC.md     # Full specification (source of truth)
│   ├── ARCHITECTURE.md     # Module diagram and design notes
│   ├── UCI_PROTOCOL.md     # UCI command reference
│   ├── HTTP_API.md         # REST API contract (Phase 6)
│   ├── TESTING.md          # How to run and extend tests
│   └── DECISIONS.md        # Open questions and resolved decisions
├── include/chess/          # Public headers (one per module)
│   ├── types.hpp
│   ├── board.hpp
│   ├── movegen.hpp
│   ├── notation.hpp
│   ├── evaluation.hpp
│   ├── search.hpp
│   ├── transposition.hpp
│   └── uci.hpp
├── src/                    # Implementation files
│   └── main.cpp
├── tests/
│   ├── unit/               # Google Test / Catch2 unit tests
│   ├── perft/              # Perft count verification
│   ├── uci/                # UCI protocol tests
│   └── integration/        # End-to-end tests
├── scripts/
│   ├── run_perft.py
│   ├── uci_smoke_test.py
│   └── benchmark.py
├── api/                    # HTTP adapter (Phase 6)
└── networks/               # NNUE weights (Phase 7, if approved)
```

---

## Architecture overview

```
UCI stdin/stdout
      │
  UCI Controller
      │
  Chess Engine Core
  ├── Board (position, FEN, make/unmake)
  ├── Move Generator (legal moves, perft)
  ├── Search (iterative deepening, alpha-beta)
  └── Evaluator Interface
        ├── Handcrafted Evaluator (initial)
        └── NNUE Evaluator (optional, Phase 7)
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full diagram.

---

## License

TBD — confirm with the project lead before publishing.  
Do not commit third-party engine code or NNUE weights without explicit
license review. See [`docs/DECISIONS.md`](docs/DECISIONS.md).
