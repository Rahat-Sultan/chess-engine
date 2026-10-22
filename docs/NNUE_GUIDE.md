# Phase 7 — NNUE Integration Guide

**Read this document in full before writing any code or downloading any
files.** Phase 7 is gated on lead approval. The steps below tell you
exactly what to confirm, where to get the network, and how to implement
the evaluator.

---

## 1. What is NNUE?

NNUE stands for **Efficiently Updatable Neural Network**. It is a small,
fast neural network used as a chess position evaluator. The key idea:

- The network takes a feature vector describing the board position and
  outputs a score in centipawns.
- Because most features change by only a few bits when a move is made,
  the network can be updated *incrementally* — much faster than
  recomputing from scratch every node.
- NNUE is a **position evaluator**, not a search algorithm. Our existing
  iterative-deepening alpha-beta search stays unchanged. Only the leaf
  evaluation function is replaced.

Our codebase already has the right design for this:

```
Search (unchanged)
  └── Evaluator interface  (include/chess/evaluation.hpp)
        ├── HandcraftedEvaluator  (current, always available as fallback)
        └── NNUEEvaluator         (Phase 7 addition)
```

---

## 2. Before You Start — Required Approvals

The project spec (§2, §8, D-4) requires explicit lead sign-off on all
three items below. **Do not proceed to Step 3 until all three are
confirmed in writing.**

| # | What to confirm | Why it matters |
|---|-----------------|---------------|
| A | Is an original NNUE implementation required, or is a compatible pretrained network permitted? | Determines whether you train your own network or download Stockfish weights |
| B | Are Stockfish `.nnue` files permitted — license and attribution reviewed? | Stockfish networks are GPL-3 compatible but attribution is required |
| C | What is the RAM budget on the lowest-spec target machine? | The smallest usable HalfKP network is ~34 MB; larger nets (HalfKAv2-1024) are 90–200 MB+ |

Record each answer in `docs/DECISIONS.md` before continuing.

---

## 3. Understanding the Options

### Option A — Use a Stockfish-compatible pretrained network

The official Stockfish networks repository publishes `.nnue` files that
your engine can load, **provided you implement the same feature encoding
and network architecture**. A Stockfish `.nnue` file is not a generic
neural model — it encodes specific HalfKP (or HalfKAv2) features and
a specific network topology. If your inference code does not match
exactly, scores will be garbage.

Realistic architectures to consider for an original engine:

| Architecture | File size | RAM | Difficulty to implement | Notes |
|--------------|-----------|-----|-------------------------|-------|
| HalfKP (768→256×2→32→32→1) | ~34 MB | ~36 MB | Medium | Older Stockfish nets; well-documented |
| HalfKAv2 (45522→512×2→16→32→1) | ~65 MB | ~68 MB | Hard | Current Stockfish architecture |
| Simple custom net (768→64×2→1) | ~1 MB | ~2 MB | Easy | Self-trained; no licensing concern |

**Recommendation:** Start with a simple custom architecture trained with
your own self-play data, or implement the HalfKP architecture if
compatibility with a pretrained Stockfish network has been approved.

### Option B — Train a small original network

Use your own self-play games or publicly available training data
(e.g. the Lichess open database). Train a compact network with a
framework such as PyTorch, then export it to a binary format your
C++ code can load. No external licensing concerns.

This guide covers the integration steps for either option.

---

## 4. Where to Get a Pretrained Network (if Option A is approved)

> **Prerequisite:** Lead approval confirmed (see Step 2).

### 4.1 Official Stockfish networks repository

URL: `https://github.com/official-stockfish/networks`

The repository lists every network used in official Stockfish builds.
The README shows the current recommended network and links to the test
pages where each network's strength is documented.

### 4.2 Identify the right network file

You need to pick a network that:
1. Has a known, documented architecture (HalfKP or HalfKAv2).
2. Has licensing terms your project can satisfy.
3. Fits within the RAM budget confirmed in Step 2.

The test page at `https://tests.stockfishchess.org/nns` lists every
network with architecture tags. Look for entries tagged `HalfKP` or
`HalfKAv2`.

### 4.3 Download the network file

Each network filename encodes its hash, e.g.:

```
nn-5af11540bbfe.nnue       (HalfKP, ~34 MB)
nn-eba324f53044.nnue       (HalfKAv2-1024, ~65 MB)
```

You can download a specific file from:

```
https://tests.stockfishchess.org/api/nn/<filename>
```

Example:

```bash
# Create the networks directory
mkdir -p networks

# Download a HalfKP network (~34 MB) — confirm this is the approved file first
curl -L "https://tests.stockfishchess.org/api/nn/nn-5af11540bbfe.nnue" \
     -o networks/nn-5af11540bbfe.nnue

# Verify the download (SHA-256 should match the filename prefix)
sha256sum networks/nn-5af11540bbfe.nnue
```

> **Do not commit the `.nnue` file to Git.** The `.gitignore` already
> excludes `networks/*.nnue`. Store the filename in a config variable
> or download script instead.

---

## 5. Step-by-Step Integration Plan

### Step 1 — Add the NNUEEvaluator class declaration

Create `include/chess/nnue.hpp`:

```cpp
#pragma once

/**
 * nnue.hpp — NNUE-based position evaluator (Phase 7)
 *
 * Implements the Evaluator interface from evaluation.hpp.
 * Loads a binary network file on construction, falls back gracefully
 * if the file is missing or incompatible.
 *
 * Architecture supported: HalfKP (768 input → 256×2 → 32 → 32 → 1)
 * Score convention: identical to HandcraftedEvaluator — side-to-move positive.
 */

#include "chess/board.hpp"
#include "chess/evaluation.hpp"

#include <string>

namespace chess {

class NNUEEvaluator : public Evaluator {
public:
    /**
     * Load the network from a binary file.
     * Throws std::runtime_error if the file is missing, unreadable,
     * or has an incompatible format.
     */
    explicit NNUEEvaluator(const std::string& network_path);

    int evaluate(Board& board) override;

    /** True if the network loaded successfully. */
    bool is_loaded() const noexcept { return loaded_; }

    /** Path used to load the network. */
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    bool        loaded_ = false;

    // Network weights (populated during load)
    // ... (defined in implementation — layout depends on architecture)

    // Feature extraction
    void compute_features(const Board& board,
                          float* white_features,
                          float* black_features) const noexcept;

    // Inference
    int forward(const float* white_features,
                const float* black_features,
                bool white_to_move) const noexcept;

    // Load the binary network file
    void load(const std::string& path);
};

} // namespace chess
```

---

### Step 2 — Implement HalfKP feature extraction

HalfKP features encode the board from each king's perspective. Each
feature is a (king_square, piece_type, piece_color, piece_square) tuple.

There are 64 king squares × 64 piece squares × 10 piece types (5 types
× 2 colors, excluding the king) = 40,960 possible features per side.

Feature index for one side:

```
idx = king_sq * 640 + piece_type_idx * 64 + piece_sq
```

Where `piece_type_idx` maps (color, type) → 0..9:

```
White Pawn=0, White Knight=1, White Bishop=2, White Rook=3, White Queen=4
Black Pawn=5, Black Knight=6, Black Bishop=7, Black Rook=8, Black Queen=9
```

In `src/nnue.cpp`:

```cpp
void NNUEEvaluator::compute_features(
    const Board& board,
    float* white_features,   // 40960 floats, zeroed by caller
    float* black_features    // 40960 floats, zeroed by caller
) const noexcept
{
    Square wk = board.king_square(Color::White);
    Square bk = board.king_square(Color::Black);

    for (Square s = 0; s < 64; ++s) {
        Piece p = board.piece_at(s);
        if (p.is_empty() || p.type == PieceType::King) continue;

        int pt_idx = (p.color == Color::Black ? 5 : 0)
                   + static_cast<int>(p.type) - 1;  // Pawn=0..Queen=4

        // White king's perspective
        white_features[wk * 640 + pt_idx * 64 + s]      = 1.0f;
        // Black king's perspective (mirror piece square)
        int bk_mirror = (7 - bk / 8) * 8 + (bk % 8);
        int s_mirror  = (7 - s  / 8) * 8 + (s  % 8);
        int bpt_idx   = (p.color == Color::White ? 5 : 0)
                      + static_cast<int>(p.type) - 1;
        black_features[bk_mirror * 640 + bpt_idx * 64 + s_mirror] = 1.0f;
    }
}
```

> **Note:** This is the reference architecture. You must match it
> exactly to use Stockfish-compatible weights. If you train your own
> network you can choose any feature encoding.

---

### Step 3 — Implement network loading

The binary format for a HalfKP network is:
1. 4-byte magic / version header
2. Architecture description string (variable length)
3. Weights for each layer in row-major float32 order

The exact byte layout differs slightly between Stockfish versions. Use
the Stockfish source (or the community-maintained NNUEReader tools) to
understand the format of your specific file before loading it.

Minimum validation checks in `NNUEEvaluator::load()`:
- Verify file exists and is readable
- Check magic bytes / version match the expected architecture
- Verify expected layer sizes match the declared architecture
- Log an error and throw `std::runtime_error` on any mismatch — never
  silently fall back without logging

---

### Step 4 — Wire into the UCI controller

In `src/uci.cpp`, make the evaluator selection configurable:

```cpp
// In handle_setoption():
else if (name == "EvalFile") {
    eval_file_ = value;
}

// In handle_isready() or handle_ucinewgame():
if (!eval_file_.empty()) {
    try {
        auto nnue = std::make_unique<NNUEEvaluator>(eval_file_);
        evaluator_ = std::move(nnue);
        searcher_.set_evaluator(*evaluator_);
        std::cerr << "info string NNUE loaded: " << eval_file_ << "\n";
    } catch (const std::exception& e) {
        std::cerr << "info string NNUE load failed: " << e.what()
                  << " — falling back to handcrafted evaluator\n";
    }
}
```

Add the UCI option declaration in `handle_uci()`:

```cpp
resp += "option name EvalFile type string default <empty>\n";
```

---

### Step 5 — Incremental accumulator updates (optional performance improvement)

For full performance, maintain two **accumulators** — one per side — that
hold the result of the first linear layer (feature input → hidden layer 0).

On `make_move`: XOR out the features that changed, XOR in the new ones.
On `unmake_move`: restore the previous accumulator from a stack.

This avoids recomputing the full dot product every node. For a 256-neuron
first hidden layer and 40,960 inputs, a full recompute costs ~40,960
floating-point multiplications; an incremental update costs roughly 2×(1–4)
feature-index changes per move.

This optimization requires storing the accumulator in `UndoInfo` or a
side-car stack indexed by ply. Implement only after the non-incremental
version is tested and verified to produce correct scores.

---

### Step 6 — Add a UCI setoption for the network path

Make the network path configurable at runtime so you can test different
networks without recompiling:

```bash
# UCI session
setoption name EvalFile value networks/nn-5af11540bbfe.nnue
isready
position startpos
go depth 10
```

The engine should log to stderr whether NNUE was loaded or fell back to
the handcrafted evaluator. Never log to stdout (that would corrupt UCI).

---

## 6. Testing Requirements

Before declaring Phase 7 complete, verify each item:

| Test | What to check |
|------|---------------|
| Load valid file | Engine starts, logs "NNUE loaded", `isready` returns `readyok` |
| Load missing file | Engine logs the error, falls back to handcrafted evaluator, returns `readyok` |
| Load corrupted file | Same fallback behaviour, no crash |
| Score consistency | Same FEN evaluated twice returns the same score |
| Side-to-move convention | White-advantage position returns positive score when White to move, negative when Black to move |
| Make/unmake integrity | Score from NNUE matches score recomputed from scratch after a series of make/unmake |
| Perft counts unchanged | NNUE evaluator does not change move generation; all perft counts must still match |
| CTest suite | All 6 existing C++ test suites continue to pass |
| Benchmark comparison | Run `scripts/benchmark.py --depth 6` with both evaluators and record NPS and scores |

---

## 7. Licensing Checklist

Before committing any code or files related to NNUE, complete this
checklist:

- [ ] Lead approval confirmed and recorded in `docs/DECISIONS.md`
- [ ] Network file license reviewed (Stockfish: GPL-3 — confirm compatibility with project licence)
- [ ] Attribution added to `README.md` and `docs/DECISIONS.md`
- [ ] Network file excluded from Git (`.gitignore` already covers `networks/*.nnue`)
- [ ] Download instructions documented so any team member can reproduce the build
- [ ] No Stockfish source code copied into the engine (only the network file, if approved)

---

## 8. Quick Reference — Download + First Test

Once approval is in place:

```bash
# 1. Build the engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 2. Download the network (replace filename with the approved network)
mkdir -p networks
curl -L "https://tests.stockfishchess.org/api/nn/<approved-network>.nnue" \
     -o networks/<approved-network>.nnue

# 3. Test via UCI
echo -e "uci\nsetoption name EvalFile value networks/<approved-network>.nnue\nisready\nposition startpos\ngo depth 6\nquit" \
  | ./build/chess_engine

# 4. Run all tests (NNUE must not break existing behaviour)
ctest --test-dir build --output-on-failure

# 5. Compare evaluators
python3 scripts/benchmark.py --depth 6 --hash 32 --json results_nnue.json
```

---

## 9. Useful External Resources

| Resource | URL |
|----------|-----|
| Stockfish networks repository | https://github.com/official-stockfish/networks |
| NNUE technical documentation | https://official-stockfish.github.io/docs/nnue-pytorch-wiki/docs/nnue.html |
| Stockfish network test page | https://tests.stockfishchess.org/nns |
| Gary Linscott's nnue-pytorch trainer | https://github.com/official-stockfish/nnue-pytorch |
| Community NNUE format description | https://github.com/glinscott/nnue-pytorch/blob/master/docs/nnue-pytorch.md |
| Smallbrain — small engine with NNUE example | https://github.com/Disservin/Smallbrain |
| Maia Chess — custom-trained lightweight net | https://github.com/CSSLab/maia-chess |

---

## 10. Summary of Actions

| When | Action |
|------|--------|
| Now | Get lead approval (§2, items A/B/C) |
| After approval | Decide architecture: pretrained HalfKP or custom-trained |
| After decision | Download / prepare the network file |
| Implementation | Add `NNUEEvaluator` behind the `Evaluator` interface |
| Implementation | Add `EvalFile` UCI option |
| Verification | Run tests, benchmark both evaluators |
| Before commit | Complete licensing checklist (§7) |
| Documentation | Update `docs/DECISIONS.md` and `networks/README.md` |
