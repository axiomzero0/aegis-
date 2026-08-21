// passes/research/GuardedDevirtualization.hpp — Guarded Devirtualization & Inlining.
// ============================================================
// Law (Section §II Speculative Execution & Deoptimization):
//   "Guarded Devirtualization & Inlining: Uses PGO to identify
//    monomorphic call sites. Emits fast-path inline with type-check
//    guard. Deopts on failure."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class GuardedDevirtualizationPass : public Pass {
public:
    GuardedDevirtualizationPass() : Pass("guarded_devirtualization") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
