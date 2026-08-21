// passes/research/PGDLO.hpp — Profile-Guided Data Layout Optimization.
// ============================================================
// Law (Section §II Hardware-Aware Memory & Layout Optimization):
//   "PGDLO: Reorders struct fields for spatial locality and pads to
//    prevent false sharing."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class PGDLOPass : public Pass {
public:
    PGDLOPass() : Pass("pgdlo") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
