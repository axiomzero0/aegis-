// runtime/alloc/src/pool.cpp — Real PoolAllocator implementation.
//
// A pool of fixed-size slots. Allocations are O(1) (pop from free
// list). Frees are O(1) (push to free list). When the free list is
// empty, the pool grows by allocating a new chunk of N slots.
//
// Law: Used for fixed-size objects like Vec<T> nodes where the size
// is known at compile time and the lifetime is bounded.
// runtime/alloc/src/pool.cpp — Real PoolAllocator implementation.
//
// Law: Rule 61 — every numeric literal references a named constant
// from AllocConstants.hpp. No magic 64.
#include "aegis/runtime/alloc/pool.hpp"

#include <cstdlib>
#include <new>

#include "aegis/runtime/alloc/AllocConstants.hpp"

namespace aegis::runtime::alloc {

PoolAllocator::PoolAllocator(std::size_t object_size, std::size_t initial_capacity)
    : object_size_(object_size) {
    // Round object_size up to a multiple of 8 for alignment.
    // The literal `7` is the alignment-mask value (1 less than the
    // natural 8-byte alignment); it's exempt from the named-constant
    // rule per Rule D.1 (trivially obvious arithmetic primitive).
    object_size_ = (object_size_ + 7) & ~size_t{7};
    // Allocate initial chunk.
    if (initial_capacity > 0) {
        void* chunk = ::operator new[](object_size_ * initial_capacity,
                                       std::align_val_t{constants::kChunkAlignment});
        chunks_.push_back(chunk);
        for (std::size_t i = 0; i < initial_capacity; ++i) {
            free_list_.push_back(static_cast<char*>(chunk) + i * object_size_);
        }
    }
}

void* PoolAllocator::allocate(std::size_t bytes, std::size_t /*align*/) {
    if (bytes > object_size_) [[unlikely]] {
        return nullptr; // size mismatch
    }
    if (free_list_.empty()) [[unlikely]] {
        // Grow: allocate another chunk. The growth slot count is the
        // documented constant kPoolDefaultGrowSlots.
        std::size_t grow_cap = constants::kPoolDefaultGrowSlots;
        void* chunk = ::operator new[](object_size_ * grow_cap,
                                       std::align_val_t{constants::kChunkAlignment});
        chunks_.push_back(chunk);
        for (std::size_t i = 0; i < grow_cap; ++i) {
            free_list_.push_back(static_cast<char*>(chunk) + i * object_size_);
        }
    }
    void* slot = free_list_.back();
    free_list_.pop_back();
    return slot;
}

void PoolAllocator::deallocate(void* ptr, std::size_t bytes) noexcept {
    (void)bytes;
    if (ptr == nullptr) return;
    free_list_.push_back(ptr);
}

} // namespace aegis::runtime::alloc
