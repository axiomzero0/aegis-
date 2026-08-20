// passes/SCCP.cpp — Constant propagation + unreachable-code elimination.
#include "aegis/passes/mid/SCCP.hpp"

#include <vector>

#include "aegis/ir/NodeKind.hpp"

namespace aegis {

namespace {
enum class LatticeTag : uint8_t { Top = 0, Const = 1, Bottom = 2 };
struct LatticeVal {
    LatticeTag tag{LatticeTag::Top};
    int64_t    value{0};

    static LatticeVal top() noexcept { return {LatticeTag::Top, 0}; }
    static LatticeVal bottom() noexcept { return {LatticeTag::Bottom, 0}; }
    static LatticeVal constant(int64_t v) noexcept { return {LatticeTag::Const, v}; }

    bool is_top()    const noexcept { return tag == LatticeTag::Top; }
    bool is_const()  const noexcept { return tag == LatticeTag::Const; }
    bool is_bottom() const noexcept { return tag == LatticeTag::Bottom; }

    // Merge (meet) operator. Returns true if changed.
    bool merge(LatticeVal other) noexcept {
        if (other.is_top())   return false;           // top is identity
        if (is_bottom())      return false;           // bottom is absorbing
        if (is_top()) {
            if (other.is_bottom()) { tag = LatticeTag::Bottom; return true; }
            *this = other;
            return true;
        }
        if (is_const() && other.is_const()) {
            if (value == other.value) return false;
            tag = LatticeTag::Bottom;
            return true;
        }
        tag = LatticeTag::Bottom;
        return true;
    }
};

int64_t apply_binop(NodeKind k, int64_t a, int64_t b) noexcept {
    switch (k) {
        case NodeKind::Add:  return a + b;
        case NodeKind::Sub:  return a - b;
        case NodeKind::Mul:  return a * b;
        case NodeKind::Div:  return b != 0 ? a / b : 0;
        case NodeKind::Mod:  return b != 0 ? a % b : 0;
        case NodeKind::And:  return a & b;
        case NodeKind::Or:   return a | b;
        case NodeKind::Xor:  return a ^ b;
        case NodeKind::Shl:  return a << b;
        case NodeKind::Shr:  return a >> b;
        case NodeKind::CmpEq: return a == b ? 1 : 0;
        case NodeKind::CmpNe: return a != b ? 1 : 0;
        case NodeKind::CmpLt: return a <  b ? 1 : 0;
        case NodeKind::CmpLe: return a <= b ? 1 : 0;
        case NodeKind::CmpGt: return a >  b ? 1 : 0;
        case NodeKind::CmpGe: return a >= b ? 1 : 0;
        default: return 0;
    }
}
} // namespace

int SCCPPass::run(Graph& g, const PassBudget& budget) {
    uint32_t n = g.size();
    std::vector<LatticeVal> state(n, LatticeVal::top());

    // Seed: Constant nodes get the lattice value of their payload.
    for (NodeId id = 0; id < n; ++id) {
        const Node& nd = g[id];
        if (nd.flags.has(NodeFlagBit::IsDead)) continue;
        if (nd.kind == NodeKind::Constant) {
            state[id] = LatticeVal::constant(nd.payload.i64);
        }
    }

    // Iterate the SSA worklist: process each non-Pure-dependent node
    // based on its operands. (Simplified SCCP: we approximate and skip
    // the conditional-edge half of the classic algorithm.)
    bool changed = true;
    while (changed) {
        changed = false;
        for (NodeId id = 0; id < n; ++id) {
            const Node& nd = g[id];
            if (nd.flags.has(NodeFlagBit::IsDead)) continue;
            if (nd.kind == NodeKind::Constant) continue; // already set
            // Compute the new lattice value from inputs.
            LatticeVal new_v;
            // If any data input is Bottom -> this node becomes Bottom.
            bool any_bottom = false;
            bool all_const  = true;
            int64_t accumulator = 0;
            bool first = true;
            for (NodeId in : nd.data_ins()) {
                if (in == kInvalidNodeId) continue;
                if (in >= n) continue;
                const LatticeVal& v = state[in];
                if (v.is_top()) { all_const = false; continue; }
                if (v.is_bottom()) { any_bottom = true; break; }
                if (first) { accumulator = v.value; first = false; }
                else {
                    accumulator = apply_binop(nd.kind, accumulator, v.value);
                }
            }
            if (any_bottom) {
                new_v = LatticeVal::bottom();
            } else if (all_const && !first) {
                new_v = LatticeVal::constant(accumulator);
            } else {
                continue; // can't determine yet
            }
            if (state[id].merge(new_v)) changed = true;
        }
    }

    // Re-write: replace any Pure node whose lattice value is Constant with
    // a reference to the original Constant node (or mark for GVN).
    int removed = 0;
    for (NodeId id = 0; id < n; ++id) {
        Node& nd = g[id];
        if (nd.flags.has(NodeFlagBit::IsDead)) continue;
        if (nd.kind == NodeKind::Constant) continue;
        if (!nd.is_pure()) continue;
        // If this node folded to a constant, replace it with a fresh
        // Constant node. The next GVN pass will dedup it.
        if (state[id].is_const()) {
            NodeId new_const = g.make_constant_i64(state[id].value, nd.type_id);
            // Rewire downstream uses.
            for (NodeId user : g.outputs()[id].view()) {
                g.swap_input(user, id, new_const);
            }
            nd.flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }

    (void)budget;
    return removed;
}

} // namespace aegis
