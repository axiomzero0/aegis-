// passes/mid/SimplifyControl.cpp — Block merging + jump threading.
//
// Jump Threading (sound rewrite):
//   If an If node's condition is a Constant, we know which branch is
//   taken at compile time. We:
//     1. Identify the live Proj (true_proj if cond != 0, else false_proj).
//     2. Rewire all uses of the live Proj's outputs to point at the
//        Proj itself (which becomes the new control token).
//     3. Mark the dead Proj + the If as dead. E-DCE will sweep the
//        dead-branch body.
//
// Block Merging (sound rewrite):
//   If a Region has exactly one live predecessor, fold the Region
//   into that predecessor: replace all uses of the Region with the
//   single_pred, then mark the Region dead.
//
// Both rewrites are SOUND: they don't change the observable behavior,
// they just remove degenerate control-flow constructs.
//
// Rule B.5: idempotent — once merged/threaded, the dead nodes are
// gone and the next pass sees nothing to do.
// Rule B.6: monotone — both rewrites remove nodes.
#include "aegis/passes/mid/SimplifyControl.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"

#include <vector>

namespace aegis::passes::mid {

int SimplifyControlPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;

    // ---- Pass A: Block Merging (single-pred Region -> fold) ----
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Region) continue;
        NodeId single_pred = kInvalidNodeId;
        bool   multiple_live = false;
        for (NodeId in : n.inputs) {
            if (in == kInvalidNodeId || in >= g.size()) continue;
            if (g[in].flags.has(NodeFlagBit::IsDead)) continue;
            if (single_pred == kInvalidNodeId) {
                single_pred = in;
            } else if (single_pred != in) {
                multiple_live = true;
                break;
            }
        }
        if (multiple_live) continue;
        if (single_pred == kInvalidNodeId) continue;
        // SOUND REWRITE: replace all uses of the Region with single_pred.
        // Copy the output list first because swap_input mutates it.
        std::vector<NodeId> user_vec;
        for (NodeId user : g.outputs()[id].view()) user_vec.push_back(user);
        for (NodeId user : user_vec) {
            if (user == id) continue; // avoid self-loop
            g.swap_input(user, id, single_pred);
        }
        n.flags.set(NodeFlagBit::IsDead);
        ++removed;
    }

    // ---- Pass B: Jump Threading (If with Constant cond -> thread) ----
    //
    // SOUND REWRITE:
    //   If the If's condition is a Constant, we know which branch is
    //   taken. We rewire ALL users of BOTH Projs to point at the If's
    //   ctrl_in (the original control token). Then we mark both Projs
    //   + the If as dead. E-DCE sweeps the dead branch's body.
    //
    //   Why rewire the LIVE Proj's users too? Because the live Proj
    //   points at the If, which we're marking dead. If we left the
    //   live Proj alive, its input would point at a dead node (verifier
    //   failure). So we eliminate the entire If + both Projs by
    //   collapsing them into the If's ctrl_in.
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::If) continue;
        if (n.inputs.size() != ir::shape::kIfInputs) continue;
        NodeId cond_id = n.inputs[ir::shape::kIfCondIndex];
        if (cond_id == kInvalidNodeId || cond_id >= g.size()) continue;
        const Node& cond_n = g[cond_id];
        if (cond_n.kind != NodeKind::Constant) continue;
        // The If's ctrl_in is what we'll rewire users to.
        NodeId if_ctrl_in = n.inputs[0];
        if (if_ctrl_in == kInvalidNodeId || if_ctrl_in >= g.size()) continue;
        // Find both Proj outputs.
        NodeId true_proj = kInvalidNodeId;
        NodeId false_proj = kInvalidNodeId;
        for (NodeId user : g.users_snapshot(id)) {
            if (user >= g.size()) continue;
            const Node& u = g[user];
            if (u.kind != NodeKind::Proj) continue;
            if (u.payload.proj_index == 0) true_proj = user;
            else if (u.payload.proj_index == 1) false_proj = user;
        }
        if (true_proj == kInvalidNodeId && false_proj == kInvalidNodeId) continue;

        // CONSTANT-BRANCH PHI COLLAPSE: phis at the merge of THIS If's
        // two projections become single-valued when the branch is
        // constant — rewire their users to the TAKEN side's value and
        // kill the phi. (Pre-fix the phi survived with a dangling
        // region slot, leaving an ambiguous merge downstream — the
        // value was only correct by accident of later passes.)
        {
            const bool taken_true =
                cond_n.payload.i64 != 0; // cond is Constant (checked above)
            for (NodeId proj : {true_proj, false_proj}) {
                if (proj == kInvalidNodeId) continue;
                for (NodeId r : g.users_snapshot(proj)) {
                    if (r >= g.size()) continue;
                    if (g[r].flags.has(NodeFlagBit::IsDead)) continue;
                    if (g[r].kind != NodeKind::Region) continue;
                    // The region must merge EXACTLY this If's two
                    // projections (no third path joining in).
                    if (g[r].inputs.size() != ir::shape::kPhiInputs2Branches - 1)
                        continue;
                    bool is_pair = true;
                    for (NodeId rp : g[r].inputs) {
                        if (rp != true_proj && rp != false_proj) {
                            is_pair = false;
                            break;
                        }
                    }
                    if (!is_pair) continue;
                    for (NodeId phi : g.users_snapshot(r)) {
                        if (phi >= g.size()) continue;
                        if (g[phi].flags.has(NodeFlagBit::IsDead)) continue;
                        if (g[phi].kind != NodeKind::Phi) continue;
                        if (g[phi].inputs.size() !=
                            ir::shape::kPhiInputs2Branches) continue;
                        if (g[phi].inputs[0] != r) continue;
                        const NodeId taken =
                            taken_true ? g[phi].inputs[1] : g[phi].inputs[2];
                        for (NodeId pu : g.users_snapshot(phi)) {
                            g.swap_input(pu, phi, taken);
                        }
                        g[phi].flags.set(NodeFlagBit::IsDead);
                        ++removed;
                    }
                }
            }
        }
        // Rewire ALL users of both Projs to point at if_ctrl_in.
        // (Copy the user lists first because swap_input mutates them.)
        auto rewire_users = [&](NodeId proj_id) {
            if (proj_id == kInvalidNodeId) return;
            std::vector<NodeId> users;
            for (NodeId user : g.outputs()[proj_id].view()) users.push_back(user);
            for (NodeId user : users) {
                if (user == proj_id) continue;
                g.swap_input(user, proj_id, if_ctrl_in);
            }
        };
        rewire_users(true_proj);
        rewire_users(false_proj);
        // Mark both Projs + the If as dead. E-DCE sweeps the dead branch.
        if (true_proj != kInvalidNodeId) g[true_proj].flags.set(NodeFlagBit::IsDead);
        if (false_proj != kInvalidNodeId) g[false_proj].flags.set(NodeFlagBit::IsDead);
        n.flags.set(NodeFlagBit::IsDead);
        ++removed;
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
