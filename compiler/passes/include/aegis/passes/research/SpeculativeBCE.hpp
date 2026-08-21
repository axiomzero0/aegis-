// passes/research/SpeculativeBCE.hpp — Speculative Bounds Check Elimination.
// ============================================================
// Law (Section §II Speculative Execution & Deoptimization):
//   "Speculative BCE: Uses PGO to prove bounds are usually safe.
//    Removes checks but inserts a guard. Deopts on out-of-bounds."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class SpeculativeBCEPass : public Pass {
public:
    SpeculativeBCEPass() : Pass("speculative_bce") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
