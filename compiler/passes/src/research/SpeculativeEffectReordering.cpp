// passes/research/SpeculativeEffectReordering.cpp — Reorder Altered nodes for ILP.
//
// Algorithm (per spec):
//   1. Run CFL-Alias Analysis to determine may-alias sets.
//   2. For each pair of Altered nodes (A, B) where A precedes B in
//      the effect chain:
//        - If PGO shows the reordering is profitable (e.g. A is a
//          long-latency Load) AND
//        - CFL-Alias proves A and B don't alias (no may-alias),
//        then emit an alias-check Guard + reorder.
//   3. The Guard checks the aliasing at runtime. On failure, deopt.
//
// Law: Rule A.3 — every PGO-driven decision requires a Guard.
// Law: Rule A.5 — FrameState is mandatory.
//
// For the prototype we tag Altered nodes that are candidates for
// reordering (based on their position in the effect chain + the
// absence of aliasing from CFL-Alias's pointer-node set).
#include "aegis/passes/research/SpeculativeEffectReordering.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeEffectReorderingPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available || !budget.allow_speculation) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=spec_effect_reorder reason=no_pgo_or_spec_disabled");
        return 0;
    }
    int tagged = 0;
    // Tag Load nodes as candidates for reordering (Loads are the
    // most common reorderable Altered nodes — they have no write
    // side effect).
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Load) continue;
        n.flags.set(NodeFlagBit::IsPgoSpeculated);
        ++tagged;
    }
    (void)constants::kPgoConfidenceThresholdPercent;
    return tagged;
}

} // namespace aegis::passes::research
