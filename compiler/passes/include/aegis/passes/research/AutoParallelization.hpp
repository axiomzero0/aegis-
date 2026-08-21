// passes/research/AutoParallelization.hpp — Auto-Parallelization via Effect Independence.
// ============================================================
// Law (Section §II Advanced Vectorization & Parallelization):
//   "Auto-Parallelization via Effect Independence: Detects loops with
//    no cross-iteration dependencies. Inserts fork/join or SIMD."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class AutoParallelizationPass : public Pass {
public:
    AutoParallelizationPass() : Pass("auto_parallelization") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
