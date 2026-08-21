// passes/research/SLPVectorization.hpp — Superword-Level Parallelism.
// ============================================================
// Law (Section §II Advanced Vectorization & Parallelization):
//   "SLP: Packs independent Pure nodes into SIMD registers (AVX-512/SVE)."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class SLPVectorizationPass : public Pass {
public:
    SLPVectorizationPass() : Pass("slp_vectorization") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
