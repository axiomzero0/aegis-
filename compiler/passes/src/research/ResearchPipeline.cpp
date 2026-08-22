// passes/research/ResearchPipeline.cpp — Construct the research pipeline.
//
// See ResearchPipeline.hpp for the documented pass order and the law
// references (Rule A.1 one-pipeline, A.2 PGO as force multiplier).
#include "aegis/passes/research/ResearchPipeline.hpp"

#include "aegis/passes/research/AutoParallelization.hpp"
#include "aegis/passes/research/BOLTLayout.hpp"
#include "aegis/passes/research/CFLAliasAnalysis.hpp"
#include "aegis/passes/research/CacheObliviousLayout.hpp"
#include "aegis/passes/research/GuardedDevirtualization.hpp"
#include "aegis/passes/research/MemPoolSynthesis.hpp"
#include "aegis/passes/research/PGDLO.hpp"
#include "aegis/passes/research/SLPVectorization.hpp"
#include "aegis/passes/research/SpeculativeBCE.hpp"
#include "aegis/passes/research/SpeculativeEffectReordering.hpp"
#include "aegis/passes/research/SpeculativeLockElision.hpp"
#include "aegis/passes/research/ValueFlowAnalysis.hpp"

namespace aegis::passes::research {

std::vector<std::unique_ptr<Pass>> build_research_pipeline() {
    std::vector<std::unique_ptr<Pass>> v;
    v.push_back(std::make_unique<CFLAliasAnalysisPass>());
    v.push_back(std::make_unique<ValueFlowAnalysisPass>());
    v.push_back(std::make_unique<PGDLOPass>());
    v.push_back(std::make_unique<MemPoolSynthesisPass>());
    v.push_back(std::make_unique<CacheObliviousLayoutPass>());
    v.push_back(std::make_unique<SLPVectorizationPass>());
    v.push_back(std::make_unique<AutoParallelizationPass>());
    v.push_back(std::make_unique<GuardedDevirtualizationPass>());
    v.push_back(std::make_unique<SpeculativeBCEPass>());
    v.push_back(std::make_unique<SpeculativeEffectReorderingPass>());
    v.push_back(std::make_unique<SpeculativeLockElisionPass>());
    v.push_back(std::make_unique<BOLTLayoutPass>());
    return v;
}

} // namespace aegis::passes::research
