// runtime/alloc/src/bump.cpp — Real BumpAllocator implementation.
//
// Law (Rule C.2 — Thread-Local Allocation for Mutators):
//   "Mutator threads use thread-local bump pointers (lexical regions)
//    for their own runtime allocations."
//
// The BumpAllocator is a thread-local, monotonically-growing slab. There
// is no per-element free(); reset() bulk-frees everything. Allocations
// are O(1) and the only synchronization is the alignment bump (no
// atomics in the fast path because each thread has its own allocator).
// runtime/alloc/src/bump.cpp — Real BumpAllocator implementation.
//
// Law (Rule C.2 — Thread-Local Allocation for Mutators):
//   "Mutator threads use thread-local bump pointers (lexical regions)
//    for their own runtime allocations."
//
// Law: Rule 61 — every numeric literal references a named constant
// from AllocConstants.hpp (here: kChunkAlignment).
#include "aegis/runtime/alloc/bump.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include "aegis/runtime/alloc/AllocConstants.hpp"

namespace aegis::runtime::alloc {

BumpAllocator::BumpAllocator(std::size_t capacity) : capacity_(capacity), offset_(0) {
    base_ = ::operator new[](capacity,
                             std::align_val_t{constants::kChunkAlignment});
}

BumpAllocator::~BumpAllocator() {
    if (base_) {
        ::operator delete[](base_, std::align_val_t{constants::kChunkAlignment});
        base_ = nullptr;
    }
}

void* BumpAllocator::allocate(std::size_t bytes, std::size_t align) {
    // Bump pointer with alignment.
    // The literals `1` here are mask arithmetic primitives
    // (`(align - 1) & ~(align - 1)` is the standard align-up idiom),
    // exempt from Rule D.1.
    std::size_t current = offset_;
    std::size_t aligned = (current + align - 1) & ~(align - 1);
    std::size_t new_offset = aligned + bytes;
    if (new_offset > capacity_) [[unlikely]] {
        // Out of memory in this bump allocator. Real impl would chain
        // to a new arena; for the prototype, return nullptr (the caller
        // will panic).
        return nullptr;
    }
    offset_ = new_offset;
    return static_cast<char*>(base_) + aligned;
}

void BumpAllocator::reset() noexcept {
    offset_ = 0;
}

} // namespace aegis::runtime::alloc
