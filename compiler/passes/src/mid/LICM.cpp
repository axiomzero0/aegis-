// passes/mid/LICM.cpp — Loop Invariant Code Motion.
//
// SOUNDNESS ANALYSIS:
//   LICM wants to hoist Pure + non-aliasing Altered nodes out of
//   loops. In our IR:
//     - Pure nodes have no ctrl_in / eff_in (their inputs are data
//       only). They have NO position in the effect chain, so they're
//       already "hoistable" in the sense that they can be evaluated
//       at any time. The IR doesn't track *where* a Pure node sits
//       in the CFG — only the effect chain ordering matters.
//     - Altered nodes (Load, Store) CANNOT be safely hoisted without
//       alias analysis proving they don't depend on the loop body's
//       mutations. We don't have CFLAliasAnalysis integrated into
//       LICM yet (it would need to be a prerequisite pass that
//       exposes a may_alias query).
//
//   Therefore: this pass currently does NOTHING. Tagging Pure nodes
//   as IsHoisted is meaningless (they have no ctrl_in to rewire).
//   Tagging Altered nodes as IsHoisted would be UNSOUND without
//   alias proof.
//
// HONEST FIX:
//   Rather than tag nodes meaninglessly, this pass is a no-op until
//   alias analysis is integrated. We emit a telemetry event noting
//   that LICM is in "analysis-only" mode, so the gap is observable
//   (Rule 65). When CFLAliasAnalysis is integrated, this pass will
//   query may_alias() before hoisting.
//
// Rule B.5: trivially idempotent (no-op).
// Rule B.6: trivially monotone (no-op).
// Rule 65: telemetry on every run so the gap is visible.
// Rule 73: robust — doesn't claim to do something it can't.
#include "aegis/passes/mid/LICM.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

int LICMPass::run(Graph& g, const PassBudget& budget) {
    // Count loops in the graph so we can emit useful telemetry on
    // the gap (how many loops COULD have been optimized).
    int loop_count = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind == NodeKind::Loop) ++loop_count;
    }
    if (loop_count > 0) {
        // Emit telemetry so the LICM gap is visible — passes that
        // could optimize but don't yet (because alias analysis isn't
        // integrated) emit a budget-exceeded event per Rule 65.
        char detail[passes::constants::kLicmTelemetryDetailBytes];
        int n = std::snprintf(detail, sizeof(detail),
                              "loops=%d reason=alias_analysis_not_integrated",
                              loop_count);
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            std::string_view{detail, static_cast<size_t>(n > 0 ? n : 0)});
    }
    (void)budget;
    return 0; // no-op until alias analysis is integrated
}

} // namespace aegis::passes::mid
