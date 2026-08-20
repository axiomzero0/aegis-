// passes/SimplifyControl.h — Block merging + jump threading + DSE + TCO.
// ============================================================
// Combines several small passes from §II Mid-Level IR:
//   - Block Merging: combine basic blocks with single pred/successor.
//   - Jump Threading: thread conditional jumps through immediate
//     successors when the successor is a Region whose only input is
//     the branch.
//   - Dead Store Elimination: a Store whose last write is overwritten
//     before any read on the same path.
//   - Tail Call Optimization: rewire SoN edges for tail calls.
// ============================================================
#pragma once
#include "passes/Pass.h"
namespace aegis {
class SimplifyControlPass : public Pass {
public:
    SimplifyControlPass() : Pass("simplify_control") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis
