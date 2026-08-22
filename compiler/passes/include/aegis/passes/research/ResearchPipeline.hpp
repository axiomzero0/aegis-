// passes/research/ResearchPipeline.hpp — Build the research pass pipeline.
//
// Law (Rule A.1 — One Pipeline, Two Inputs): the research passes are
// part of the SAME unified pipeline as the standard mid-level passes.
// There is no "JIT-only" pass list: every research pass handles both
// AOT and JIT modes via the shared PassBudget interface (Rule A.2 —
// PGO changes how aggressively a pass fires, not whether it exists).
//
// Order rationale (documented per Rule 70/71):
//   1. CFLAliasAnalysis    — alias facts feed BCE / reordering proofs.
//   2. ValueFlowAnalysis   — value-flow facts feed pool synthesis.
//   3. PGDLO               — struct layout tagging (JIT: PGO-driven).
//   4. MemPoolSynthesis    — pool-allocator synthesis for loop allocs.
//   5. CacheObliviousLayout — container layout tagging.
//   6. SLPVectorization    — SLP-packable group tagging (Rule 49).
//   7. AutoParallelization — loop parallelism tagging (trip-count gate).
//   8. GuardedDevirtualization — monomorphic call speculation (JIT).
//   9. SpeculativeBCE      — bounds-check speculation (JIT).
//  10. SpeculativeEffectReordering — load speculation (JIT).
//  11. SpeculativeLockElision      — lock elision speculation (JIT).
//  12. BOLTLayout          — post-link layout (graph-level: no-op).
//
// Analyses run before transforms so the transforms can consume their
// results; speculation runs last so it sees the final optimized graph
// (guarding post-optimization nodes keeps FrameStates minimal).
#pragma once
#include <memory>
#include <vector>
#include "aegis/passes/Pass.hpp"

namespace aegis::passes::research {

// Build the research pass pipeline (Rules A.1, B.5, B.6). Returns the
// passes in the order documented above. All passes are budget-aware:
// in AOT mode the speculative members no-op (they require PGO proof),
// in JIT mode they speculate and install guards + FrameStates
// (Rules A.3, A.5).
std::vector<std::unique_ptr<Pass>> build_research_pipeline();

} // namespace aegis::passes::research
