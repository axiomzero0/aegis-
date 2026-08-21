// backend/RegAlloc/GraphColoring.hpp — Chaitin-Briggs graph coloring allocator.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Graph Coloring Register Allocation: Chaitin-Briggs coloring for
//    physical registers."
// ============================================================
#pragma once
#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/Target.hpp"
namespace aegis::backend::regalloc {

class GraphColoringAllocator {
public:
    GraphColoringAllocator(MachineFunction& mf, const Target& target)
        : mf_(mf), target_(target) {}

    // Returns the number of spills. Falls back to Linear Scan if the
    // function has more than kGraphColoringMaxVregs vregs (Rule 47
    // cost model — graph coloring is O(n^2)).
    int run() noexcept;

private:
    MachineFunction& mf_;
    const Target&     target_;
};

} // namespace aegis::backend::regalloc
