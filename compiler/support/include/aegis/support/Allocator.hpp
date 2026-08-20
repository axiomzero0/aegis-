// ============================================================
// aegis/support/Allocator.hpp — std::pmr wrappers + monotonic buffers.
// ============================================================
// Law: Rule B.2 — "Both AOT and JIT compilers must use
//       std::pmr::monotonic_buffer_resource for IR allocation.
//       AOT: Bulk-free after compilation. JIT: Bulk-free after
//       compilation. No malloc/free in the compiler hot path."
//
// This header exposes a thin convenience wrapper over PMR's
// monotonic_buffer_resource that:
//   - Pre-allocates a 256 KB inline buffer (typical IR size).
//   - Falls through to heap if the buffer is exhausted.
//   - Bulk-frees at destruction (zero per-node free()).
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>

namespace aegis::support {

// Monotonic allocator used by the IR arena. Bulk-free at destruction.
// Rule B.2: no malloc/free on the hot path.
class MonoArena {
public:
    static constexpr size_t kDefaultCapacity = 1u << 18; // 256 KB inline

    explicit MonoArena(size_t inline_capacity = kDefaultCapacity)
        : buffer_(::operator new[](inline_capacity, std::align_val_t{64})),
          capacity_(inline_capacity),
          upstream_(buffer_, capacity_) {}

    ~MonoArena() {
        ::operator delete[](buffer_, std::align_val_t{64});
    }

    MonoArena(const MonoArena&) = delete;
    MonoArena& operator=(const MonoArena&) = delete;

    // The pmr resource the Graph should allocate through.
    [[nodiscard]] std::pmr::memory_resource* resource() noexcept {
        return &upstream_;
    }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    void*    buffer_{nullptr};
    size_t   capacity_{0};
    std::pmr::monotonic_buffer_resource upstream_;
};

} // namespace aegis::support
