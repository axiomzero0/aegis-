// passes/SCCP.h — Sparse Conditional Constant Propagation.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Sparse Conditional Constant Propagation (SCCP): Propagates
//    constants and eliminates unreachable code."
//
// Classic SCCP lattice:
//   Top         (unknown — not yet visited)
//   Constant(x) (a specific compile-time value)
//   Bottom      (overdefined — variable, not a constant)
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis {
class SCCPPass : public Pass {
public:
    SCCPPass() : Pass("sccp") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis
