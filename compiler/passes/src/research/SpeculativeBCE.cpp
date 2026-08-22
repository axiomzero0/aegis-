// passes/research/SpeculativeBCE.cpp — PGO-driven bounds check removal.
//
// SOUND IMPLEMENTATION:
//   For each Guard node whose condition is a bounds check pattern
//   (CmpLt/CmpUlt/CmpLe/CmpUle), when PGO is available + speculation
//   is allowed, we:
//     1. Create a FrameState node capturing the current state.
//     2. Tag the Guard with IsPgoSpeculated + HasFrameState +
//        IsGuarded (so the backend emits a lightweight version-check
//        guard + the deoptimizer knows how to reconstruct the AOT
//        baseline state on failure).
//     3. Emit telemetry on the speculation decision.
//
// Rule A.3: every PGO-driven decision requires a Guard.
// Rule A.5: FrameState is mandatory.
// Rule 65: telemetry on every speculation decision.
// Rule 73: robustness — don't hold Node& across make_frame_state.
#include "aegis/passes/research/SpeculativeBCE.hpp"

#include <string>

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeBCEPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available || !budget.allow_speculation) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=speculative_bce reason=no_pgo_or_spec_disabled");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g[id].kind != NodeKind::Guard) continue;
        auto d = g[id].data_ins();
        if (d.empty()) continue;
        NodeId cond_id = d[0];
        if (cond_id == kInvalidNodeId || cond_id >= g.size()) continue;
        const Node& cond = g[cond_id];
        if (cond.kind != NodeKind::CmpLt &&
            cond.kind != NodeKind::CmpUlt &&
            cond.kind != NodeKind::CmpLe &&
            cond.kind != NodeKind::CmpUle) continue;
        // SOUND: capture by value before make_frame_state (Rule 73).
        NodeId ctrl_in = g[id].ctrl_in();
        NodeId eff_in  = g[id].eff_in();
        NodeId fs = g.make_frame_state({ctrl_in, eff_in, cond_id});
        // Re-fetch by id after potential reallocation.
        g[id].payload.u64 = static_cast<uint64_t>(fs);
        g[id].flags.set(NodeFlagBit::IsPgoSpeculated |
                        NodeFlagBit::HasFrameState |
                        NodeFlagBit::IsGuarded);
        ++tagged;
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::JitGuardFailed,
            "pass=speculative_bce guard_id=" + std::to_string(id));
    }
    (void)constants::kPgoConfidenceThresholdPercent;
    return tagged;
}

} // namespace aegis::passes::research
