// ============================================================
// aegis/jit/Deopt.hpp — Deoptimization trampolines & state reconstruction.
// ============================================================
// Law (Section §A "The Unified Pipeline & Speculation Laws"):
//   A.3 — "Every PGO-Driven Decision Requires a Guard."
//   A.4 — "Deoptimization Must Reconstruct AOT State."
//   A.5 — "FrameState is Mandatory for All Guards."
//
// When a JIT guard fails (e.g. speculation on aliasing, monomorphic
// call site, lock-contention guard for SLE), the deoptimization engine
// reconstructs the AOT baseline state at the failed instruction pointer
// using the FrameState attachment on the speculating node, then transfers
// control to the AOT version at the equivalent instruction pointer.
// ============================================================
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "aegis/support/Primitives.hpp"

namespace aegis::jit {

// A snapshot of the mutator thread's state at the moment a guard was
// evaluated. The runtime deoptimizer materializes this into a real
// AOT-compatible stack frame.
struct FrameState {
    uint64_t  ip;          // instruction pointer where the guard lives
    uint64_t  region_id;   // affine-region id (Rule A.4: re-materialize regions)
    std::span<const uint64_t> register_snapshot; // values of all GPRs
    std::span<const uint32_t> aliased_nodes;     // NodeIds whose writes must be rolled back
};

// Reconstruct the AOT baseline state from a FrameState snapshot and
// transfer control to the AOT function at the equivalent IP.
//
// The deoptimizer must:
//   1. Restore all registers from the snapshot.
//   2. Materialize the lexical region stack for the active affine regions.
//   3. Roll back any speculatively reordered Altered-node writes that
//      have not yet been observed by a Crowded node (Rule A.4:
//      "Rolling back any speculatively reordered memory writes").
//   4. Jump into the AOT baseline code at the equivalent IP.
[[noreturn]] void deoptimize(const FrameState& state) noexcept;

// Register a deopt trampoline entry for a given JIT guard node id. The
// runtime uses this to look up the FrameState at the moment of failure.
void register_guard_framestate(NodeId guard_id, FrameState state);

} // namespace aegis::jit
