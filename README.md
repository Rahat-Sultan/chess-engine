# Lightweight Chess Engine

An original, modular chess engine written in C++17.
It communicates through the Universal Chess Interface (UCI), is designed
to run on modest CPU-only hardware, and is exposed to application
backends through an HTTP/JSON adapter.

**v0.1.0 — all eight phases complete and tested.**

---

## Project status

| Phase | Description                          | Status      |
|-------|--------------------------------------|-------------|
| 0     | Environment & build scaffold         | ✅ Done     |
| 1     | Types, board representation, FEN     | ✅ Done     |
| 2     | Move generation & rules              | ✅ Done     |
| 3     | Evaluation & search                  | ✅ Done     |
| 4     | UCI protocol controller              | ✅ Done     |
| 5     | Resource limits & performance tuning | ✅ Done     |
| 6     | HTTP/JSON adapter                    | ✅ Done     |
| 7     | NNUE integration                     | ✅ Done     |
| 8     | Integration & handoff                | ✅ Done     |

### What is in v0.1.0

| Component | Detail |
|-----------|--------|
| C++ chess engine | Original implementation, UCI-compatible executable |
| Move generation | Full legal-move generation; perft-verified |
| Search | Iterative deepening, alpha-beta, quiescence search, TT |
| Evaluator | Handcrafted (default) + HalfKP NNUE (opt-in via EvalFile) |
| Transposition table | Configurable 1–1024 MB; Zobrist hashing; generation-based replacement |
| HTTP adapter | FastAPI `GET /health` + `POST /analyze` |
| Test suites | 7 C++ test suites + 23 API tests; all passing |
| Performance baseline | ~90–176K NPS; depth-6 in 0.1–17 s; ~21 MB RAM at 16 MB hash |

### Documentation index

| Document | Purpose |
|----------|---------|
| [`docs/HANDOFF.md`](docs/HANDOFF.md) | **Start here for backend integration** |
| [`docs/HTTP_API.md`](docs/HTTP_API.md) | Full HTTP API contract and curl examples |
| [`docs/UCI_COMMANDS.md`](docs/UCI_COMMANDS.md) | **Engine startup guide and all UCI commands** |
| [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md) | Honest list of limitations and next steps |
| [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) | Performance baseline and safe defaults |
| [`docs/NNUE_GUIDE.md`](docs/NNUE_GUIDE.md) | NNUE integration guide |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Architecture decisions log |
| [`docs/PROJECT_SPEC.md`](docs/PROJECT_SPEC.md) | Full original specification |

---

## Requirements

| Tool | Minimum version | Notes |
|------|----------------|-------|
| g++ / clang++ | C++17 | `build-essential` on Ubuntu |
| CMake | 3.20 | `pip install --user cmake` if system package is old |
| Ninja | any | `pip install --user ninja` or `apt` |
| Git | any | |
| Python 3 | 3.8+ | For HTTP adapter and scripts |

### Ubuntu quick-install

```bash
sudo apt update
sudo apt install build-essential git python3 python3-pip curl
pip3 install --user --break-system-packages cmake ninja
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc && source ~/.bashrc
```

---

## Build

```bash
# Configure and build (Release)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Executable:
./build/chess_engine
```

### Debug build with sanitizers

```bash
cmake -S . -B build-debug -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DENABLE_ASAN=ON \
      -DENABLE_UBSAN=ON
cmake --build build-debug
```

---

## Run (UCI)

```bash
./build/chess_engine
```

Then type UCI commands:

```
uci
isready
position startpos
go depth 6
quit
```

---

## Tests

```bash
# All C++ test suites (7 total)
ctest --test-dir build --output-on-failure

# UCI protocol smoke test
python3 scripts/uci_smoke_test.py ./build/chess_engine

# API tests (requires .venv active)
source .venv/bin/activate
pytest api/test_api.py -v

# Performance benchmark
python3 scripts/benchmark.py --depth 6 --hash 16
```

---

## HTTP API

### Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r api/requirements.txt
```

### Start

```bash
source .venv/bin/activate
uvicorn api.app:app --host 0.0.0.0 --port 8000
```

### Quick test

```bash
curl http://localhost:8000/health

curl -X POST http://localhost:8000/analyze \
  -H "Content-Type: application/json" \
  -d '{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
       "limits": {"depth": 6}}'
```

See [`docs/HANDOFF.md`](docs/HANDOFF.md) for full integration
instructions, error behavior examples, and the configuration reference.

---

## NNUE

The engine ships with a HalfKP 256x2-32-32 evaluator. The network file
is not committed to Git. Download it with:

```bash
bash scripts/download_network.sh
```

Enable at runtime:

```
setoption name EvalFile value networks/nn-04cf2b4ed1da.nnue
```

The engine falls back to the handcrafted evaluator if the file is absent.
See [`docs/NNUE_GUIDE.md`](docs/NNUE_GUIDE.md) for details.

---

## Repository layout

```
chess_engine/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── docs/
│   ├── HANDOFF.md           # ← Backend integration guide
│   ├── HTTP_API.md          # REST API contract
│   ├── UCI_COMMANDS.md      # ← Engine startup and all UCI commands
│   ├── KNOWN_LIMITATIONS.md # Honest limitations list
│   ├── BENCHMARKS.md        # Performance baseline
│   ├── NNUE_GUIDE.md        # NNUE integration guide
│   ├── DECISIONS.md         # Architecture decisions log
│   └── PROJECT_SPEC.md      # Full specification
├── include/chess/           # Public headers
│   ├── types.hpp
│   ├── board.hpp
│   ├── movegen.hpp
│   ├── evaluation.hpp
│   ├── nnue_evaluator.hpp
│   ├── search.hpp
│   ├── transposition.hpp
│   └── uci.hpp
├── src/                     # C++ implementation
│   ├── main.cpp
│   ├── board.cpp
│   ├── movegen.cpp
│   ├── evaluation.cpp
│   ├── nnue_evaluator.cpp
│   ├── search.cpp
│   ├── transposition.cpp
│   ├── uci.cpp
│   └── nnue_probe/          # Vendored nnue-probe (GPL-3, Daniel Shawul)
├── tests/
│   ├── unit/                # Custom unit test framework
│   ├── perft/               # Perft count verification
│   └── uci/                 # UCI protocol tests
├── scripts/
│   ├── uci_smoke_test.py
│   ├── benchmark.py
│   └── download_network.sh
├── api/                     # HTTP/JSON adapter (Phase 6)
│   ├── app.py
│   ├── engine_manager.py
│   ├── test_api.py
│   └── requirements.txt
└── networks/                # NNUE weights (not in Git — use download script)
```

---

## Architecture

```
Backend (HTTP/JSON)
      │
  FastAPI Adapter (api/app.py)
      │  subprocess / UCI pipes
  UCI Controller (uci.cpp)
      │
  Chess Engine Core
  ├── Board — 64-square array, FEN, make/unmake, Zobrist hash
  ├── Move Generator — legal moves, perft-verified
  ├── Search — iterative deepening, alpha-beta, quiescence, TT
  └── Evaluator Interface
        ├── HandcraftedEvaluator — material + PST + mobility (default)
        └── NNUEEvaluator — HalfKP 256x2-32-32, opt-in via EvalFile
```

---

## License

The chess engine core is original code. License to be confirmed by the
project lead before public release.

The vendored `src/nnue_probe/` library is by Daniel Shawul under GPL-3.
The NNUE network file (`nn-04cf2b4ed1da.nnue`) is from the Stockfish
project under GPL-3. Lead approval for use in this project was granted
in October 2026. See [`docs/DECISIONS.md`](docs/DECISIONS.md) for
the full attribution record.
