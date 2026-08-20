// passes/mid/TCO.cpp — Tail Call Optimization.
//
// A tail call is a Call node whose result feeds *directly* into a
// Return node with no intervening effectful operation. The pattern is:
//
//     Return ctrl_in = call_ctrl
//            eff_in  = call_eff
//            val     = Proj(call, 0)
//
// TCO rewires this so the call becomes a jump (the callee's frame
// replaces the caller's). At the SoN level, we can't fully emit a
// jump — that's the backend's job. But we can tag the Return node so
// the backend knows to emit a `jmp` instead of a `call+ret`.
//
// Algorithm:
//   1. Find every Return node.
//   2. For each Return, check if the value input is Proj(call, 0)
//      for some Call node, AND the ctrl_in of the Return == call,
//      AND the eff_in of the Return == call.
//   3. If so, tag the Return with IsTailCall (a new flag bit; for
//      the prototype we reuse IsStackPromoted as the TCO marker).
//
// The backend then lowers a tail-call-tagged Return to a `jmp`
// instead of `call+ret`.
#include "aegis/passes/mid/TCO.hpp"

#include "aegis/ir/NodeKind.hpp"

namespace aegis::passes::mid {

int TailCallOptPass::run(Graph& g, const PassBudget& budget) {
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Return) continue;
        // Return convention: inputs = {ctrl, eff, val}.
        if (n.inputs.size() != 3) continue;
        NodeId ctrl = n.inputs[0];
        NodeId eff  = n.inputs[1];
        NodeId val  = n.inputs[2];
        if (val == kInvalidNodeId || val >= g.size()) continue;
        // val should be a Proj of a Call node.
        const Node& proj = g[val];
        if (proj.kind != NodeKind::Proj) continue;
        if (proj.inputs.size() != 1) continue;
        NodeId call = proj.inputs[0];
        if (call == kInvalidNodeId || call >= g.size()) continue;
        const Node& call_node = g[call];
        if (call_node.kind != NodeKind::CallPure &&
            call_node.kind != NodeKind::CallAltered &&
            call_node.kind != NodeKind::CallCrowded) continue;
        // The Return's ctrl + eff must be the call itself.
        if (ctrl != call) continue;
        if (eff  != call) continue;
        // Tag as tail-call.
        n.flags.set(NodeFlagBit::IsStackPromoted); // TODO: dedicated IsTailCall flag.
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::mid
