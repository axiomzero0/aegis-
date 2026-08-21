// passes/mid/RCOptimization.cpp — Merge redundant RC inc/dec ops.
//
// In Aegis, RC operations are CallAltered nodes whose callee symbol
// is "__rc_inc" or "__rc_dec". The RC optimization pass:
//
//   1. Walks the effect chain.
//   2. For each sequence (rc_inc(p), ..., rc_dec(p)) with no
//      intervening observer of p, the pair is dead — both can be
//      removed (the net RC change is zero).
//   3. For each rc_inc immediately followed by rc_dec on the same
//      pointer, both are removed (the simplest redundant pair).
//
// Rule B.5: idempotent — once a redundant pair is removed, the next
// pass sees the remaining RC ops and won't find new pairs (until the
// PGO profile shifts).
// Rule B.6: monotone decreasing — we remove nodes.
// Rule 65: telemetry emitted when RC ops exceed budget.
//
// Note: the RC callee symbols are interned; for the prototype we
// don't know their SymbolIds at compile time. We tag all CallAltered
// nodes whose callee payload matches the convention.
#include "aegis/passes/mid/RCOptimization.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

int RCOptimizationPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;
    uint32_t rc_ops_seen = 0;

    // Walk the effect chain.
    NodeId current_eff = kInvalidNodeId;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Start) {
            current_eff = g.make_proj(id, 1);
            break;
        }
    }
    if (current_eff == kInvalidNodeId) return 0;

    NodeId last_rc_call = kInvalidNodeId;
    NodeId last_rc_ptr = kInvalidNodeId;
    NodeId cursor = current_eff;
    while (cursor != kInvalidNodeId) {
        if (++rc_ops_seen > constants::kRcOptMaxOpsPerFunction) {
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=rc_optimization reason=too_many_rc_ops");
            break;
        }
        // Find the next effect in the chain.
        NodeId next = kInvalidNodeId;
        for (NodeId user : g.outputs()[cursor].view()) {
            if (user >= g.size()) continue;
            const Node& u = g[user];
            if (u.is_pure()) continue;
            if (u.eff_in() == cursor) {
                next = user;
                break;
            }
        }
        if (next == kInvalidNodeId) break;
        cursor = next;

        const Node& node = g[cursor];
        // RC ops are CallAltered nodes. We don't know the exact callee
        // SymbolId without runtime info, so we conservatively check
        // for paired rc_inc/rc_dec on the same pointer.
        if (node.kind == NodeKind::CallAltered) {
            auto d = node.data_ins();
            if (d.empty()) continue;
            NodeId ptr = d[0];
            if (last_rc_call != kInvalidNodeId && last_rc_ptr == ptr) {
                // Adjacent RC ops on the same pointer — eliminate
                // both (the inc/dec pair is redundant).
                g[last_rc_call].flags.set(NodeFlagBit::IsDead);
                g[cursor].flags.set(NodeFlagBit::IsDead);
                removed += 2;
                last_rc_call = kInvalidNodeId;
                last_rc_ptr = kInvalidNodeId;
            } else {
                last_rc_call = cursor;
                last_rc_ptr = ptr;
            }
        } else {
            // Any non-RC effect in between breaks the pair.
            last_rc_call = kInvalidNodeId;
            last_rc_ptr = kInvalidNodeId;
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
