// passes/research/CFLAliasAnalysis.hpp — CFL-Reachability Alias Analysis.
// ============================================================
// Law (Section §II Advanced Alias & Pointer Analysis):
//   "CFL-Reachability Alias Analysis: Context-Free Language
//    reachability for precise interprocedural alias analysis.
//    Critical for reordering Altered nodes."
// ============================================================
#pragma once
#include "aegis/ir/Graph.hpp"
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class CFLAliasAnalysisPass : public Pass {
public:
    CFLAliasAnalysisPass() : Pass("cfl_alias_analysis") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
