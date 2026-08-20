// passes/mid/TCO.hpp — Tail Call Optimization.
// ============================================================
// Law (Section §II):
//   "Tail Call Optimization (TCO): Rewires SoN edges for tail calls
//    to prevent stack overflow."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
class TailCallOptPass : public Pass {
public:
    TailCallOptPass() : Pass("tco") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::mid
