// passes/mid/DSE.hpp — Dead Store Elimination.
// ============================================================
// Law (Section §II):
//   "Dead Store Elimination: Deletes writes to memory overwritten
//    before any read."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class DeadStoreElimPass : public Pass {
public:
    DeadStoreElimPass() : Pass("dse") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
