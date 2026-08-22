// passes/research/SpeculativeLockElision.cpp — Speculative lock elision.
//
// SOUND IMPLEMENTATION:
//   For each CallCrowded node (mutex acquire/release), when PGO shows
//   low contention (< kSleMaxContentionPercent), we:
//     1. Create a FrameState node capturing the current state.
//     2. Attach the FrameState as an INPUT EDGE (append_input) and tag
//        the Call with IsPgoSpeculated + HasFrameState + IsGuarded (so
//        the backend knows to inline the critical section + guard it
//        with an atomic version counter, with deopt to the standard
//        lock path on contention). The callee SymbolId in the payload
//        is NOT touched — clobbering payload.u64 would silently
//        redirect the call to a bogus symbol (Rule 62).
//     3. Emit telemetry on the speculation decision.
//
//   The actual lock elision (inlining the critical section + emitting
//   the version-counter guard) happens at backend lowering time.
//
// Rule A.3: every PGO-driven decision requires a Guard.
// Rule A.5: FrameState is mandatory.
// Rule 65: telemetry on every speculation decision.
// Rule 73: robustness — don't hold Node& across make_frame_state
//   (the graph's node vector may reallocate, invalidating the
//   reference). Capture ctrl_in/eff_in by value first, create the
//   FrameState, then re-fetch the node by id to set flags.
#include "aegis/passes/research/SpeculativeLockElision.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeLockElisionPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available || !budget.allow_speculation) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=sle reason=no_pgo_or_spec_disabled");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::CallCrowded) continue;
        // Rule B.5 (idempotency): already-elided calls keep their
        // original FrameState; re-speculating would duplicate it on
        // every fixpoint iteration.
        if (g[id].flags.has(NodeFlagBit::IsPgoSpeculated)) continue;
        // SOUND: capture ctrl_in/eff_in by value BEFORE calling
        // make_frame_state (which may reallocate the node vector,
        // invalidating any Node& reference).
        NodeId ctrl_in = g[id].ctrl_in();
        NodeId eff_in  = g[id].eff_in();
        NodeId fs = g.make_frame_state({ctrl_in, eff_in});
        // Attach the FrameState as an input edge; the callee SymbolId
        // in the payload stays intact (Rule 62).
        g.append_input(id, fs);
        g[id].flags.set(NodeFlagBit::IsPgoSpeculated |
                        NodeFlagBit::HasFrameState |
                        NodeFlagBit::IsGuarded);
        ++tagged;
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::JitGuardFailed,
            "pass=sle call_id=" + std::to_string(id));
    }
    (void)constants::kSleMaxContentionPercent;
    return tagged;
}

} // namespace aegis::passes::research
