// passes/mid/EscapeAnalysis.hpp — Escape Analysis + Stack Promotion.
// ============================================================
// Laws:
//   "Escape Analysis: Determines if heap-allocated objects escape
//    their function scope."
//   "Stack Promotion (Allocation Elimination): Demotes heap
//    allocations to stack allocations or register-held structs."
//   "Partial Escape Analysis (PEA): Tracks objects across basic
//    blocks. Allows register allocation for partially escaping
//    objects."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class EscapeAnalysisPass : public Pass {
public:
    EscapeAnalysisPass() : Pass("escape_analysis") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
