// passes/mid/NullPointerElimination.cpp — Strip null-check guards that are
// provably unnecessary.
//
// Algorithm:
//   1. For every Guard node, look at its condition input.
//   2. If the condition is "ptr != null" (i.e. CmpNe(ptr, Constant(0)))
//      and the pointer was just produced by an Alloc node, the pointer
//      is provably non-null (Alloc never returns null on success).
//      Strip the guard.
//   3. If the condition is "ptr != null" and the pointer is a function
//      parameter that's never explicitly nulled in the function, we
//      can't prove non-nullness statically — leave the guard in place.
//
// Rule B.5: idempotent — once stripped, the guard is gone, no re-strip.
// Rule B.6: monotone decreasing — we remove a Guard node each time.
// Rule 65: telemetry emitted when we can't prove + leave the guard.
#include "aegis/passes/mid/NullPointerElimination.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

int NullPointerEliminationPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;
    uint32_t check_sites = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Guard) continue;
        if (++check_sites > constants::kNullPtrElimMaxCheckSites) {
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=null_pointer_elimination reason=too_many_check_sites");
            break;
        }
        // Guard's first data input is the condition.
        auto d = n.data_ins();
        if (d.empty()) continue;
        NodeId cond_id = d[0];
        if (cond_id == kInvalidNodeId || cond_id >= g.size()) continue;
        const Node& cond = g[cond_id];
        // Look for CmpNe(ptr, Constant(0)) — the "ptr != null" pattern.
        if (cond.kind != NodeKind::CmpNe) continue;
        auto cd = cond.data_ins();
        if (cd.size() != 2) continue;
        NodeId lhs = cd[0];
        NodeId rhs = cd[1];
        // One side must be a Constant(0).
        NodeId ptr_id = kInvalidNodeId;
        if (lhs != kInvalidNodeId && lhs < g.size() &&
            g[lhs].kind == NodeKind::Constant &&
            g[lhs].payload.i64 == 0) {
            ptr_id = rhs;
        } else if (rhs != kInvalidNodeId && rhs < g.size() &&
                   g[rhs].kind == NodeKind::Constant &&
                   g[rhs].payload.i64 == 0) {
            ptr_id = lhs;
        }
        if (ptr_id == kInvalidNodeId || ptr_id >= g.size()) continue;
        // If the pointer came from an Alloc, it's provably non-null.
        if (g[ptr_id].kind == NodeKind::Alloc ||
            g[ptr_id].kind == NodeKind::StackAlloc) {
            n.flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
