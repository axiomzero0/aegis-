// passes/mid/LICM.hpp — Loop Invariant Code Motion.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Loop Invariant Code Motion (LICM): Hoists Pure/non-aliasing
//    Altered nodes out of loops."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class LICMPass : public Pass {
public:
    LICMPass() : Pass("licm") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
