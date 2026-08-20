// passes/EDCE.h — Effect-Aware Dead Code Elimination.
// ============================================================
// Law (Section §II Mid-Level IR):
//   "Effect-Aware Dead Code Elimination (E-DCE): Mark-and-sweep from
//    Crowded nodes. Deletes unreachable Pure/Altered nodes."
//
// Algorithm:
//   1. Find the roots: Crowded nodes, Return nodes, Guard nodes.
//   2. Walk backwards through the inputs of the roots, marking each
//      reachable node as Live.
//   3. Sweep: any node not marked Live is dead.
//
// Pure nodes are only kept if they're reachable from a root. Altered
// nodes are kept only if their effect chain reaches a Return or a
// Crowded root.
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis {
class EDCEPass : public Pass {
public:
    EDCEPass() : Pass("edce") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis
