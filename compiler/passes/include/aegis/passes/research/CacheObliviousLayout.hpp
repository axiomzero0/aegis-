// passes/research/CacheObliviousLayout.hpp — Cache-Oblivious Layout Synthesis.
// ============================================================
// Law (Section §II Hardware-Aware Memory & Layout Optimization):
//   "Cache-Oblivious Layout Synthesis: Rewrites container layouts
//    based on target CPU cache line sizes."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class CacheObliviousLayoutPass : public Pass {
public:
    CacheObliviousLayoutPass() : Pass("cache_oblivious_layout") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
