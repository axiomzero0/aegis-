// passes/mid/EscapeAnalysis.cpp — Escape analysis + stack promotion.
//
// Algorithm (function-local, conservative):
//   1. Find every Alloc node in the graph.
//   2. For each Alloc, walk the forward output edges. A Load /
//      GetFieldPtr / Cast / Select on the pointer is safe — we keep
//      walking its outputs. A Return only escapes if its value input
//      IS the current pointer. A Call only escapes if the pointer
//      is one of its data args. A Store only escapes if the pointer
//      is the *value* being stored.
//   3. Promote non-escaping Allocs: rewrite the Alloc node's kind to
//      StackAlloc (Pure, no allocation effect). This eliminates the
//      heap allocation entirely — downstream Load/Store on the
//      pointer now operate on a stack slot.
//
// Soundness: the escape check is conservative — we ONLY promote when
// we can prove the pointer doesn't reach a Return value, a Call data
// arg, a Store value, or an atomic. If any of these reach, we keep
// the Alloc as heap.
//
// Rule B.5: idempotent — once rewritten to StackAlloc, the node is no
// longer an Alloc, so the next pass sees nothing to promote.
// Rule B.6: monotone — we don't add nodes; we just change the kind
// (Alloc -> StackAlloc), which moves the node from Altered to Pure
// effect class. This makes downstream passes (GVN, CSE) able to
// dedup Loads on the pointer.
#include "aegis/passes/mid/EscapeAnalysis.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis::passes::mid {

namespace {
// Returns true iff the given Alloc's pointer escapes (reaches a Call
// argument, a Store-of-pointer, a Return-of-pointer, or an atomic).
bool escapes(Graph& g, NodeId alloc_id) {
    std::vector<NodeId> worklist;
    worklist.push_back(alloc_id);
    std::vector<uint8_t> visited(g.size(), 0);
    visited[alloc_id] = 1;
    while (!worklist.empty()) {
        NodeId cur = worklist.back();
        worklist.pop_back();
        for (NodeId user : g.outputs()[cur].view()) {
            if (user >= g.size()) continue;
            const Node& u = g[user];
            if (u.flags.has(NodeFlagBit::IsDead)) continue;
            // ---- Escape triggers (the pointer is being moved out) ----
            if (u.kind == NodeKind::Return) {
                // Return inputs = {ctrl, eff, val}. If val == cur, the
                // pointer is being returned -> escape.
                if (u.inputs.size() == ir::shape::kReturnInputs &&
                    u.inputs[ir::shape::kReturnValIndex] == cur) return true;
                continue;
            }
            if (u.kind == NodeKind::CallPure ||
                u.kind == NodeKind::CallAltered ||
                u.kind == NodeKind::CallCrowded) {
                for (NodeId in : u.data_ins()) {
                    if (in == cur) return true;
                }
                continue;
            }
            if (u.kind == NodeKind::Store) {
                auto sd = u.data_ins();
                if (sd.size() >= 2 && sd[1] == cur) return true;
                continue;
            }
            if (u.kind == NodeKind::AtomicStore) {
                auto sd = u.data_ins();
                if (sd.size() >= 2 && sd[1] == cur) return true;
                continue;
            }
            if (u.kind == NodeKind::AtomicRMW) return true;
            // ---- Pointer-consuming: STOP ----
            if (u.kind == NodeKind::Load ||
                u.kind == NodeKind::AtomicLoad) {
                continue;
            }
            // ---- Pointer-preserving: continue walking ----
            if (!visited[user]) {
                visited[user] = 1;
                worklist.push_back(user);
            }
        }
    }
    return false;
}
} // namespace

int EscapeAnalysisPass::run(Graph& g, const PassBudget& budget) {
    int promoted = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Alloc) continue;
        if (escapes(g, id)) continue;
        // SOUND REWRITE: change the node's kind from Alloc (Altered
        // effect) to StackAlloc (Pure effect). The pointer now refers
        // to a stack slot instead of a heap object. Downstream
        // Load/Store on the pointer still work — they just read/write
        // the stack slot.
        //
        // This is the standard "stack promotion" rewrite: the Alloc
        // node's effect class changes from Altered to Pure, which
        // makes GVN/CSE able to dedup Loads on the pointer (since
        // Loads on a non-escaping pointer are provably pure).
        n.kind = NodeKind::StackAlloc;
        n.effect = EffectClass::Pure;
        n.flags.set(NodeFlagBit::IsStackPromoted);
        ++promoted;
    }
    (void)budget;
    return promoted;
}

} // namespace aegis::passes::mid
