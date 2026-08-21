// passes/research/ValueFlowAnalysis.hpp — Value-Flow Analysis (VFA).
// ============================================================
// Law (Section §II Advanced Alias & Pointer Analysis):
//   "Value-Flow Analysis (VFA) for Systems: Tracks value flows to
//    prove disjoint memory regions based on allocation sites."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class ValueFlowAnalysisPass : public Pass {
public:
    ValueFlowAnalysisPass() : Pass("value_flow_analysis") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
