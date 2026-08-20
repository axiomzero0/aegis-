// passes/mid/StrengthReduction.hpp — Strength Reduction.
// ============================================================
// Law (Section §II):
//   "Strength Reduction: Replaces expensive ops with cheaper ones
//    (e.g. x * 2 -> x << 1)."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class StrengthReductionPass : public Pass {
public:
    StrengthReductionPass() : Pass("strength_reduction") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
