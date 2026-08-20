// passes/mid/CSE.hpp — Common Subexpression Elimination (effect-sensitive).
// ============================================================
// Law (Section §II):
//   "Common Subexpression Elimination (CSE): Local, effect-sensitive
//    CSE for nodes with different control deps."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class CSEPass : public Pass {
public:
    CSEPass() : Pass("cse") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
