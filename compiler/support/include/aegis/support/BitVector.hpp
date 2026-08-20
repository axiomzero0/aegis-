// ============================================================
// core/BitVector.h — Bit-packed set for large sparse NodeId sets.
// ============================================================
// Law: Rule 56 — "Use BitVectors for large, sparse sets."
//
// `std::vector<bool>` is forbidden (proxy-reference slowness, no
// contiguous access). This BitVector uses raw uint64_t words and gives
// real references, O(1) random access, and O(N/64) bulk operations
// (intersection, union, complement, count) — well-suited to dataflow
// fixpoints.
// ============================================================
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace aegis {

class BitVector {
public:
    BitVector() = default;
    explicit BitVector(uint32_t size) : size_(size), words_((size + 63) / 64, 0) {}

    void resize(uint32_t size) {
        size_ = size;
        words_.resize((size + 63) / 64, 0);
    }
    void clear_bits() noexcept {
        std::memset(words_.data(), 0, words_.size() * sizeof(uint64_t));
    }
    [[nodiscard]] uint32_t size() const noexcept { return size_; }

    void set(uint32_t i) noexcept {
        [[assume(i < size_)]];
        words_[i >> 6] |= (uint64_t{1} << (i & 63));
    }
    void clear(uint32_t i) noexcept {
        [[assume(i < size_)]];
        words_[i >> 6] &= ~(uint64_t{1} << (i & 63));
    }
    void set_if(uint32_t i, bool v) noexcept { v ? set(i) : clear(i); }
    [[nodiscard]] bool test(uint32_t i) const noexcept {
        [[assume(i < size_)]];
        return (words_[i >> 6] >> (i & 63)) & 1u;
    }
    [[nodiscard]] bool operator[](uint32_t i) const noexcept { return test(i); }

    // Bulk operations (vectorizable).
    void operator|=(const BitVector& o) noexcept {
        uint32_t common = std::min(words_.size(), o.words_.size());
        for (uint32_t i = 0; i < common; ++i) words_[i] |= o.words_[i];
    }
    void operator&=(const BitVector& o) noexcept {
        uint32_t common = std::min(words_.size(), o.words_.size());
        for (uint32_t i = 0; i < common; ++i) words_[i] &= o.words_[i];
    }
    void operator^=(const BitVector& o) noexcept {
        uint32_t common = std::min(words_.size(), o.words_.size());
        for (uint32_t i = 0; i < common; ++i) words_[i] ^= o.words_[i];
    }
    void subtract(const BitVector& o) noexcept {
        uint32_t common = std::min(words_.size(), o.words_.size());
        for (uint32_t i = 0; i < common; ++i) words_[i] &= ~o.words_[i];
    }

    [[nodiscard]] uint32_t popcount() const noexcept {
        uint32_t c = 0;
        for (uint64_t w : words_) c += static_cast<uint32_t>(std::popcount(w));
        return c;
    }
    [[nodiscard]] bool any() const noexcept {
        for (uint64_t w : words_) if (w != 0) return true;
        return false;
    }
    [[nodiscard]] bool none() const noexcept { return !any(); }

    // Iterate over set bits.
    template <typename F>
    void for_each_set(F&& f) const noexcept(noexcept(f(uint32_t{}))) {
        for (uint32_t w = 0; w < words_.size(); ++w) {
            uint64_t bits = words_[w];
            while (bits != 0) {
                uint32_t off = static_cast<uint32_t>(std::countr_zero(bits));
                uint32_t idx = (w << 6) + off;
                if (idx < size_) f(idx);
                bits &= bits - 1;
            }
        }
    }

    [[nodiscard]] std::span<const uint64_t> words() const noexcept {
        return {words_.data(), words_.size()};
    }

private:
    uint32_t size_{0};
    std::vector<uint64_t> words_{};
};

} // namespace aegis
