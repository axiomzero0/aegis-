// passes/mid/InductionVarSimplification.cpp — Induction-variable
// strength reduction: rewrite `primary_iv * K` computed inside a loop
// into a DERIVED induction variable.
//
// THE REWRITE (the classic optimization this pass is named for):
//   for (i = i0; i < hi; i += step) { ... i * K ... }
// becomes
//   j = i0*K;                     // derived IV entry value
//   for (i = i0; i < hi; i += step) { ... j ... ; j += step*K; }
// — the per-iteration multiply disappears; each iteration pays one
// add instead (and on our linear-scan backend, nothing: j lives in a
// register).
//
// SOUNDNESS PROOF (why replacing every use of `i*K` — inside the loop
// AND after it — with `j` is exact):
//   Define j_k = i_k * K for the k-th iteration's header value. The
//   rewrite gives j the recurrence j_{k+1} = j_k + step*K, and
//   i_{k+1} = i_k + step, so j_{k+1} = i_k*K + step*K = i_{k+1}*K —
//   the invariant j_k = i_k*K holds at EVERY iteration boundary by
//   induction, independent of whether the multiply's arm executed
//   (the multiply is Pure: its value at iteration k is i_k*K whenever
//   read). The post-loop read follows the same boundary rule (our
//   lowering binds post-loop reads of a header phi to the next-entry
//   value), so the equality holds on the exit path too.
//
//   Requirement: K must be a Constant (so step*K folds), and the
//   multiply must be by the phi's VALUE (either operand order).
//
// Rule B.5 (idempotent): the Mul is dead after the rewrite; the new
//   phi is an AddRec, and a further Mul(phi_j, K2) would chain as a
//   new derived IV — each rewrite strictly removes one Mul, so the
//   pass converges.
// Rule B.6 (monotone): -1 Mul, +1 Phi +1 Add +1 Constant (the entry
//   value folds when i0 is constant) — the removed Mul is the
//   expensive node this pass exists to eliminate; growth is bounded
//   by kIVSMaxInductionVarsPerLoop rewrites per run.
// Rule 61: thresholds from PassConstants.hpp.
// Rule 65: telemetry when candidates exist but are skipped.
// Rule 73: Node references re-fetched by id after every make_* (the
//   node vector may reallocate).
#include "aegis/passes/mid/InductionVarSimplification.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::mid {

namespace {
// Is this node a live Constant (usable as the multiply factor K)?
bool live_constant(const Graph& g, NodeId id) noexcept {
    return id != kInvalidNodeId && id < g.size() &&
           !g[id].flags.has(NodeFlagBit::IsDead) &&
           g[id].kind == NodeKind::Constant;
}
} // namespace

int InductionVarSimplificationPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis scev(g);
    scev.run();

    int rewritten = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::Loop) continue;
        const NodeId loop = id;

        // Find this loop's induction phis (SCEV AddRec) and, for each,
        // the Muls by a Constant that consume them.
        for (NodeId phi = 0; phi < g.size(); ++phi) {
            if (g[phi].flags.has(NodeFlagBit::IsDead)) continue;
            if (g[phi].kind != NodeKind::Phi) continue;
            if (g[phi].inputs.empty() || g[phi].inputs[0] != loop) continue;
            SCEVExpr expr = scev.scev_of(phi);
            if (expr.kind != SCEVKind::AddRec) continue;
            if (expr.step == 0) continue;

            if (static_cast<uint32_t>(rewritten) >=
                constants::kIVSMaxInductionVarsPerLoop) {
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::PassBudgetExceeded,
                    "pass=ivs reason=max_induction_vars_per_run");
                return rewritten;
            }

            // Scan the phi's users for `phi * K` / `K * phi`.
            for (NodeId mul : g.users_snapshot(phi)) {
                if (mul >= g.size()) continue;
                if (g[mul].flags.has(NodeFlagBit::IsDead)) continue;
                if (g[mul].kind != NodeKind::Mul) continue;
                auto d = g[mul].data_ins();
                if (d.size() != 2) continue;
                NodeId k_id = kInvalidNodeId;
                if (d[0] == phi && live_constant(g, d[1])) k_id = d[1];
                else if (d[1] == phi && live_constant(g, d[0])) k_id = d[0];
                else continue;
                // Skip multi-edge products (phi * phi) — k_id selection
                // above already requires the OTHER operand constant.
                const int64_t factor = g[k_id].payload.i64;

                // Dead multiply (no live users)? Nothing to gain.
                bool has_live_user = false;
                for (NodeId u : g.users_snapshot(mul)) {
                    if (u < g.size() && !g[u].flags.has(NodeFlagBit::IsDead)) {
                        has_live_user = true;
                        break;
                    }
                }
                if (!has_live_user) continue;

                // ---- Build the derived IV. ----
                // Rule 73: capture everything by value BEFORE make_*.
                const NodeId entry_val = g[phi].inputs[1];
                const TypeId ty = g[phi].type_id;
                const int64_t step_times_k = expr.step * factor;

                NodeId j_entry;
                if (live_constant(g, entry_val)) {
                    // Fold i0*K at compile time.
                    j_entry = g.make_constant_i64(
                        g[entry_val].payload.i64 * factor, ty);
                } else {
                    // j0 = i0 * K (Pure node; position-free).
                    j_entry = g.make_binop(NodeKind::Mul, entry_val, k_id, ty);
                }
                NodeId j_phi = g.make_phi(loop, {j_entry, kInvalidNodeId}, ty);
                NodeId j_step = g.make_constant_i64(step_times_k, ty);
                NodeId j_back = g.make_binop(NodeKind::Add, j_phi, j_step, ty);
                g.set_input(j_phi, 2, j_back);

                // Replace every use of the Mul with the derived phi.
                for (NodeId u : g.users_snapshot(mul)) {
                    g.swap_input(u, mul, j_phi);
                }
                g[mul].flags.set(NodeFlagBit::IsDead);
                ++rewritten;
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::PassOptimized,
                    "pass=ivs mul=n" + std::to_string(mul) +
                    " derived_phi=n" + std::to_string(j_phi));
            }
        }
    }
    (void)budget;
    return rewritten;
}

} // namespace aegis::passes::mid
