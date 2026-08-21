// passes/mid/LoopFusion.hpp — Loop Fusion.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Loop Fusion: Merges adjacent loops over same range for cache
//    locality."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class LoopFusionPass : public Pass {
public:
    LoopFusionPass() : Pass("loop_fusion") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
