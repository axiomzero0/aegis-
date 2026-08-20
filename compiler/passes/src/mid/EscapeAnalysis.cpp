// passes/mid/EscapeAnalysis.cpp — Escape analysis + stack promotion.
//
// Algorithm (function-local, conservative):
//   1. Find every Alloc node in the graph.
//   2. For each Alloc, walk the output edges forward: if the only
//      operations on the resulting pointer are:
//        - Load
//        - Store
//        - GetElementPtr / GetFieldPtr
//      and the pointer never reaches:
//        - a Call (arg or callee)
//        - a Store of the pointer itself (i.e., the pointer escapes
//          into a memory location the function doesn't own)
//        - a Return
//      ...then the Alloc doesn't escape and can be promoted to a
//      StackAlloc + register-held fields.
//   3. Promote non-escaping Allocs:
//        - Replace Alloc with StackAlloc (same effect class semantics
//          work: StackAlloc is Pure).
//        - For each Load on the pointer, rewrite to read from the
//          "register slot" we maintain in the SSA.
//        - For each Store on the pointer, rewrite to write to the
//          register slot.
//      This removes the allocation entirely if all accesses can be
//      registerized; otherwise the StackAlloc remains.
//
// Conservative: a real impl uses PEA (Partial Escape Analysis) to
// handle branches. For the prototype, only function-local Allocs
// whose pointer never crosses a Call / Return boundary are promoted.
#include "aegis/passes/mid/EscapeAnalysis.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"

namespace aegis::passes::mid {

namespace {
// Returns true iff the given Alloc's pointer escapes (reaches a Call
// argument, a Store-of-pointer, a Return-of-pointer, or an atomic).
//
// Walks the forward output edges of `alloc_id`. The walk is "pointer-
// preserving": a Load consumes the pointer (its output is the loaded
// value, not the pointer), so we STOP walking at Load. The same goes
// for AtomicLoad. GetElementPtr / GetFieldPtr / Cast / Select preserve
// the pointer and we keep walking through them.
//
// Pointer-escape triggers:
//   - Return (if val == current node being tracked).
//   - Call (if any data arg == current node).
//   - Store (if value == current node).
//   - AtomicStore / AtomicRMW (if value == current node).
//
// Pointer-preserving (continue walking):
//   - GetElementPtr, GetFieldPtr, Cast, Select (when the data input
//     is the pointer).
//
// Pointer-consuming (STOP — do NOT walk into outputs):
//   - Load, AtomicLoad (they read the pointer; their output is a value
//     of unrelated type).
//   - Store as the *destination* (the pointer is the address; nothing
//     escapes from this use alone).
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
                continue; // pointer not in args; don't walk into Call.
            }
            if (u.kind == NodeKind::Store) {
                auto sd = u.data_ins();
                // data_ins() for Altered nodes = inputs[2..].
                // For Store: inputs = {ctrl, eff, ptr, val}, so
                // data_ins = {ptr, val}.
                if (sd.size() >= 2 && sd[1] == cur) return true;
                // Else `cur` is the Store's destination pointer — that's
                // a pointer-consuming use, not escape. Stop.
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
            // GetElementPtr, GetFieldPtr, Cast, Select.
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
        // The Alloc doesn't escape — promote it. Mark the original
        // Alloc as Dead and replace it with a StackAlloc. A real impl
        // would rewrite the Load/Store uses to register slots, but
        // for the prototype we just flag the promotion.
        n.flags.set(NodeFlagBit::IsStackPromoted);
        ++promoted;
    }
    (void)budget;
    return promoted;
}

} // namespace aegis::passes::mid
