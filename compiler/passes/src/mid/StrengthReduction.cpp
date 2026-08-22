// passes/mid/StrengthReduction.cpp — Replace expensive ops with cheaper ones.
//
// Patterns:
//   x * 2^k  ->  x << k           (Mul by power-of-two -> Shl)
//   x * 1    ->  x                 (Mul by 1 -> identity)
//   x * 0    ->  Constant 0        (Mul by 0)
//   x / 2^k  ->  x >> k            (UDiv by power-of-two -> LShr)
//   x + 0    ->  x                 (Add by 0)
//   x - 0    ->  x                 (Sub by 0)
//   x & 0    ->  Constant 0
//   x | 0    ->  x
//   x & ~0   ->  x
//   x | ~0   ->  Constant ~0
//   x ^ 0    ->  x
//   x - x    ->  Constant 0        (Sub of identical values)
//
// Idempotency (Rule B.5): once we replace x * 2 with x << 1, the next
// pass sees a Shl, not a Mul, so it doesn't apply the rule again.
// Monotonic (Rule B.6): every rewrite removes a node (the original
// Mul) and replaces it with a cheaper one — node count is monotone
// decreasing.
#include "aegis/passes/mid/StrengthReduction.hpp"

#include "aegis/ir/NodeKind.hpp"

#include <cstdint>

namespace aegis::passes::mid {

namespace {
bool is_power_of_two(uint64_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }
int  log2_pow2(uint64_t v) noexcept {
    int r = 0;
    while (v > 1) { v >>= 1; ++r; }
    return r;
}
} // namespace

int StrengthReductionPass::run(Graph& g, const PassBudget& budget) {
    int replaced = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        // Rule 73: do NOT hold a Node& across the make_* calls below —
        // they append to the node vector and a reallocation would
        // dangle the reference. Snapshot the fields we need by value.
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (!g[id].is_pure()) continue;
        const NodeKind kind = g[id].kind;
        const TypeId  ty    = g[id].type_id;
        auto d = g[id].data_ins();
        if (d.size() != 2) continue;
        NodeId lhs = d[0];
        NodeId rhs = d[1];
        if (lhs == kInvalidNodeId || rhs == kInvalidNodeId) continue;
        if (lhs >= g.size() || rhs >= g.size()) continue;
        const Node& lhs_n = g[lhs];
        const Node& rhs_n = g[rhs];

        // Helper: is the operand a Constant node?
        auto const_val = [](const Node& nd) -> std::optional<int64_t> {
            if (nd.kind != NodeKind::Constant) return std::nullopt;
            return nd.payload.i64;
        };
        auto lval = const_val(lhs_n);
        auto rval = const_val(rhs_n);

        NodeId new_node = kInvalidNodeId;
        NodeKind new_kind = kind; // default unchanged

        switch (kind) {
            case NodeKind::Mul:
                if (rval && *rval == 1) {
                    // x * 1 -> x. Mark n dead, rewire all uses to lhs.
                    new_node = lhs;
                } else if (rval && *rval == 0) {
                    // x * 0 -> 0.
                    new_node = g.make_constant_i64(0, ty);
                } else if (rval && is_power_of_two(static_cast<uint64_t>(*rval))) {
                    // x * 2^k -> x << k.
                    int k = log2_pow2(static_cast<uint64_t>(*rval));
                    NodeId shift_amt = g.make_constant_i64(k, ty);
                    new_node = g.make_binop(NodeKind::Shl, lhs, shift_amt, ty);
                }
                break;
            case NodeKind::UDiv:
                if (rval && is_power_of_two(static_cast<uint64_t>(*rval)) && *rval > 0) {
                    // x / 2^k -> x >> k (logical).
                    int k = log2_pow2(static_cast<uint64_t>(*rval));
                    NodeId shift_amt = g.make_constant_i64(k, ty);
                    new_node = g.make_binop(NodeKind::LShr, lhs, shift_amt, ty);
                }
                break;
            case NodeKind::Add:
                if (rval && *rval == 0) new_node = lhs;
                else if (lval && *lval == 0) new_node = rhs;
                break;
            case NodeKind::Sub:
                if (rval && *rval == 0) new_node = lhs;
                else if (lhs == rhs) {
                    // x - x = 0.
                    new_node = g.make_constant_i64(0, ty);
                }
                break;
            case NodeKind::And:
                if (rval && *rval == 0) new_node = g.make_constant_i64(0, ty);
                else if (rval && *rval == -1) new_node = lhs;
                else if (lval && *lval == 0) new_node = g.make_constant_i64(0, ty);
                else if (lval && *lval == -1) new_node = rhs;
                break;
            case NodeKind::Or:
                if (rval && *rval == 0) new_node = lhs;
                else if (rval && *rval == -1) new_node = g.make_constant_i64(-1, ty);
                else if (lval && *lval == 0) new_node = rhs;
                else if (lval && *lval == -1) new_node = g.make_constant_i64(-1, ty);
                break;
            case NodeKind::Xor:
                if (rval && *rval == 0) new_node = lhs;
                else if (lval && *lval == 0) new_node = rhs;
                else if (lhs == rhs) new_node = g.make_constant_i64(0, ty);
                break;
            default: break;
        }
        (void)new_kind;

        if (new_node != kInvalidNodeId) {
            // Rewire all downstream uses to point to new_node, then
            // mark the original as Dead. SNAPSHOT first: swap_input
            // mutates this output list mid-iteration otherwise.
            for (NodeId user : g.users_snapshot(id)) {
                g.swap_input(user, id, new_node);
            }
            g[id].flags.set(NodeFlagBit::IsDead); // re-fetch (Rule 73)
            ++replaced;
        }
    }
    (void)budget;
    return replaced;
}

} // namespace aegis::passes::mid
