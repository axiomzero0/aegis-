// ============================================================
// core/SparseSet.h — O(1) insert/remove/clear set for dense NodeId sets.
// ============================================================
// Law: Rule 56 — "Ban std::set, std::unordered_set, and std::vector<bool>
//       for dataflow analysis. Use Sparse Sets (for small, dense sets)
//       or BitVectors (for large, sparse sets)."
//
// Sparse-set data structure (Briggs, 1993): a dense array of elements
// plus a sparse map from element -> index-in-dense. O(1) insert, O(1)
// remove, O(1) clear via the "epoch" trick (no per-element scan on
// clear), and O(N) iteration in contiguous cache-friendly memory.
// ============================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aegis {

class SparseSet {
public:
    SparseSet() = default;
    explicit SparseSet(uint32_t universe_size) { resize(universe_size); }

    // Pre-allocate for a universe of [0, universe_size).
    void resize(uint32_t universe_size) {
        sparse_.assign(universe_size, SparseEntry{kSentinel, 0});
        dense_.clear();
        dense_.reserve(universe_size);
        epoch_ = 1;
    }

    // O(1) insert.
    bool insert(uint32_t v) noexcept {
        [[assume(v < sparse_.size())]];
        auto& e = sparse_[v];
        if (e.epoch == epoch_ && e.idx != kSentinel) return false;
        e.epoch = epoch_;
        e.idx   = static_cast<uint32_t>(dense_.size());
        dense_.push_back(v);
        return true;
    }

    // O(1) removal.
    bool erase(uint32_t v) noexcept {
        if (!contains(v)) return false;
        auto& e = sparse_[v];
        uint32_t idx = e.idx;
        uint32_t last = dense_.back();
        dense_[idx] = last;
        sparse_[last].idx = idx;
        dense_.pop_back();
        e.idx = kSentinel;
        e.epoch = 0; // invalidate
        return true;
    }

    [[nodiscard]] bool contains(uint32_t v) const noexcept {
        if (v >= sparse_.size()) return false;
        const auto& e = sparse_[v];
        return e.epoch == epoch_ && e.idx != kSentinel;
    }

    // O(1) clear — bump epoch. New inserts get new epoch. Old sparse
    // entries are invisible because their epoch no longer matches.
    void clear() noexcept {
        ++epoch_;
        dense_.clear();
        // On epoch overflow (every ~4 billion clears, never in practice),
        // fall back to a full reset to avoid spurious matches.
        if (epoch_ == 0) {
            for (auto& e : sparse_) { e.idx = kSentinel; e.epoch = 0; }
            epoch_ = 1;
        }
    }

    [[nodiscard]] uint32_t size() const noexcept {
        return static_cast<uint32_t>(dense_.size());
    }
    [[nodiscard]] bool empty() const noexcept { return dense_.empty(); }

    // Iteration over the dense array.
    [[nodiscard]] const uint32_t* begin() const noexcept { return dense_.data(); }
    [[nodiscard]] const uint32_t* end() const noexcept { return dense_.data() + dense_.size(); }
    [[nodiscard]] uint32_t operator[](size_t i) const noexcept { return dense_[i]; }

private:
    static constexpr uint32_t kSentinel = 0xFFFFFFFFu;

    struct SparseEntry {
        uint32_t idx{kSentinel};
        uint32_t epoch{0};
    };

    std::vector<SparseEntry> sparse_{};
    std::vector<uint32_t>    dense_{};
    uint32_t                 epoch_{1};
};

} // namespace aegis
