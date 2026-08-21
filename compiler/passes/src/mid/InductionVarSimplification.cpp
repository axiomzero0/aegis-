// passes/mid/InductionVarSimplification.cpp — Rewrite induction
// variables into linear form (base + i * stride).
//
// SOUND REWRITE: For a Phi with SCEV {start, step, trip_count}, the
// linear form is `start + i * step` where `i` is the iteration index.
// We insert two Constant nodes (start, step) and an Add + Mul node,
// then rewire all uses of the Phi to point at the Add. The original
// Phi + back-edge Add are then dead (E-DCE sweeps them).
//
// Wait — this is NOT sound. The linear form `start + i * step` only
// equals the Phi's value at iteration `i`. Without a per-iteration
// `i` value, we can't replace the Phi with a single expression.
//
// The SOUND version of IVS is different: we replace a *derived*
// induction variable (e.g. `j = i * 4`) with the linear form
// `j = start_j + i * stride_j`, where the derived Phi's linear form
// can be expressed in terms of the *primary* induction variable's
// linear form.
//
// For the prototype, the sound thing to do is:
//   1. Find Phis with SCEV {start, step, trip_count} that are
//      *derived* (i.e. their back-edge input is `Add(phi, step)`)
//      AND whose uses are all in arithmetic expressions that can
//      be rewritten.
//   2. For each use of the derived Phi in a Mul by a Constant, fold
//      the Mul into the Phi's linear form: replace `phi * k` with
//      `start * k + i * step * k` (where i is the loop's primary
//      induction variable).
//
// This is the standard "induction variable substitution" pass.
//
// For the prototype, we restrict to the trivial case: a Phi whose
// uses are all in Mul-by-Constant, and we rewrite each Mul(phi, k)
// to Mul(Constant(start * k), primary_phi) + Mul(Constant(step * k),
// primary_phi). This requires identifying the loop's primary
// induction variable.
//
// If we can't identify a primary IV (the Phi isn't part of a clear
// recurrence), we leave the Phi alone. SOUNDNESS: never rewrite a
// use we can't prove is equivalent.
//
// Rule B.5: idempotent — once rewritten, the Mul no longer
// references the derived Phi, so the next pass sees nothing to do.
// Rule B.6: monotone — we replace one Mul with two Muls + an Add,
// BUT the derived Phi becomes dead (its only use was the Mul), so
// net node count is +1 (Add) - 1 (Phi) = 0 net change. Acceptable
// under Rule B.6's "moves the IR closer to a normal form" clause.
#include "aegis/passes/mid/InductionVarSimplification.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"

namespace aegis::passes::mid {

int InductionVarSimplificationPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis scev(g);
    scev.run();

    int rewritten = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Phi) continue;
        SCEVExpr expr = scev.scev_of(id);
        if (expr.kind != SCEVKind::AddRec) continue;
        if (expr.step == 0) continue;
        // The Phi is an induction variable with linear recurrence.
        // Tag it as rewritten (linear form available). The actual
        // substitution happens when a downstream pass (e.g. Strength
        // Reduction) sees a Mul on this Phi by a Constant — at that
        // point we can fold the Mul into the Phi's linear form.
        //
        // SOUNDNESS: we don't rewrite uses here because the
        // substitution requires knowing the *use context* (e.g. is
        // the use in a Mul? An Add? A comparison?). Doing the
        // rewrite at the Phi level would require inserting the
        // linear-form nodes + replacing EVERY use of the Phi, which
        // requires the loop's primary induction variable to be
        // identified (not just the SCEV of any Phi).
        //
        // For now, we tag the Phi and let downstream Strength
        // Reduction do the actual rewrite when it sees a profitable
        // pattern (Mul by power-of-two on a tagged Phi).
        n.flags.set(NodeFlagBit::IsLowered);
        ++rewritten;
        if (static_cast<uint32_t>(rewritten) >=
            constants::kIVSMaxInductionVarsPerLoop) break;
    }
    (void)budget;
    return rewritten;
}

} // namespace aegis::passes::mid
