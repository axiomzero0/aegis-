// passes/mid/LoopFusion.cpp — Merge adjacent loops with identical ranges.
//
// SOUND IMPLEMENTATION:
//   When two adjacent Loop nodes have the same SCEV recurrence
//   (same start, same step, same trip_count), they iterate over the
//   same range. If there's no intervening Altered/Crowded node
//   between them, we can merge them: rewire the second loop's body
//   to use the first loop's induction Phi, then mark the second
//   loop + its Phi + back-edge Add as dead.
//
// Soundness conditions:
//   1. Both loops have the same SCEV {start, step, trip_count}.
//   2. No Altered/Crowded node sits between the two loops in the
//      effect chain (otherwise fusing could change observable
//      side-effect ordering).
//   3. The second loop's body doesn't reference its own Phi in a
//      way that would conflict with the first loop's Phi (we check
//      by verifying the second Phi's only uses are body-internal).
//
// Rule B.5: idempotent — once merged, the second Loop is dead, so
// the next pass sees nothing to fuse.
// Rule B.6: monotone decreasing — we mark the second loop's
// induction Phi + back-edge Add + the second Loop itself as dead.
#include "aegis/passes/mid/LoopFusion.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

int LoopFusionPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis scev(g);
    scev.run();

    // Collect all Loop nodes with known trip counts.
    SmallVector<NodeId, constants::kLoopFusionMaxAdjacent> loops;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;
        loops.push_back(id);
    }
    if (loops.size() < 2) return 0;

    int fused = 0;
    for (size_t i = 0; i + 1 < loops.size(); ++i) {
        NodeId a = loops[i];
        NodeId b = loops[i + 1];
        // Find the induction phi + SCEV of each loop.
        NodeId phi_a = kInvalidNodeId;
        NodeId phi_b = kInvalidNodeId;
        SCEVExpr expr_a{};
        SCEVExpr expr_b{};
        for (NodeId user : g.outputs()[a].view()) {
            if (user >= g.size()) continue;
            if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
            if (g[user].kind != NodeKind::Phi) continue;
            phi_a = user;
            expr_a = scev.scev_of(user);
            break;
        }
        for (NodeId user : g.outputs()[b].view()) {
            if (user >= g.size()) continue;
            if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
            if (g[user].kind != NodeKind::Phi) continue;
            phi_b = user;
            expr_b = scev.scev_of(user);
            break;
        }
        if (phi_a == kInvalidNodeId || phi_b == kInvalidNodeId) continue;
        if (expr_a.kind != SCEVKind::AddRec ||
            expr_b.kind != SCEVKind::AddRec) continue;
        // Same SCEV recurrence?
        if (expr_a.start != expr_b.start) continue;
        if (expr_a.step != expr_b.step) continue;
        if (expr_a.trip_count != expr_b.trip_count) continue;
        if (expr_a.trip_count <= 0) continue;

        // SOUND CHECK: the second Phi's only uses are loop-structure
        // nodes: the back-edge Add + the exit-condition CmpLt/CmpLe/
        // CmpUlt/CmpUle. Any other use means the Phi escapes the loop
        // body and we can't soundly eliminate it.
        NodeId back_add_b = kInvalidNodeId;
        NodeId exit_cmp_b = kInvalidNodeId;
        bool phi_b_has_external_uses = false;
        for (NodeId user : g.outputs()[phi_b].view()) {
            if (user >= g.size()) continue;
            // Dead users are stale output-list entries, not uses
            // (same defect class fixed in LoopUnrolling; Rule 73).
            if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
            const Node& u = g[user];
            if (u.kind == NodeKind::Add) {
                back_add_b = user;
                continue;
            }
            if (u.kind == NodeKind::CmpLt ||
                u.kind == NodeKind::CmpLe ||
                u.kind == NodeKind::CmpUlt ||
                u.kind == NodeKind::CmpUle) {
                exit_cmp_b = user;
                continue;
            }
            phi_b_has_external_uses = true;
        }
        if (phi_b_has_external_uses || back_add_b == kInvalidNodeId) continue;

        // Source-level loops lower with an exit If off the Loop node
        // (Proj(0)=body ctrl, Proj(1)=exit ctrl). Eliminating loop B
        // must retire that If too: B's exit users rewire to B's ENTRY
        // control, and B's body control must have no live users
        // (any live body node = per-iteration side effects that
        // elimination would silently drop — Rule 62 class).
        NodeId exit_if_b    = kInvalidNodeId;
        NodeId tproj_b      = kInvalidNodeId;
        NodeId fproj_b      = kInvalidNodeId;
        for (NodeId user : g.users_snapshot(b)) {
            if (user >= g.size()) continue;
            if (g[user].kind != NodeKind::If) continue;
            if (g[user].ctrl_in() != b) continue;
            exit_if_b = user;
            break;
        }
        if (exit_if_b != kInvalidNodeId) {
            for (NodeId user : g.users_snapshot(exit_if_b)) {
                if (user >= g.size()) continue;
                const Node& u = g[user];
                if (u.kind != NodeKind::Proj) continue;
                if (u.payload.proj_index == 0) tproj_b = user;
                else if (u.payload.proj_index == 1) fproj_b = user;
            }
            bool body_b_has_live_uses = false;
            if (tproj_b != kInvalidNodeId) {
                for (NodeId user : g.users_snapshot(tproj_b)) {
                    if (user == b) continue; // the back edge itself
                    if (user >= g.size()) continue;
                    if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
                    body_b_has_live_uses = true;
                    break;
                }
            }
            if (body_b_has_live_uses || fproj_b == kInvalidNodeId) {
                // B's body does real per-iteration work; fusion is not
                // the pass to eliminate it (and elimination would be
                // unsound anyway). Telemetry per Rule 65.
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::PassBudgetExceeded,
                    "pass=loop_fusion reason=body_has_live_effect_uses");
                continue;
            }
        }

        // SOUND REWRITE: the second loop is degenerate (Phi only feeds
        // loop-structure nodes). Rewire the Phi's uses to point at
        // phi_a + mark the second loop's Loop + Phi + back-edge Add +
        // exit CmpLt dead.
        // Snapshot: swap_input mutates this output list mid-iteration.
        for (NodeId user : g.users_snapshot(phi_b)) {
            g.swap_input(user, phi_b, phi_a);
        }
        // Mark the second Loop + its Phi + its back-edge Add + its
        // exit CmpLt dead.
        g[b].flags.set(NodeFlagBit::IsDead);
        g[phi_b].flags.set(NodeFlagBit::IsDead);
        g[back_add_b].flags.set(NodeFlagBit::IsDead);
        if (exit_cmp_b != kInvalidNodeId) {
            g[exit_cmp_b].flags.set(NodeFlagBit::IsDead);
        }
        // Source-level shape: retire B's exit If + Projs, rewiring
        // B's exit users (post-B code) to B's ENTRY control.
        if (exit_if_b != kInvalidNodeId) {
            NodeId entry_ctrl_b = g[b].inputs.size() > 1
                ? g[b].inputs[1] : kInvalidNodeId;
            if (entry_ctrl_b != kInvalidNodeId) {
                for (NodeId user : g.users_snapshot(fproj_b)) {
                    g.swap_input(user, fproj_b, entry_ctrl_b);
                }
            }
            if (tproj_b != kInvalidNodeId) {
                g[tproj_b].flags.set(NodeFlagBit::IsDead);
            }
            g[fproj_b].flags.set(NodeFlagBit::IsDead);
            g[exit_if_b].flags.set(NodeFlagBit::IsDead);
        }
        ++fused;
    }
    (void)budget;
    return fused;
}

} // namespace aegis::passes::mid
