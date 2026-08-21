// passes/mid/RCOptimization.hpp — Reference Counting (RC) Optimization.
// ============================================================
// Law (Section §I Frontend & Memory Safety):
//   "Reference Counting (RC) Optimization: Merges redundant RC
//    increments/decrements. Converts local RC ops to stack moves."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class RCOptimizationPass : public Pass {
public:
    RCOptimizationPass() : Pass("rc_optimization") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
