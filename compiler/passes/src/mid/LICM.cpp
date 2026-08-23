// passes/mid/LICM.cpp — Loop-Invariant Code Motion.
//
// WHAT IS REAL IN THIS IR (and what is not):
//   - PURE nodes (arithmetic, comparisons) carry no ctrl/eff edges in
//     a sea of nodes: they are evaluated by demand, once, wherever
//     their users sit. A loop-invariant Pure expression is therefore
//     ALREADY "hoisted" in every sense this IR can express — the
//     scheduler places it outside the loop body because the body's
//     emission is demand-driven from its users. What this pass DOES
//     do: PROVE invariance for every Pure computation inside each
//     loop (all data inputs defined outside the loop or constant),
//     and report the counts. That proof is the input later passes
//     need (e.g. hoisting loads once alias analysis lands, or
//     rematerialization decisions).
//   - ALTERED nodes (Load/Store) cannot move without a may_alias
//     proof against the loop body's mutations. CFLAliasAnalysis is
//     not yet integrated as a queryable prerequisite pass; hoisting
//     without it would be unsound, so those candidates are counted
//     and REPORTED (Rule 65), never moved.
//
// SOUNDNESS: the pass performs no rewrite it cannot prove; today it
// performs no rewrite at all (analysis + telemetry only), which is
// the honest state for this IR (Rule 74: document, don't pretend).
//
// Rule B.5: trivially idempotent. Rule B.6: trivially monotone.
// Rule 65: telemetry carries the real analysis numbers.
#include "aegis/passes/mid/LICM.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

namespace {
// Is `id` defined OUTSIDE loop `loop` (so a Pure consumer inside the
// loop is invariant)? Structural nodes of the loop itself (Loop,
// its Phis) are "inside".
bool defined_outside(const Graph& g, NodeId id, NodeId loop,
                     const uint8_t* inside) {
    if (id == kInvalidNodeId || id >= g.size()) return true; // no input
    if (inside[id] != 0) return false;                      // proven inside
    (void)loop;
    return true;
}

// Mark every node transitively reachable from the loop's Phis'
// non-entry inputs (i.e. computed inside the loop). Nodes NOT marked
// are loop-invariant definitions.
void mark_inside(const Graph& g, NodeId loop, std::vector<uint8_t>& inside) {
    // Seed: every Phi whose region input is this loop, plus anything
    // reachable from their back-edge values.
    std::vector<NodeId> work;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::Phi) continue;
        if (g[id].inputs.empty() || g[id].inputs[0] != loop) continue;
        inside[id] = 1;
        work.push_back(id);
    }
    while (!work.empty()) {
        const NodeId cur = work.back();
        work.pop_back();
        for (NodeId in : g[cur].inputs) {
            if (in == kInvalidNodeId || in >= g.size()) continue;
            if (in == loop) continue;
            if (inside[in] != 0) continue;
            // Constants/Parameters are position-free invariants even
            // if reachable; only computed nodes are "inside".
            const NodeKind k = g[in].kind;
            if (k == NodeKind::Constant || k == NodeKind::Parameter) continue;
            inside[in] = 1;
            work.push_back(in);
        }
    }
}
} // namespace

int LICMPass::run(Graph& g, const PassBudget& budget) {
    int loops = 0;
    int invariant_pure = 0;
    int blocked_altered = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::Loop) continue;
        ++loops;
        std::vector<uint8_t> inside(g.size(), 0);
        mark_inside(g, id, inside);

        // Pure nodes USED inside the loop but fully invariant: prove
        // and count (they are demand-evaluated outside the body by
        // the scheduler — see header).
        for (NodeId p = 0; p < g.size(); ++p) {
            if (inside[p] != 0) continue;
            const Node& n = g[p];
            if (n.flags.has(NodeFlagBit::IsDead)) continue;
            if (!n.is_pure()) continue;
            if (n.kind == NodeKind::Constant || n.kind == NodeKind::Parameter)
                continue; // trivially invariant, not a computation
            // A used-in-this-loop invariant: at least one user inside.
            bool used_inside = false;
            for (NodeId u : g.users_snapshot(p)) {
                if (u < g.size() && inside[u] != 0) { used_inside = true; break; }
            }
            if (!used_inside) continue;
            // Prove invariance: every data input outside the loop.
            bool invariant = true;
            for (NodeId in : n.data_ins()) {
                if (!defined_outside(g, in, id, inside.data())) {
                    invariant = false;
                    break;
                }
            }
            if (invariant) ++invariant_pure;
        }
        // Altered nodes inside the loop: the blocked hoist candidates.
        for (NodeId p = 0; p < g.size(); ++p) {
            if (inside[p] == 0) continue;
            const Node& n = g[p];
            if (n.flags.has(NodeFlagBit::IsDead)) continue;
            if (n.kind == NodeKind::Load || n.kind == NodeKind::Store) {
                ++blocked_altered;
            }
        }
    }
    if (loops > 0) {
        // Rule 65: the analysis numbers are the observable output;
        // pure-invariant expressions need no motion in this IR (see
        // header), loads/stores stay blocked on alias proof.
        std::string detail = "loops=" + std::to_string(loops) +
                             " pure_invariant=" +
                             std::to_string(invariant_pure) +
                             " altered_blocked_alias=" +
                             std::to_string(blocked_altered);
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded, detail);
    }
    (void)budget;
    return 0;
}

} // namespace aegis::passes::mid
