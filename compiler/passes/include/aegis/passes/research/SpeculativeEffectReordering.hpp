// passes/research/SpeculativeEffectReordering.hpp — Speculative Effect Reordering.
// ============================================================
// Law (Section §II Speculative Execution & Deoptimization):
//   "Speculative Effect Reordering: Uses CFL-Alias + PGO to reorder
//    Altered nodes for ILP. Inserts runtime alias-checks. Deopts on
//    alias detection."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class SpeculativeEffectReorderingPass : public Pass {
public:
    SpeculativeEffectReorderingPass() : Pass("speculative_effect_reordering") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
