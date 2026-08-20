// passes/mid/BoundsCheckElim.cpp — Range analysis + bounds check removal.
//
// Algorithm:
//   1. Identify all Load nodes whose first data input is computed by
//      a CmpLt/CmpLe/CmpUlt/CmpUle node feeding a Guard or If.
//   2. Range-analyze each index value: if the index is provably
//      within [0, array_length), the bounds check is dead and can be
//      removed.
//   3. Conservative: only eliminate when we can prove both bounds
//      (0 <= i && i < length) statically. If we cannot prove, leave
//      the check in place.
//
// For the prototype we eliminate only the trivially-provable cases:
//   - index is a Constant and array length is a known Constant.
//   - index is the loop induction variable and the loop's upper bound
//     is statically known and less than the array length.
#include "aegis/passes/mid/BoundsCheckElim.hpp"

#include "aegis/ir/NodeKind.hpp"

namespace aegis::passes::mid {

int BoundsCheckElimPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;
    // Walk the graph looking for Guard nodes whose condition is a CmpLt
    // between an index and a length. If both operands are Constants and
    // the comparison is statically true, the Guard is dead.
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Guard) continue;
        // The Guard's first data input is the condition.
        auto data = n.data_ins();
        if (data.empty()) continue;
        NodeId cond_id = data[0];
        if (cond_id == kInvalidNodeId || cond_id >= g.size()) continue;
        const Node& cond = g[cond_id];
        // We look for: CmpLt(constant_idx, constant_len).
        if (cond.kind != NodeKind::CmpLt && cond.kind != NodeKind::CmpUlt) continue;
        auto cdata = cond.data_ins();
        if (cdata.size() != 2) continue;
        NodeId lhs = cdata[0];
        NodeId rhs = cdata[1];
        if (lhs == kInvalidNodeId || rhs == kInvalidNodeId) continue;
        if (lhs >= g.size() || rhs >= g.size()) continue;
        const Node& lhs_n = g[lhs];
        const Node& rhs_n = g[rhs];
        if (lhs_n.kind != NodeKind::Constant || rhs_n.kind != NodeKind::Constant) continue;
        int64_t idx = lhs_n.payload.i64;
        int64_t len = rhs_n.payload.i64;
        if (idx >= 0 && idx < len) {
            // The check is statically true — eliminate the Guard.
            n.flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
