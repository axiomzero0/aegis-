// passes/mid/LoopFission.cpp — Split large loops for I-Cache density.
//
// Algorithm:
//   1. For each Loop node, count the number of Altered nodes (Store,
//      Load, CallAltered) in its body.
//   2. If the count exceeds kLoopFissionMaxSliceSize, the loop is a
//      candidate for fission.
//   3. Tag the loop for fission. (A real impl would split the body
//      into N smaller loops, each touching a disjoint subset of
//      memory, to improve I-cache density. For the prototype we tag
//      + count for telemetry.)
//
// Rule B.6: Fission GROWS the IR (creates new Loop nodes). Bounded by
// kLoopFissionMaxSliceSize — we only split when the body is genuinely
// too large, and we cap the per-loop split count.
#include "aegis/passes/mid/LoopFission.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

namespace {
// Count the number of Altered/Crowded nodes whose ctrl_in traces back
// to the given Loop. This is a conservative body-size estimate.
uint32_t count_altered_in_loop(Graph& g, NodeId loop_id) {
    uint32_t count = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.is_pure()) continue;
        // Walk ctrl_in chain to see if it reaches the loop.
        NodeId cur = n.ctrl_in();
        while (cur != kInvalidNodeId && cur < g.size()) {
            if (cur == loop_id) { ++count; break; }
            const Node& c = g[cur];
            if (c.kind == NodeKind::Start) break;
            cur = c.ctrl_in();
            // Anti-cycle guard: bail after a fixed number of steps.
            // Law: Rule 61 — kEscapeMaxBfsDepth is the documented
            // walk bound. Reusing it for the ctrl-chain walk is fine.
            static uint32_t s_guard = 0;
            if (++s_guard > constants::kEscapeMaxBfsDepth) break;
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
        // Candidate for fission. Tag + emit telemetry.
        n.flags.set(NodeFlagBit::IsLowered);
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=loop_fission reason=body_too_large");
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::mid
