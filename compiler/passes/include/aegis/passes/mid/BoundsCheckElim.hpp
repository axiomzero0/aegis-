// passes/mid/BoundsCheckElim.hpp — Bounds Check Elimination (BCE).
// ============================================================
// Law (Section §I Frontend & Memory Safety):
//   "Bounds Check Elimination (BCE): Uses range analysis to prove
//    array indices are within bounds. Removes runtime checks."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class BoundsCheckElimPass : public Pass {
public:
    BoundsCheckElimPass() : Pass("bce") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
