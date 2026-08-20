// passes/mid/CopyPropagation.hpp — Copy Propagation.
// ============================================================
// Law (Section §II):
//   "Copy Propagation: Replaces temporaries with source values."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class CopyPropagationPass : public Pass {
public:
    CopyPropagationPass() : Pass("copy_propagation") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
