// passes/mid/LoopUnrolling.hpp — Loop Unrolling.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Loop Unrolling: Duplicates loop bodies to reduce branch
//    overhead and expose ILP."
//
// Rule B.6: This pass GROWS the IR. It runs inside a guarded fixpoint
// with a strict budget (kLoopUnrollMaxNodeGrowth). PassManager checks
// the budget and skips if exceeded.
//
// Algorithm:
//   1. For each Loop node, query SCEVAnalysis for the trip count.
//   2. If trip_count is known and <= kLoopUnrollFullUnrollTripCount,
//      fully unroll (no remainder).
//   3. Otherwise unroll by kLoopUnrollDefaultFactor and emit a
//      remainder loop for the leftover iterations.
//   4. Track IR growth; abort if it would exceed the budget.
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class LoopUnrollingPass : public Pass {
public:
    LoopUnrollingPass() : Pass("loop_unrolling") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
