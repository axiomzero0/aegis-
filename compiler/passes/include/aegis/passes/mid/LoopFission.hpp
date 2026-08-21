// passes/mid/LoopFission.hpp — Loop Fission (Distribution).
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Loop Fission (Distribution): Splits large loops for I-Cache
//    density or disjoint data access."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class LoopFissionPass : public Pass {
public:
    LoopFissionPass() : Pass("loop_fission") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
