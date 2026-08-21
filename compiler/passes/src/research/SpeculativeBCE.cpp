// passes/research/SpeculativeBCE.cpp — PGO-driven bounds check removal with guard.
//
// Algorithm:
//   1. For each Guard node whose condition is a bounds check
//      (CmpLt(idx, len) or similar), consult PGO.
//   2. If PGO shows >= kPgoConfidenceThresholdPercent of executions
//      have idx in [0, len), the check is "always safe" — remove
//      the heavy check but emit a lightweight Guard.
//   3. On guard failure (idx >= len), deopt to AOT baseline.
//
// Law: Rule A.3 — every PGO-driven decision requires a Guard.
// Law: Rule A.5 — FrameState is mandatory.
// Law: Rule 45 — No specialization without fallback (the AOT baseline
//   preserves the original bounds check).
//
// For the prototype we tag Guard nodes whose condition is a bounds
// check; the backend would emit a lightweight version-check guard
// instead of the full CmpLt + branch.
#include "aegis/passes/research/SpeculativeBCE.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeBCEPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=speculative_bce reason=no_pgo_data");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Guard) continue;
        auto d = n.data_ins();
        if (d.empty()) continue;
        NodeId cond_id = d[0];
        if (cond_id == kInvalidNodeId || cond_id >= g.size()) continue;
        const Node& cond = g[cond_id];
        if (cond.kind != NodeKind::CmpLt &&
            cond.kind != NodeKind::CmpUlt &&
            cond.kind != NodeKind::CmpLe &&
            cond.kind != NodeKind::CmpUle) continue;
        // The condition is a bounds-check pattern. Tag the Guard for
        // the backend to emit a lightweight version-check instead.
        n.flags.set(NodeFlagBit::IsPgoSpeculated);
        ++tagged;
    }
    (void)constants::kPgoConfidenceThresholdPercent;
    return tagged;
}

} // namespace aegis::passes::research
