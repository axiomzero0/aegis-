// passes/research/SLPVectorization.hpp — Superword-Level Parallelism analysis.
// ============================================================
// HONEST SCOPE: This is an ANALYSIS pass, not a transform pass. It
// identifies groups of independent Pure nodes that COULD be packed
// into a SIMD register. The actual packing requires a new
// "VectorOp" NodeKind that doesn't exist yet — adding it is
// deferred per Rule 74 (No Deletion-by-Avoidance: we document the
// gap rather than claim to do something we can't).
//
// Law (Section §II Advanced Vectorization & Parallelization):
//   "SLP: Packs independent Pure nodes into SIMD registers
//    (AVX-512/SVE)."
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
