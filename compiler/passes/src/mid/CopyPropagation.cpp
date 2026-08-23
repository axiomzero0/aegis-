// passes/mid/CopyPropagation.cpp — Replace temporaries with source values.
//
// In the Aegis IR, "copies" are Cast nodes that are identity casts
// (same source + destination type), Select nodes where the two
// alternatives are the same, and Phi nodes where all the input values
// are identical.
//
// Algorithm:
//   1. Find every Cast node. If its source node has the same TypeId
//      as the Cast's TypeId, the cast is identity — rewrite all uses
//      of the Cast to point at the source, mark the Cast dead.
//   2. Find every Select node where the two data inputs are identical
//      NodeIds — replace with one of the inputs.
//   3. Find every Phi node where all the input values are identical
//      — replace with that value.
//
// Idempotency: once we eliminate a copy, downstream copies become
// visible and may also be eliminated in the same pass — but the
// rewrite-all-uses step ensures that by the time we re-run, all
// copies are already gone.
#include "aegis/passes/mid/CopyPropagation.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"

namespace aegis::passes::mid {

int CopyPropagationPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        NodeId replacement = kInvalidNodeId;
        if (n.kind == NodeKind::Cast) {
            // Identity cast: source type == destination type.
            auto d = n.data_ins();
            if (d.size() == 1) {
                NodeId src = d[0];
                if (src != kInvalidNodeId && src < g.size() &&
                    g[src].type_id == n.type_id) {
                    replacement = src;
                }
            }
        } else if (n.kind == NodeKind::Select) {
            // Identical branches: c ? a : a -> a.
            // Law: Rule 61 — kSelectInputs is the named constant, not
            // a magic 3.
            auto d = n.data_ins();
            if (d.size() == ir::shape::kSelectInputs && d[1] == d[2]) {
                replacement = d[1];
            }
        } else if (n.kind == NodeKind::Phi) {
            // All inputs identical: phi(a, a, ..., a) -> a.
            auto d = n.data_ins();
            // Skip the region (data_ins skips ctrl/eff for Pure nodes,
            // but Phi's first input is the region — we need to look
            // past that).
            // For Pure Phi nodes, inputs = {region, vals...} per the
            // make_phi convention. data_ins() returns all inputs for
            // Pure nodes (which is a quirk of the prototype — see
            // Node::data_ins()). Filter out Region nodes.
            NodeId first_val = kInvalidNodeId;
            bool all_same = true;
            for (size_t idx = 0; idx < d.size(); ++idx) {
                NodeId in = d[idx];
                if (in == kInvalidNodeId) continue;
                if (in >= g.size()) continue;
                // The REGION slot is inputs[0] by the make_phi
                // convention — a Region, a Loop, or a ctrl Proj (the
                // loop-exit effect merge lowers as
                // Phi(exit_proj, {eff_a, eff_b})). It is skipped BY
                // POSITION, never by node kind: the phi VALUES may
                // legitimately be Projs themselves (the effect chain
                // is built from Start's eff Proj), and skipping those
                // by kind would silently disable the collapse.
                if (idx == 0) continue;
                if (g[in].kind == NodeKind::Region ||
                    g[in].kind == NodeKind::Loop) continue;
                if (first_val == kInvalidNodeId) {
                    first_val = in;
                } else if (first_val != in) {
                    all_same = false;
                    break;
                }
            }
            if (all_same && first_val != kInvalidNodeId) {
                replacement = first_val;
            }
        }
        if (replacement != kInvalidNodeId) {
            // Snapshot: swap_input mutates this output list
            // mid-iteration (Rule 62/73 — see Graph::users_snapshot).
            for (NodeId user : g.users_snapshot(id)) {
                g.swap_input(user, id, replacement);
            }
            n.flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
