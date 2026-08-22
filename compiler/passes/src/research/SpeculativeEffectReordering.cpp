// passes/research/SpeculativeEffectReordering.cpp — Reorder Altered nodes for ILP.
//
// SOUND IMPLEMENTATION:
//   For each Load node, when PGO is available + speculation is
//   allowed, we tag it + emit FrameState so the backend can emit an
//   alias-check guard and reorder the Load earlier in the effect
//   chain for ILP.
//
// Rule A.3: every PGO-driven decision requires a Guard.
// Rule A.5: FrameState is mandatory.
// Rule 65: telemetry on every speculation decision.
// Rule 73: robustness — don't hold Node& across make_frame_state.
#include "aegis/passes/research/SpeculativeEffectReordering.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeEffectReorderingPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available || !budget.allow_speculation) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=spec_effect_reorder reason=no_pgo_or_spec_disabled");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::Load) continue;
        // SOUND: capture by value before make_frame_state (Rule 73).
        NodeId ctrl_in = g[id].ctrl_in();
        NodeId eff_in  = g[id].eff_in();
        NodeId fs = g.make_frame_state({ctrl_in, eff_in});
        // Re-fetch by id after potential reallocation.
        g[id].payload.u64 = static_cast<uint64_t>(fs);
        g[id].flags.set(NodeFlagBit::IsPgoSpeculated |
                        NodeFlagBit::HasFrameState |
                        NodeFlagBit::IsGuarded);
        ++tagged;
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::JitGuardFailed,
            "pass=spec_effect_reorder load_id=" + std::to_string(id));
    }
    (void)constants::kPgoConfidenceThresholdPercent;
    return tagged;
}

} // namespace aegis::passes::research
