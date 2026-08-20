// passes/mid/SCEV.cpp — Scalar Evolution analysis.
//
// For the prototype we recognize the most common recurrence:
//
//   loop_id = Loop(entry, back)
//   phi     = Phi(loop_id, [start, back_value])
//   back_value = Add(phi, step)
//
// If the loop's exit condition is `CmpLt(phi, n)` with n a Constant,
// we know the trip count is n - start (when start < n).
//
// Algorithm:
//   1. Find all Loop nodes.
//   2. For each Loop, find its induction Phi: a Phi node whose first
//      input is the Loop node and whose two value inputs are
//      (start_constant, Add(phi, step_constant)).
//   3. Record SCEVExpr {start=start_constant, step=step_constant,
//      trip_count=-1} for the Phi node.
//   4. Look for an If node inside the loop whose condition is
//      CmpLt(phi, n_constant). Compute trip_count = n - start.
//   5. Record the SCEVExpr for the Add node too: same {start, step,
//      trip_count}.
//
// Other passes can query scev_of(phi_id) to learn the recurrence and
// scev_of(add_id) for the updated value at any iteration.
#include "aegis/passes/mid/SCEV.hpp"

#include "aegis/ir/NodeKind.hpp"

#include <unordered_map>

namespace aegis::passes::mid {

int SCEVAnalysis::run() noexcept {
    int found = 0;
    for (NodeId loop_id = 0; loop_id < g_.size(); ++loop_id) {
        const Node& lp = g_[loop_id];
        if (lp.flags.has(NodeFlagBit::IsDead)) continue;
        if (lp.kind != NodeKind::Loop) continue;
        // Find the Phi that belongs to this loop.
        for (NodeId user : g_.outputs()[loop_id].view()) {
            if (user >= g_.size()) continue;
            Node& phi = g_[user];
            if (phi.kind != NodeKind::Phi) continue;
            // phi.inputs = {loop_id, start_val, back_val}.
            if (phi.inputs.size() != 3) continue;
            NodeId start = phi.inputs[1];
            NodeId back  = phi.inputs[2];
            if (start == kInvalidNodeId || back == kInvalidNodeId) continue;
            // start must be a Constant.
            const Node& start_n = g_[start];
            if (start_n.kind != NodeKind::Constant) continue;
            // back must be an Add(phi, step_constant).
            const Node& back_n = g_[back];
            if (back_n.kind != NodeKind::Add) continue;
            auto bd = back_n.data_ins();
            if (bd.size() != 2) continue;
            // One operand is phi (us), the other is the step Constant.
            NodeId step_id = kInvalidNodeId;
            for (NodeId in : bd) {
                if (in == user) continue;
                step_id = in;
            }
            if (step_id == kInvalidNodeId || step_id >= g_.size()) continue;
            const Node& step_n = g_[step_id];
            if (step_n.kind != NodeKind::Constant) continue;

            SCEVExpr expr;
            expr.kind = SCEVKind::AddRec;
            expr.start = start_n.payload.i64;
            expr.step  = step_n.payload.i64;
            expr.trip_count = -1;
            // Try to find the exit condition: a CmpLt(phi, n_const)
            // somewhere downstream.
            for (NodeId user2 : g_.outputs()[user].view()) {
                if (user2 >= g_.size()) continue;
                Node& cmp = g_[user2];
                if (cmp.kind != NodeKind::CmpLt && cmp.kind != NodeKind::CmpUlt) continue;
                auto cd = cmp.data_ins();
                if (cd.size() != 2) continue;
                NodeId cmp_lhs = cd[0];
                NodeId cmp_rhs = cd[1];
                if (cmp_lhs != user) continue;
                if (cmp_rhs == kInvalidNodeId || cmp_rhs >= g_.size()) continue;
                const Node& n_const = g_[cmp_rhs];
                if (n_const.kind != NodeKind::Constant) continue;
                int64_t n = n_const.payload.i64;
                int64_t trip = n - expr.start;
                if (trip > 0) {
                    expr.trip_count = trip;
                    break;
                }
            }
            map_[user] = expr;
            map_[back] = expr;
            ++found;
        }
    }
    return found;
}

SCEVExpr SCEVAnalysis::scev_of(NodeId id) const noexcept {
    auto it = map_.find(id);
    if (it == map_.end()) return SCEVExpr{};
    return it->second;
}

int64_t SCEVAnalysis::trip_count_of(NodeId id) const noexcept {
    auto it = map_.find(id);
    if (it == map_.end()) return -1;
    return it->second.trip_count;
}

int SCEVPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis a(g);
    int n = a.run();
    (void)budget;
    return n;
}

} // namespace aegis::passes::mid
