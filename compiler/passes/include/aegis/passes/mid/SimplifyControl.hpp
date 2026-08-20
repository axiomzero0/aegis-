// passes/mid/SimplifyControl.hpp — Block merging + jump threading + Branch relaxation.
// ============================================================
// Laws (Section §II Mid-Level IR):
//   "Jump Threading: Threads conditional jumps through immediate
//    successors."
//   "Block Merging: Combines basic blocks with single
//    predecessors/successors."
//   "Branch Relaxation: Adjusts branch offsets (short to long)."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class SimplifyControlPass : public Pass {
public:
    SimplifyControlPass() : Pass("simplify_control") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
