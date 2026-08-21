// passes/mid/NullPointerElimination.hpp — Null Pointer Elimination.
// ============================================================
// Law (Section §I Frontend & Memory Safety):
//   "Null Pointer Elimination: Proves pointers cannot be null at
//    dereference sites. Strips null-check guards."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class NullPointerEliminationPass : public Pass {
public:
    NullPointerEliminationPass() : Pass("null_pointer_elimination") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
