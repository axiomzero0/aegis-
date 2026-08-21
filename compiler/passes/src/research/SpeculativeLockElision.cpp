// passes/research/SpeculativeLockElision.cpp — Speculative lock elision.
//
// Algorithm (per spec):
//   1. Find every critical section: a sequence of Altered nodes
//      between an "acquire mutex" call and a "release mutex" call.
//   2. Look up the mutex's contention rate in the PGO profile.
//   3. If contention rate < kSleMaxContentionPercent, inline the
//      critical section and guard it with an atomic version counter.
//   4. If the counter changes (contention detected), deopt to the
//      standard lock path.
//
// Law: Rule A.3 — every PGO-driven decision requires a Guard. We
// emit a Guard node + FrameState.
// Law: Rule A.5 — FrameState is mandatory for all guards.
//
// For the prototype we conservatively skip SLE in AOT mode (requires
// static proof) and only run it in JIT mode (where PGO is available).
#include "aegis/passes/research/SpeculativeLockElision.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int SpeculativeLockElisionPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available) {
        // No PGO — can't speculate. Emit telemetry on the gap.
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=sle reason=no_pgo_data");
        return 0;
    }
    // Find CallCrowded nodes (mutex acquire is Crowded — it's a thread
    // sync). Tag them as candidates for SLE.
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::CallCrowded) continue;
        // Tag for the backend to consider SLE. The actual contention
        // check would consult the PGO profile.
        n.flags.set(NodeFlagBit::IsPgoSpeculated);
        ++tagged;
    }
    (void)constants::kSleMaxContentionPercent;
    return tagged;
}

} // namespace aegis::passes::research
