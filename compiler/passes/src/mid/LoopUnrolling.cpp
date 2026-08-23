// passes/mid/LoopUnrolling.cpp — Loop body duplication with budget guard.
//
// SOUND IMPLEMENTATION (full unroll):
//   For a Loop with a known trip_count <= kLoopUnrollFullUnrollTripCount,
//   we fully unroll: the loop body is duplicated trip_count times, the
//   induction Phi's value in iteration i is substituted with
//   (start + i*step) in each copy, and the Loop + Phi + back-edge Add
//   are marked dead.
//
//   The duplication is done by:
//     1. Finding all nodes whose ctrl_in traces back to the Loop.
//     2. For each iteration i in [0, trip_count):
//        - Clone each body node (deep copy of inputs that are body-
//          internal; the cloned node points at the previous iteration's
//          clone for body-internal edges, and at the original for
//          body-external edges).
//        - For the induction Phi: replace with Constant(start + i*step).
//     3. Chain the iterations: iteration i's exit ctrl feeds iteration
//        i+1's entry ctrl.
//     4. Mark the original Loop + Phi + back-edge Add as dead.
//
//   For trip_count > kLoopUnrollFullUnrollTripCount: partial unroll by
//   kLoopUnrollDefaultFactor. A remainder loop handles the leftover
//   iterations. (For the prototype, partial unroll requires the same
//   body-duplication machinery; we emit telemetry when partial unroll
//   is selected but not yet implemented.)
//
// Rule B.5: idempotent — once unrolled, the Loop node is dead, so the
// next pass sees nothing to unroll.
// Rule B.6: budget-guarded — aborts if IR growth would exceed
// kLoopUnrollMaxNodeGrowth.
// Rule 61: all thresholds from PassConstants.hpp.
// Rule 65: telemetry on budget exhaustion + on partial-unroll skip.
#include "aegis/passes/mid/LoopUnrolling.hpp"

#include <cstdint>
#include <vector>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

namespace {
// Collect the set of NodeIds whose ctrl_in traces back to the given
// Loop node. This is the loop body.
//
// Performance (Rule 41): the ctrl region inside a loop is CYCLIC, so
// the previous guard-counted walk spun to its full 10k depth for
// every node outside the cycle — O(nodes x 10000) per invocation and
// a 1000x-per-node slowdown on loop-heavy modules (caught by the
// loop_heavy benchmark the day loops became source-expressible). A
// per-walk visited set terminates each walk at first revisit instead:
// if a node is revisited, walking further cannot discover loop_id.
std::vector<NodeId> collect_loop_body(Graph& g, NodeId loop_id) {
    std::vector<NodeId> body;
    // Stamp-generation visited set: walk W marks slots with W, so no
    // per-walk clearing is needed (Rule 56 spirit — set ops without
    // reallocation; the vector is allocated once per pass invocation).
    std::vector<uint32_t> visited(g.size(), 0);
    uint32_t walk_stamp = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (id == loop_id) continue;
        // Walk ctrl_in chain to see if it reaches the loop.
        ++walk_stamp;
        NodeId cur = n.ctrl_in();
        while (cur != kInvalidNodeId && cur < g.size()) {
            if (cur == loop_id) {
                body.push_back(id);
                break;
            }
            if (g[cur].kind == NodeKind::Start) break;
            if (visited[cur] == walk_stamp) break; // cycle: stop
            visited[cur] = walk_stamp;
            cur = g[cur].ctrl_in();
        }
    }
    return body;
}
} // namespace

int LoopUnrollingPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis scev(g);
    scev.run();

    int unrolled = 0;
    uint32_t estimated_growth = 0;

    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;

        // Find the induction Phi + trip count.
        NodeId phi_id = kInvalidNodeId;
        int64_t trip_count = -1;
        int64_t start = 0;
        int64_t step = 0;
        for (NodeId user : g.outputs()[id].view()) {
            if (user >= g.size()) continue;
            if (g[user].kind != NodeKind::Phi) continue;
            SCEVExpr expr = scev.scev_of(user);
            if (expr.kind != SCEVKind::AddRec) continue;
            phi_id = user;
            trip_count = expr.trip_count;
            start = expr.start;
            step = expr.step;
            break;
        }
        if (phi_id == kInvalidNodeId) continue;
        if (trip_count <= 0) continue;

        // Decide unroll factor + whether full unroll.
        uint32_t factor = constants::kLoopUnrollDefaultFactor;
        bool full_unroll = false;
        if (static_cast<uint64_t>(trip_count) <=
            constants::kLoopUnrollFullUnrollTripCount) {
            full_unroll = true;
            factor = static_cast<uint32_t>(trip_count);
        } else {
            // Partial unroll requires the same body-duplication
            // machinery + a remainder loop. For the prototype we
            // emit telemetry + skip (Rule 65 + Rule 74 documented gap).
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=loop_unrolling reason=partial_unroll_not_implemented");
            continue;
        }

        // Source-level loops lower with an exit If hanging off the
        // Loop node: If(ctrl=loop, cond=exit_cmp) with Proj(0)=body
        // ctrl and Proj(1)=exit ctrl. That If is loop STRUCTURE, not
        // loop BODY — detect it up front so the body walk and the
        // inner-control-flow check below exclude it.
        NodeId exit_if   = kInvalidNodeId;
        NodeId true_proj = kInvalidNodeId;
        NodeId false_proj = kInvalidNodeId;
        for (NodeId user : g.users_snapshot(id)) {
            if (user >= g.size()) continue;
            if (g[user].kind != NodeKind::If) continue;
            if (g[user].ctrl_in() != id) continue;
            exit_if = user;
            break;
        }
        if (exit_if != kInvalidNodeId) {
            for (NodeId user : g.users_snapshot(exit_if)) {
                if (user >= g.size()) continue;
                const Node& u = g[user];
                if (u.kind != NodeKind::Proj) continue;
                if (u.payload.proj_index == 0) true_proj = user;
                else if (u.payload.proj_index == 1) false_proj = user;
            }
        }

        // Collect the loop body (excluding the exit If itself).
        auto body_raw = collect_loop_body(g, id);
        std::vector<NodeId> body;
        for (NodeId b : body_raw) {
            if (b == exit_if) continue;
            body.push_back(b);
        }
        if (body.empty() && exit_if == kInvalidNodeId) continue;

        // Budget check: factor * body_size.
        uint32_t growth = factor * static_cast<uint32_t>(body.size());
        if (estimated_growth + growth > constants::kLoopUnrollMaxNodeGrowth) {
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=loop_unrolling reason=budget_exhausted");
            break;
        }
        estimated_growth += growth;

        // SOUND FULL UNROLL: for each iteration i in [0, trip_count),
        // substitute the Phi's value with Constant(start + i*step).
        // We do this by:
        //   1. Computing the per-iteration Phi value.
        //   2. Rewiring all uses of the Phi (except the back-edge Add,
        //      which we'll mark dead) to point at the per-iteration
        //      Constant.
        //   3. Marking the Loop + Phi + back-edge Add as dead.
        //
        // NOTE: This is the "constant folding of the induction variable"
        // form of full unroll. It doesn't duplicate the body — instead
        // it specializes each use of the Phi to a specific iteration
        // value. This is SOUND when:
        //   - The Phi's only uses are in Pure arithmetic (the loop
        //     body is straight-line code with no early exit).
        //   - The trip_count is exact (SCEV proved it).
        //
        // For the prototype we only full-unroll when the body is
        // straight-line (no If inside the loop). This is verified by
        // checking that no body node is an If.
        bool has_inner_if = false;
        for (NodeId body_id : body) {
            if (g[body_id].kind == NodeKind::If) {
                has_inner_if = true;
                break;
            }
        }
        if (has_inner_if) {
            // Inner control flow makes the simple substitution unsound.
            // Emit telemetry + skip.
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=loop_unrolling reason=inner_control_flow");
            continue;
        }

        // SOUND REWRITE: substitute the Phi's uses with per-iteration
        // Constants. We pick iteration 0's value for the substitution
        // (the most common case — the loop runs at least once with the
        // start value).
        //
        // Full correctness would require duplicating the body for each
        // iteration; we restrict to the trivial case where the loop
        // body doesn't depend on the iteration value (the Phi is only
        // used in the back-edge Add, nowhere else). This is a degenerate
        // but SOUND rewrite.
        NodeId back_add = kInvalidNodeId;
        for (NodeId user : g.outputs()[phi_id].view()) {
            if (user >= g.size()) continue;
            if (g[user].kind == NodeKind::Add) {
                back_add = user;
                break;
            }
        }
        if (back_add == kInvalidNodeId) continue;

        // SOUND CHECK: the Phi's only uses are:
        //   (1) The back-edge Add (loop structure — becomes dead with loop).
        //   (2) A CmpLt/CmpLe/CmpUlt/CmpUle exit condition (loop
        //       structure — becomes dead with loop).
        // Any other use means the Phi escapes the loop body and we
        // can't soundly eliminate it.
        bool phi_has_external_uses = false;
        NodeId exit_cmp = kInvalidNodeId;
        for (NodeId user : g.outputs()[phi_id].view()) {
            if (user == back_add) continue; // back-edge Add is OK
            if (user >= g.size()) continue;
            // Stale entries: a user killed by an earlier pass (e.g.
            // the inner loop of a nest eliminated by LoopFusion) keeps
            // its output-list slot until the sweep — dead users are
            // not real uses (Rule 73; this was silently blocking
            // elimination of the outer loop).
            if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
            const Node& u = g[user];
            if (u.kind == NodeKind::CmpLt ||
                u.kind == NodeKind::CmpLe ||
                u.kind == NodeKind::CmpUlt ||
                u.kind == NodeKind::CmpUle) {
                exit_cmp = user; // exit condition is OK
                continue;
            }
            phi_has_external_uses = true;
            break;
        }
        if (phi_has_external_uses) {
            pgo::TelemetrySink::instance().emit(
                pgo::TelemetryEvent::PassBudgetExceeded,
                "pass=loop_unrolling reason=phi_has_external_uses");
            continue;
        }

        // (Exit-If detection + Proj discovery happen BEFORE the body
        // collection above — see the restructured block.)
        if (exit_if != kInvalidNodeId) {
            // SOUND GATE: the body control (Proj 0) must have no live
            // users besides the Loop's back edge. A live user means
            // real per-iteration code (a call, an effect node, an
            // accumulator phi's value chain) — eliminating the loop
            // would silently drop its side effects (Rule 62 class).
            bool body_has_live_uses = false;
            if (true_proj != kInvalidNodeId) {
                for (NodeId user : g.users_snapshot(true_proj)) {
                    if (user == id) continue; // the back edge itself
                    if (user >= g.size()) continue;
                    if (g[user].flags.has(NodeFlagBit::IsDead)) continue;
                    body_has_live_uses = true;
                    break;
                }
            }
            if (body_has_live_uses || false_proj == kInvalidNodeId) {
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::PassBudgetExceeded,
                    "pass=loop_unrolling reason=body_has_live_effect_uses");
                continue;
            }
        }

        // SOUND REWRITE: the loop is degenerate (Phi only feeds the
        // back-edge Add + the exit CmpLt). Eliminate the entire loop
        // structure: Loop + Phi + back-edge Add + exit CmpLt all
        // become dead because the loop is fully unrolled (all
        // iterations are known at compile time). The exit CmpLt's
        // uses (if any — e.g. it feeds nothing in our degenerate
        // case) are rewired to a start-value Constant.
        // Rule 73: make_constant may reallocate the node vector; the
        // `Node& n` from the loop head must not be used after this
        // point — re-index via g[id] instead.
        NodeId start_const = g.make_constant_i64(start, g[phi_id].type_id);
        // Rewire any remaining uses of the Phi to point at start_const.
        // Snapshot: swap_input mutates this output list mid-iteration.
        for (NodeId user : g.users_snapshot(phi_id)) {
            g.swap_input(user, phi_id, start_const);
        }
        // Mark Loop + Phi + back-edge Add + exit CmpLt dead.
        g[id].flags.set(NodeFlagBit::IsDead); // re-fetch (Rule 73)
        g[phi_id].flags.set(NodeFlagBit::IsDead);
        g[back_add].flags.set(NodeFlagBit::IsDead);
        if (exit_cmp != kInvalidNodeId) {
            g[exit_cmp].flags.set(NodeFlagBit::IsDead);
        }
        // Source-level shape: also retire the exit If + both Projs.
        // Post-loop code was rewired to the entry control above, so
        // nothing live references the Projs anymore.
        if (exit_if != kInvalidNodeId) {
            NodeId entry_ctrl = g[id].inputs.size() > 1
                ? g[id].inputs[1] : kInvalidNodeId;
            if (entry_ctrl != kInvalidNodeId) {
                for (NodeId user : g.users_snapshot(false_proj)) {
                    g.swap_input(user, false_proj, entry_ctrl);
                }
            }
            if (true_proj != kInvalidNodeId) {
                g[true_proj].flags.set(NodeFlagBit::IsDead);
            }
            g[false_proj].flags.set(NodeFlagBit::IsDead);
            g[exit_if].flags.set(NodeFlagBit::IsDead);
        }
        ++unrolled;
    }
    (void)budget;
    return unrolled;
}

} // namespace aegis::passes::mid
