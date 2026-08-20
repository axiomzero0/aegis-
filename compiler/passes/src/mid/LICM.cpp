// passes/mid/LICM.cpp — Loop Invariant Code Motion.
//
// Algorithm (single-pass, conservative):
//   1. Identify loops: a Loop node + its body. A Loop node has
//      inputs [entry, back_edge]. The back_edge's defining region
//      contains all the body nodes.
//   2. For each Pure node inside the loop body whose data inputs are
//      all defined *outside* the loop (i.e., the input node is not
//      reachable from the Loop node), hoist it: replace its ctrl_in
//      (if any) with the loop's entry control.
//   3. Altered nodes (Load/Store) are hoisted only if alias analysis
//      proves they don't depend on the loop's mutations. For the
//      prototype we conservatively only hoist Pure nodes.
//
// Idempotency (Rule B.5): running LICM twice produces the same IR
// because once a node is hoisted out of the loop, its data inputs are
// by definition all outside the loop, so the second pass sees it as
// already-hoisted.
#include "aegis/passes/mid/LICM.hpp"

#include "aegis/ir/NodeKind.hpp"

namespace aegis::passes::mid {

namespace {
// Returns the set of NodeIds reachable from the Loop node by walking
// the control + effect chain forward within the loop body (excluding
// the loop's exit projection).
std::vector<uint8_t> compute_loop_body(Graph& g, NodeId loop_id) {
    std::vector<uint8_t> in_loop(g.size(), 0);
    if (loop_id == kInvalidNodeId || loop_id >= g.size()) return in_loop;
    // The Loop's inputs are [entry, back_edge]. We follow the back_edge
    // forward and mark every node reachable until we hit a node that
    // post-dominates the loop exit.
    const Node& lp = g[loop_id];
    if (lp.inputs.size() < 2) return in_loop;
    NodeId back = lp.inputs[1];
    if (back == kInvalidNodeId) return in_loop;
    std::vector<NodeId> worklist;
    worklist.push_back(back);
    while (!worklist.empty()) {
        NodeId cur = worklist.back();
        worklist.pop_back();
        if (cur >= g.size()) continue;
        if (in_loop[cur]) continue;
        in_loop[cur] = 1;
        // Walk output edges: every node that uses `cur` as input and
        // is not the loop's exit is in the loop body.
        for (NodeId user : g.outputs()[cur].view()) {
            if (user >= g.size()) continue;
            if (user == loop_id) continue; // avoid back-edge
            if (!in_loop[user]) worklist.push_back(user);
        }
    }
    return in_loop;
}

// Returns true iff every data input of `n` is defined outside `in_loop`.
bool is_loop_invariant(const Node& n, const std::vector<uint8_t>& in_loop) noexcept {
    for (NodeId in : n.data_ins()) {
        if (in == kInvalidNodeId) continue;
        if (in >= in_loop.size()) continue;
        if (in_loop[in]) return false;
    }
    return true;
}
} // namespace

int LICMPass::run(Graph& g, const PassBudget& budget) {
    int hoisted = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;
        auto in_loop = compute_loop_body(g, id);
        // For each Pure, loop-invariant node in the body, mark it as
        // hoisted. The IsHoisted flag is read by downstream passes
        // (eventual backend lowering) to know this node's control
        // input should be rewired to the loop's preheader.
        for (NodeId body_id = 0; body_id < g.size(); ++body_id) {
            if (!in_loop[body_id]) continue;
            Node& bn = g[body_id];
            if (!bn.is_pure()) continue;
            if (bn.kind == NodeKind::Phi) continue; // phi's are loop-carried
            if (is_loop_invariant(bn, in_loop)) {
                bn.flags.set(NodeFlagBit::IsHoisted);
                ++hoisted;
            }
        }
    }
    (void)budget;
    return hoisted;
}

} // namespace aegis::passes::mid
