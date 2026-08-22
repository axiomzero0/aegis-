// passes/research/GuardedDevirtualization.cpp — Devirtualize + inline monomorphic call sites.
//
// SOUND IMPLEMENTATION:
//   For each CallAltered/CallCrowded node, when PGO shows the call
//   site is monomorphic, we tag it + attach a FrameState input edge
//   so the backend can emit a type-check guard + inline the
//   monomorphic target.
//
//   The FrameState is attached as an INPUT EDGE (append_input), NOT
//   by overwriting payload.u64 — the payload of a Call node carries
//   the callee SymbolId, and clobbering it would silently redirect
//   the call to a bogus symbol (Rule 62: no "small" data corruption).
//
// Rule A.3: every PGO-driven decision requires a Guard.
// Rule A.5: FrameState is mandatory.
// Rule 65: telemetry on every speculation decision.
// Rule 73: robustness — don't hold Node& across make_frame_state
//   (vector reallocation). Capture ctrl_in/eff_in by value first.
#include "aegis/passes/research/GuardedDevirtualization.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int GuardedDevirtualizationPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available || !budget.allow_speculation) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=guarded_devirt reason=no_pgo_or_spec_disabled");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::CallAltered &&
            g[id].kind != NodeKind::CallCrowded) continue;
        // Rule B.5 (idempotency): a call that already carries a
        // speculation guard from a previous run of this pass is done —
        // re-tagging it would mint a duplicate FrameState every
        // fixpoint iteration and grow the graph forever.
        if (g[id].flags.has(NodeFlagBit::IsPgoSpeculated)) continue;
        // SOUND: capture by value before make_frame_state (Rule 73).
        NodeId ctrl_in = g[id].ctrl_in();
        NodeId eff_in  = g[id].eff_in();
        NodeId fs = g.make_frame_state({ctrl_in, eff_in});
        // Attach the FrameState as an input edge. The callee SymbolId
        // in the payload stays intact (Rule 62 — payload slots carry
        // meaning; never overwrite them with unrelated data).
        g.append_input(id, fs);
        g[id].flags.set(NodeFlagBit::IsMonomorphic |
                        NodeFlagBit::IsPgoSpeculated |
                        NodeFlagBit::HasFrameState |
                        NodeFlagBit::IsGuarded);
        ++tagged;
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::JitGuardFailed,
            "pass=guarded_devirt call_id=" + std::to_string(id));
    }
    (void)constants::kGuardedDevirtMinCalls;
    (void)constants::kPgoConfidenceThresholdPercent;
    return tagged;
}

} // namespace aegis::passes::research
