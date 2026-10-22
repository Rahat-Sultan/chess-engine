#pragma once

/**
 * transposition.hpp — Transposition table (search cache)
 *
 * Phase 5: Resource and performance baseline.
 *
 * Caches previously searched positions keyed by Zobrist hash so the
 * search can:
 *   - Cut off nodes already searched at sufficient depth (alpha/beta bounds).
 *   - Improve move ordering by trying the stored best move first.
 *
 * Memory:
 *   - Configurable size in megabytes (powers of two internally).
 *   - Each entry is 16 bytes (see TTEntry layout below).
 *   - Index = key & (num_entries - 1); uses the lower bits as a direct
 *     address into a flat array — no hashing, no collision chains.
 *
 * Replacement:
 *   - Generation-based: entries from older search generations are
 *     preferred for replacement. Within the same generation, deeper
 *     entries are kept. This is a simple, effective scheme.
 *
 * Thread safety:
 *   - NOT thread-safe. The table is used from the single search thread.
 *     If multi-threaded search is added later, each thread should have its
 *     own table or access must be synchronized.
 */

#include "chess/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chess {

// ─── Bound types ──────────────────────────────────────────────────────────────

enum BoundType : uint8_t {
    BOUND_NONE  = 0,  // empty / invalid entry
    BOUND_EXACT = 1,  // PV node — exact score
    BOUND_LOWER = 2,  // beta cutoff — score is a lower bound
    BOUND_UPPER = 3,  // alpha not raised — score is an upper bound
};

// ─── Packed move ──────────────────────────────────────────────────────────────
//
// Packs a Move into 16 bits: from(6) | to(6) | promo(3) | flag(1)
// promo: 0=none, 1=knight, 2=bishop, 3=rook, 4=queen
// flag: 1 if the move is a promotion (distinguishes promo from non-promo)

constexpr uint16_t pack_move(const Move& m) noexcept {
    if (!m.is_valid()) return 0;
    uint16_t packed = static_cast<uint16_t>(
        (static_cast<uint16_t>(m.from) << 10) |
        (static_cast<uint16_t>(m.to) << 4));

    if (m.is_promotion()) {
        uint16_t promo = 0;
        switch (m.promotion_piece) {
            case PieceType::Knight: promo = 1; break;
            case PieceType::Bishop: promo = 2; break;
            case PieceType::Rook:   promo = 3; break;
            case PieceType::Queen:  promo = 4; break;
            default: break;
        }
        packed |= (promo << 1) | 1;  // set promotion flag bit 0
    }
    return packed;
}

inline Move unpack_move(uint16_t packed) noexcept {
    if (packed == 0) return NULL_MOVE;
    Move m;
    m.from = (packed >> 10) & 0x3F;
    m.to   = (packed >> 4)  & 0x3F;
    if (packed & 1) {  // promotion flag
        m.flags |= MF_PROMOTION;
        uint16_t promo = (packed >> 1) & 0x7;
        switch (promo) {
            case 1: m.promotion_piece = PieceType::Knight; break;
            case 2: m.promotion_piece = PieceType::Bishop; break;
            case 3: m.promotion_piece = PieceType::Rook;   break;
            case 4: m.promotion_piece = PieceType::Queen;  break;
            default: break;
        }
    }
    return m;
}

// ─── TT entry (16 bytes) ──────────────────────────────────────────────────────
//
// Layout:
//   key        8 bytes  — full Zobrist key for verification
//   score      2 bytes  — score in centipawns (int16_t)
//   depth      1 byte  — depth remaining when stored (uint8_t, 0–255)
//   bound      1 byte  — BOUND_EXACT / BOUND_LOWER / BOUND_UPPER
//   move       2 bytes  — packed best move (for ordering)
//   generation 1 byte  — search generation (for replacement)
//   (1 byte padding → 16 total)

struct TTEntry {
    uint64_t key        = 0;
    int16_t  score      = 0;
    uint8_t  depth      = 0;
    uint8_t  bound      = BOUND_NONE;
    uint16_t move       = 0;
    uint8_t  generation = 0;

    bool is_empty() const noexcept { return bound == BOUND_NONE; }
};

// ─── TranspositionTable ───────────────────────────────────────────────────────

class TranspositionTable {
public:
    /**
     * Create a table with the given size in megabytes (rounded up to the
     * nearest power-of-two number of entries).
     * Default: 16 MB.
     */
    explicit TranspositionTable(std::size_t size_mb = 16);

    /**
     * Resize the table to a new size in megabytes.
     * All existing entries are discarded.
     */
    void resize(std::size_t size_mb);

    /** Remove all entries (does not deallocate). */
    void clear() noexcept;

    /** Advance the generation counter (call at the start of each search). */
    void new_search() noexcept { ++generation_; }

    /** Current generation counter. */
    uint8_t generation() const noexcept { return generation_; }

    /** Number of entries in the table. */
    std::size_t num_entries() const noexcept { return entries_.size(); }

    /** Approximate memory usage in bytes. */
    std::size_t memory_bytes() const noexcept {
        return entries_.size() * sizeof(TTEntry);
    }

    /**
     * Probe the table for the given Zobrist key.
     * Returns a pointer to the matching entry, or nullptr if not found
     * (or if the entry is empty).
     */
    TTEntry* probe(uint64_t key) noexcept;
    const TTEntry* probe(uint64_t key) const noexcept;

    /**
     * Store a result in the table.
     *
     * @param key      Zobrist key
     * @param score    Score from the search (side-to-move perspective)
     * @param depth    Depth remaining at this node
     * @param bound    Bound type (EXACT, LOWER, UPPER)
     * @param move     Best move found (may be NULL_MOVE for non-PV nodes)
     */
    void store(uint64_t key, int score, int depth, BoundType bound,
               const Move& move);

    /** Approximate hash fullness in per-mille (0–1000). */
    int hash_full_permille() const noexcept;

private:
    std::vector<TTEntry> entries_;
    std::size_t mask_     = 0;   // num_entries - 1, for fast modulo
    uint8_t    generation_ = 0;

    std::size_t size_to_entries(std::size_t size_mb) const noexcept;
    void store_internal(uint64_t key, int score, int depth,
                        BoundType bound, uint16_t packed_move);
};

} // namespace chess
