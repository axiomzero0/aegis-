// ============================================================
// aegis/backend/RegAlloc/RegAllocInterface.hpp — Allocator interface.
// ============================================================
// Law: Section §"Backend & Low-Level":
//   "Graph Coloring Register Allocation: Chaitin-Briggs coloring for
//    physical registers.
//    Linear Scan Register Allocation: Faster alternative for very
//    large functions.
//    Spill Code Generation: Generates save/restore code for spilled
//    variables."
//
// This interface lets us plug in different allocators (LinearScan today,
// Cranelift's regalloc2 via FFI in Phase 2 per the spec) without
// touching the rest of the backend.
// ============================================================
#pragma once

#include <cstdint>
#include <string_view>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/Target.hpp"

namespace aegis::backend::regalloc {

// Result of a register allocation run.
struct AllocationResult {
    uint32_t spills{0};        // number of vregs spilled to stack
    uint32_t coalesces{0};     // number of vreg->vreg coalesces performed
    uint32_t max_live_set{0};  // peak simultaneous live vregs (useful metric)
};

// Abstract allocator interface. Implementations include:
//   - LinearScanAllocator (Phase 1 — already implemented in LinearScan.hpp)
//   - (Future) RegAlloc2 via Cranelift FFI (Phase 2 per spec)
class RegAllocator {
public:
    virtual ~RegAllocator() = default;

    // Allocate registers for all vregs in `fn` according to `target`.
    // Returns a result describing the allocation. After this call,
    // each MachineInstr's `defs`/`uses` arrays map 1:1 to physical
    // register ids (or to spill slots when the vreg was spilled).
    [[nodiscard]] virtual AllocationResult allocate(MachineFunction& fn,
                                                   const Target& target) = 0;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace aegis::backend::regalloc
