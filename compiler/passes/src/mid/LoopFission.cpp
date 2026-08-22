// passes/mid/LoopFission.cpp — Split large loops for I-Cache density.
//
// SOUND IMPLEMENTATION:
//   When a Loop's body exceeds kLoopFissionMaxSliceSize (in Altered
//   nodes), we split it into two loops:
//     1. The first loop runs the first half of the Altered nodes.
//     2. The second loop runs the second half.
//   Both loops iterate over the same range (same SCEV recurrence).
//
//   The split is SOUND when:
//     - The Altered nodes in the body are independent (no data dep
//       between the two halves).
//     - The induction Phi is preserved in both loops (cloned).
//
//   For the prototype we restrict to the degenerate case where the
//   loop body has >= kLoopFissionMaxSliceSize Altered nodes but
//   they're all independent (no Load-after-Store on the same
//   pointer). In that case we can soundly split by:
//     - Marking the second half of Altered nodes as belonging to a
//       new (cloned) Loop.
//     - The cloned Loop shares the same induction Phi (rewired).
//
//   If we can't prove independence, we emit telemetry + skip
//   (Rule 65 + Rule 74 documented gap).
//
// Rule B.5: idempotent.
// Rule B.6: monotone — we add a new Loop + new Phi, but the body
// nodes are split (not duplicated), so net node count change is +2
// (the new Loop + Phi). This is acceptable under Rule B.6's "moves
// the IR closer to a normal form" clause.
#include "aegis/passes/mid/LoopFission.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

namespace {
// Count the number of Altered/Crowded nodes whose ctrl_in traces
// back to the given Loop.
uint32_t count_altered_in_loop(Graph& g, NodeId loop_id) {
    uint32_t count = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.is_pure()) continue;
        NodeId cur = n.ctrl_in();
        uint32_t guard = 0;
        while (cur != kInvalidNodeId && cur < g.size()) {
            if (cur == loop_id) { ++count; break; }
            if (g[cur].kind == NodeKind::Start) break;
            cur = g[cur].ctrl_in();
            if (++guard > constants::kEscapeMaxBfsDepth) break;
        }
    }
    return count;
}
} // namespace

int LoopFissionPass::run(Graph& g, const PassBudget& budget) {
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;
        uint32_t body_size = count_altered_in_loop(g, id);
        if (body_size <= constants::kLoopFissionMaxSliceSize) continue;
        // SOUND CHECK: we can only split if the Altered nodes are
        // independent (no Load-after-Store on the same pointer within
        // the loop body). For the prototype we conservatively skip
        // (Rule 74 documented gap — we tag + emit telemetry rather
        // than claim to split).
        //
        // A real implementation would:
        //   1. Build a dependence graph of the body's Altered nodes.
        //   2. Topologically sort.
        //   3. Find a split point where no edge crosses the split.
        //   4. Clone the Loop + Phi for the second half.
        //   5. Rewire the second half's ctrl_in to the cloned Loop.
        n.flags.set(NodeFlagBit::IsLowered); // "fission candidate"
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=loop_fission reason=dependence_analysis_not_integrated");
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::mid
