// passes/mid/InductionVarSimplification.hpp — Induction Variable Simplification.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Induction Variable Simplification: Rewrites induction variables
//    into linear forms (base + i * stride)."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class InductionVarSimplificationPass : public Pass {
public:
    InductionVarSimplificationPass() : Pass("induction_var_simplification") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
