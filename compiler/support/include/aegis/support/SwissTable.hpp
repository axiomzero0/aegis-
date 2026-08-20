// ============================================================
// core/SwissTable.h — Cache-friendly open-addressing hash map.
// ============================================================
// Law: Rule 55 — "std::unordered_map and std::map are forbidden in the
//       compiler hot path. Use a cache-friendly, open-addressing hash
//       map (e.g., a SwissTable)."
//
// SwissTable-style flat hash map: contiguous byte-array of 8-bit control
// bytes + parallel array of (key, value) slots. SIMD-friendly metadata
// scan; no per-insert heap allocation; all data lives in two contiguous
// buffers, perfect for the CPU prefetcher and compatible with PMR arenas.
//
// Restrictions: K and V must be trivially-destructible (true for
// NodeId / SymbolId / indices, which is the only thing we use here).
// ============================================================
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace aegis {

namespace detail {
// SwissTable control bytes. The high bit (0x80) distinguishes EMPTY /
// DELETED from any "live" control. The low 7 bits store the bottom-7 of
// the hash, enabling 16-way SIMD metadata scan on x86/ARM.
inline constexpr uint8_t kCtrlEmpty   = 0b10000000;
inline constexpr uint8_t kCtrlDeleted = 0b11111110;
inline constexpr uint8_t kCtrlLiveMask = 0b10000000; // bit set = not live

inline uint8_t ctrl_for_hash(uint64_t h) noexcept {
    uint8_t b = static_cast<uint8_t>(h & 0x7F);
    return b == 0 ? 1u : b; // 0 collides with the EMPTY encoding.
}
} // namespace detail

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEq = std::equal_to<K>>
    requires std::is_trivially_destructible_v<K> && std::is_trivially_destructible_v<V>
class SwissTable {
public:
    SwissTable() = default;
    explicit SwissTable(size_t reserve_n) { reserve(reserve_n); }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool   empty() const noexcept { return size_ == 0; }

    // We keep the load factor at 7/8 — the SwissTable sweet spot.
    void reserve(size_t n) {
        size_t new_cap = std::max<size_t>(16, std::bit_ceil((n * 8 + 6) / 7));
        if (new_cap > capacity_) rehash(new_cap);
    }

    // ---- Insert / lookup / erase ----
    template <typename KK, typename VV>
    std::pair<size_t, bool> emplace(KK&& k, VV&& v) {
        if (((size_ + 1) * 8) >= (capacity_ * 7)) [[unlikely]] {
            rehash(capacity_ == 0 ? 16 : capacity_ * 2);
        }
        auto [pos, is_new] = find_or_insert(k);
        if (is_new) [[unlikely]] {
            new (&slots_[pos].key) K(std::forward<KK>(k));
            new (&slots_[pos].val) V(std::forward<VV>(v));
            ++size_;
        } else {
            slots_[pos].val = std::forward<VV>(v);
        }
        return {pos, is_new};
    }
    bool insert(const K& k, V v) { return emplace(k, std::move(v)).second; }

    static constexpr size_t kNotFound = static_cast<size_t>(-1);

    [[nodiscard]] size_t find(const K& k) const noexcept {
        if (capacity_ == 0) return kNotFound;
        uint64_t h    = Hash{}(k);
        uint8_t  ctrl = detail::ctrl_for_hash(h);
        size_t   mask = capacity_ - 1;
        size_t   pos  = static_cast<size_t>(h) & mask;
        for (size_t probes = 0; probes < capacity_; ++probes) {
            uint8_t c = ctrl_[pos];
            if (c == detail::kCtrlEmpty) return kNotFound;
            if (c == ctrl && KeyEq{}(slots_[pos].key, k)) return pos;
            pos = (pos + 1) & mask;
        }
        return kNotFound;
    }

    [[nodiscard]] V* get(const K& k) noexcept {
        size_t i = find(k);
        return i == kNotFound ? nullptr : &slots_[i].val;
    }
    [[nodiscard]] const V* get(const K& k) const noexcept {
        size_t i = find(k);
        return i == kNotFound ? nullptr : &slots_[i].val;
    }
    [[nodiscard]] bool contains(const K& k) const noexcept { return find(k) != kNotFound; }

    void erase(const K& k) noexcept {
        size_t i = find(k);
        if (i == kNotFound) return;
        ctrl_[i] = detail::kCtrlDeleted;
        --size_;
    }

    void clear() noexcept {
        if (capacity_) {
            std::memset(ctrl_.data(), detail::kCtrlEmpty, capacity_);
            size_ = 0;
        }
    }

    // Iterate over all live (key, value) entries. Order is unspecified.
    // Used by passes / the lowerer that need to walk the full map.
    template <typename F>
    void for_each(F&& f) const
        noexcept(noexcept(f(std::declval<const K&>(), std::declval<const V&>()))) {
        for (size_t i = 0; i < capacity_; ++i) {
            uint8_t c = ctrl_[i];
            if (c & detail::kCtrlLiveMask) continue; // empty or deleted
            f(slots_[i].key, slots_[i].val);
        }
    }

private:
    struct Slot { K key{}; V val{}; };

    void rehash(size_t new_cap) {
        // Save the old buffers so we can walk them to reinsert.
        std::vector<uint8_t> old_ctrl    = std::move(ctrl_);
        std::vector<Slot>    old_slots   = std::move(slots_);
        size_t              old_capacity = capacity_;

        // Allocate fresh buffers for the new capacity.
        ctrl_.assign(new_cap, detail::kCtrlEmpty);
        slots_.resize(new_cap);
        capacity_ = new_cap;
        size_ = 0;

        // Reinsert every live entry. Skips EMPTY and DELETED control bytes.
        for (size_t i = 0; i < old_capacity; ++i) {
            uint8_t c = old_ctrl[i];
            if (c & detail::kCtrlLiveMask) continue; // empty or deleted
            // Live slot: reinsert key + value.
            auto [pos, is_new] = find_or_insert(old_slots[i].key);
            [[assume(is_new)]];
            new (&slots_[pos].key) K(std::move(old_slots[i].key));
            new (&slots_[pos].val) V(std::move(old_slots[i].val));
            ++size_;
        }
    }

    // Walk the probe sequence to find either:
    //   - an existing slot with key `k` (returns {pos, false}), or
    //   - the slot where `k` should be inserted (returns {pos, true}).
    std::pair<size_t, bool> find_or_insert(const K& k) noexcept {
        uint64_t h    = Hash{}(k);
        uint8_t  ctrl = detail::ctrl_for_hash(h);
        size_t   mask = capacity_ - 1;
        size_t   pos  = static_cast<size_t>(h) & mask;
        size_t   first_deleted = static_cast<size_t>(-1);
        for (size_t probes = 0; probes < capacity_; ++probes) {
            uint8_t c = ctrl_[pos];
            if (c == detail::kCtrlEmpty) {
                // Empty -> key is not in the table. Insert here, or in an
                // earlier-deleted slot if we saw one (reduces probe length).
                size_t insert_pos = (first_deleted != static_cast<size_t>(-1))
                                    ? first_deleted : pos;
                ctrl_[insert_pos] = ctrl;
                return {insert_pos, true};
            }
            if (c == detail::kCtrlDeleted) {
                if (first_deleted == static_cast<size_t>(-1))
                    first_deleted = pos;
            } else if (c == ctrl && KeyEq{}(slots_[pos].key, k)) {
                return {pos, false};
            }
            pos = (pos + 1) & mask;
        }
        __builtin_unreachable();
    }

    std::vector<uint8_t> ctrl_{};
    std::vector<Slot>    slots_{};
    size_t capacity_{0};
    size_t size_{0};
};

} // namespace aegis
