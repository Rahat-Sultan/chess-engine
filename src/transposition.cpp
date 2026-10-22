/**
 * transposition.cpp — Transposition table implementation (Phase 5)
 *
 * See include/chess/transposition.hpp for the full interface.
 */

#include "chess/transposition.hpp"

#include <algorithm>
#include <cmath>

namespace chess {

// ─── Construction / resize ────────────────────────────────────────────────────

TranspositionTable::TranspositionTable(std::size_t size_mb) {
    resize(size_mb);
}

std::size_t TranspositionTable::size_to_entries(std::size_t size_mb) const noexcept {
    // Each entry is 16 bytes; convert MB → bytes → entry count.
    std::size_t bytes = size_mb * 1024 * 1024;
    std::size_t count = bytes / sizeof(TTEntry);

    // Round down to the nearest power of two so we can use a bitmask.
    if (count < 1) count = 1;
    std::size_t pow2 = 1;
    while (pow2 * 2 <= count) pow2 *= 2;
    return pow2;
}

void TranspositionTable::resize(std::size_t size_mb) {
    size_mb = std::max<std::size_t>(1, size_mb);
    std::size_t count = size_to_entries(size_mb);
    entries_.resize(count);
    mask_ = count - 1;
    clear();
}

void TranspositionTable::clear() noexcept {
    for (auto& e : entries_) {
        e = TTEntry{};
    }
}

// ─── Probe ────────────────────────────────────────────────────────────────────

TTEntry* TranspositionTable::probe(uint64_t key) noexcept {
    TTEntry& entry = entries_[key & mask_];
    if (entry.key == key && entry.bound != BOUND_NONE) {
        return &entry;
    }
    return nullptr;
}

const TTEntry* TranspositionTable::probe(uint64_t key) const noexcept {
    const TTEntry& entry = entries_[key & mask_];
    if (entry.key == key && entry.bound != BOUND_NONE) {
        return &entry;
    }
    return nullptr;
}

// ─── Store ─────────────────────────────────────────────────────────────────────

void TranspositionTable::store_internal(uint64_t key, int score, int depth,
                                         BoundType bound, uint16_t packed_move) {
    TTEntry& entry = entries_[key & mask_];

    // Replacement strategy (generation-based):
    //   - Always replace empty slots.
    //   - Replace if the existing entry is from an older generation.
    //   - Within the same generation, replace if the new entry has
    //     greater or equal depth (deeper searches are more valuable).
    bool replace = entry.bound == BOUND_NONE
                || entry.key == key
                || entry.generation != generation_
                || depth >= static_cast<int>(entry.depth);

    if (!replace) return;

    entry.key        = key;
    entry.score      = static_cast<int16_t>(std::clamp(score, -32767, 32767));
    entry.depth      = static_cast<uint8_t>(std::clamp(depth, 0, 255));
    entry.bound      = bound;
    entry.move       = packed_move;
    entry.generation = generation_;
}

void TranspositionTable::store(uint64_t key, int score, int depth,
                                BoundType bound, const Move& move) {
    store_internal(key, score, depth, bound, pack_move(move));
}

// ─── Statistics ────────────────────────────────────────────────────────────────

int TranspositionTable::hash_full_permille() const noexcept {
    // Sample the first 1000 entries for an approximation.
    std::size_t sample = std::min<std::size_t>(1000, entries_.size());
    if (sample == 0) return 0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < sample; ++i) {
        if (entries_[i].bound != BOUND_NONE &&
            entries_[i].generation == generation_) {
            ++used;
        }
    }
    return static_cast<int>((used * 1000) / sample);
}

} // namespace chess
