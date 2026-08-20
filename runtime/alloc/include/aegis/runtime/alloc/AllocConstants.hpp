// ============================================================
// aegis/runtime/alloc/AllocConstants.hpp — Named allocator constants.
// ============================================================
// Law: Rule 61 (No Hard-Coded Constants) + Rule D.1.
//
// Every magic number in runtime/alloc lives here as a named,
// documented constexpr.
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace aegis::runtime::alloc::constants {

/// Default initial capacity (in objects) for a new PoolAllocator.
/// Tuned to amortize chunk allocation overhead across ~64 object
/// allocations while keeping per-pool memory usage modest.
constexpr uint32_t kPoolDefaultInitialCapacity{64};

/// Default growth factor (in slots) when a PoolAllocator's free list
/// runs empty. Same value as the initial capacity — keeps the chunk
/// size predictable.
constexpr uint32_t kPoolDefaultGrowSlots{64};

/// Default inline capacity (bytes) for a new BumpAllocator. 256 KiB
/// matches the typical IR arena size — bump allocators are typically
/// short-lived per-function-scoped allocations.
constexpr size_t kBumpDefaultCapacityBytes{1u << 18};

/// Alignment for chunk allocations. 64 bytes matches the common x86-64
/// + ARM64 cache line size (Rule 66 — queried at runtime by the JIT,
/// this is the AOT-build default).
constexpr size_t kChunkAlignment{64};

/// Maximum single allocation size before a BumpAllocator returns
/// nullptr. 1 GiB — should never be reached by well-formed programs.
constexpr size_t kBumpMaxAllocBytes{1ull << 30};

} // namespace aegis::runtime::alloc::constants
