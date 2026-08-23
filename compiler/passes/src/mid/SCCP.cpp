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

// Fold a binary op over two known constants. Returns nullopt for
// kinds that are not binary foldable — folding those to any value
// would be a silent wrong answer (Rule D.3).
std::optional<int64_t> apply_binop(NodeKind k, int64_t a, int64_t b) noexcept {
    switch (k) {
        case NodeKind::Add:  return a + b;
        case NodeKind::Sub:  return a - b;
        case NodeKind::Mul:  return a * b;
        case NodeKind::Div:  return b != 0 ? std::optional<int64_t>{a / b} : std::nullopt;
        case NodeKind::Mod:  return b != 0 ? std::optional<int64_t>{a % b} : std::nullopt;
        case NodeKind::UDiv: return b != 0 ? std::optional<int64_t>{static_cast<int64_t>(
                                     static_cast<uint64_t>(a) / static_cast<uint64_t>(b))}
                                   : std::nullopt;
        case NodeKind::UMod: return b != 0 ? std::optional<int64_t>{static_cast<int64_t>(
                                     static_cast<uint64_t>(a) % static_cast<uint64_t>(b))}
                                   : std::nullopt;
        case NodeKind::And:  return a & b;
        case NodeKind::Or:   return a | b;
        case NodeKind::Xor:  return a ^ b;
        case NodeKind::Shl:  return a << b;
        case NodeKind::Shr:  return a >> b;
        case NodeKind::LShr: return static_cast<int64_t>(
                                     static_cast<uint64_t>(a) >> static_cast<uint64_t>(b));
        case NodeKind::CmpEq: return a == b ? 1 : 0;
        case NodeKind::CmpNe: return a != b ? 1 : 0;
        case NodeKind::CmpLt: return a <  b ? 1 : 0;
        case NodeKind::CmpLe: return a <= b ? 1 : 0;
        case NodeKind::CmpGt: return a >  b ? 1 : 0;
        case NodeKind::CmpGe: return a >= b ? 1 : 0;
        case NodeKind::CmpUlt: return static_cast<uint64_t>(a) <  static_cast<uint64_t>(b) ? 1 : 0;
        case NodeKind::CmpUle: return static_cast<uint64_t>(a) <= static_cast<uint64_t>(b) ? 1 : 0;
        case NodeKind::CmpUgt: return static_cast<uint64_t>(a) >  static_cast<uint64_t>(b) ? 1 : 0;
        case NodeKind::CmpUge: return static_cast<uint64_t>(a) >= static_cast<uint64_t>(b) ? 1 : 0;
        default: return std::nullopt;
    }
}

// Fold a unary op over one known constant. Returns nullopt for kinds
// that are not unary foldable.
std::optional<int64_t> apply_unop(NodeKind k, int64_t a) noexcept {
    switch (k) {
        case NodeKind::Neg:    return -a;
        case NodeKind::Not:    return a == 0 ? 1 : 0; // logical not (!x)
        case NodeKind::BitNot: return ~a;             // bitwise not (~x)
        default: return std::nullopt;
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
            // Phi: fold ONLY when EVERY data input is the SAME
            // constant. A TOP input is "no information" (e.g. an
            // Altered producer like a call never computes a lattice
            // value) and MUST poison the meet to Bottom — treating it
            // as an unreachable edge silently folded phi(7,
            // call_result) to 7, deleting the else arm and the call
            // with it (caught as a wrong mutual-recursion result by
            // the runtime differential harness). Distinct constants
            // also meet to Bottom (the branch decides at runtime).
            if (nd.kind == NodeKind::Phi) {
                LatticeVal meet;
                bool have_any = false;
                bool poison = false;
                for (NodeId in : nd.data_ins()) {
                    if (in == kInvalidNodeId || in >= n) continue;
                    const Node& in_node = g[in];
                    // The region/loop slot is structural, not a value.
                    if (in_node.kind == NodeKind::Region ||
                        in_node.kind == NodeKind::Loop) {
                        continue;
                    }
                    const LatticeVal& v = state[in];
                    if (v.is_top() || v.is_bottom()) {
                        poison = true; // unknown or overdefined input
                        break;
                    }
                    if (!have_any) {
                        meet = v;
                        have_any = true;
                    } else if (meet.value != v.value) {
                        poison = true; // differing constants
                        break;
                    }
                }
                if (poison || !have_any) {
                    if (state[id].merge(LatticeVal::bottom())) changed = true;
                } else if (state[id].merge(meet)) {
                    changed = true;
                }
                continue;
            }
            // Unary ops: Neg / Not / BitNot. (Pre-fix these folded to
            // the operand unchanged — the negation/not was dropped.)
            if (nd.kind == NodeKind::Neg || nd.kind == NodeKind::Not ||
                nd.kind == NodeKind::BitNot) {
                NodeId in0 = nd.data_ins().empty()
                    ? kInvalidNodeId : nd.data_ins()[0];
                if (in0 == kInvalidNodeId || in0 >= n) continue;
                const LatticeVal& v = state[in0];
                if (v.is_bottom()) {
                    if (state[id].merge(LatticeVal::bottom())) changed = true;
                    continue;
                }
                if (v.is_top()) continue;
                std::optional<int64_t> folded = apply_unop(nd.kind, v.value);
                if (folded && state[id].merge(LatticeVal::constant(*folded))) {
                    changed = true;
                }
                continue;
            }
            // Binary ops: fold only when BOTH inputs are the same
            // known constant and the op is foldable.
            {
                LatticeVal new_v;
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
                        std::optional<int64_t> folded =
                            apply_binop(nd.kind, accumulator, v.value);
                        if (!folded) { all_const = false; break; }
                        accumulator = *folded;
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
    }

    // Re-write: replace any Pure node whose lattice value is Constant with
    // a reference to the original Constant node (or mark for GVN).
    int removed = 0;
    for (NodeId id = 0; id < n; ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind == NodeKind::Constant) continue;
        if (!g[id].is_pure()) continue;
        // If this node folded to a constant, replace it with a fresh
        // Constant node. The next GVN pass will dedup it.
        if (state[id].is_const()) {
            // Rule 73: capture the type by value BEFORE make_constant
            // appends to the node vector — a Node& held across the
            // append dangles after reallocation, and writing through
            // it corrupts unrelated heap memory (the pre-fix defect
            // the Rule 42 verifier surfaced as bogus use-def edges).
            TypeId folded_ty = g[id].type_id;
            NodeId new_const = g.make_constant_i64(state[id].value, folded_ty);
            // Rewire downstream uses. SNAPSHOT first: swap_input
            // removes `user` from this very output list, and iterating
            // the live view while mutating it corrupts the use-def map
            // (caught by the Rule 42 verifier as a use-def mismatch).
            for (NodeId user : g.users_snapshot(id)) {
                g.swap_input(user, id, new_const);
            }
            // Re-fetch by id: make_constant may have reallocated the
            // node vector (Rule 73).
            g[id].flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }

    (void)budget;
    return removed;
}

} // namespace aegis
