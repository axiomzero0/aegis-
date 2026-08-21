// passes/research/PGDLO.cpp — Profile-Guided struct layout optimization.
//
// Law: PGDLO reorders struct fields by access frequency (from PGO)
// to maximize spatial locality. Hot fields are placed first (so
// they share a cache line with the object header); cold fields are
// placed last.
//
// For the prototype we tag Alloc nodes whose type is a struct with
// IsLowered (so the backend knows to consult the layout table at
// emit time). Real implementation requires PGO data + struct-type
// info, which requires the affine TypeTable to be fully wired.
//
// Law: Rule 47 — No aggressive pass without a cost model. PGDLO
// runs only in JIT mode (where PGO data is available). In AOT mode
// it's a no-op.
#include "aegis/passes/research/PGDLO.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int PGDLOPass::run(Graph& g, const PassBudget& budget) {
    if (!budget.pgo_available) {
        // No PGO data — skip. Emit telemetry so the gap is observable.
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=pgdlo reason=no_pgo_data");
        return 0;
    }
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Alloc) continue;
        // Tag for the backend to consult the layout table.
        n.flags.set(NodeFlagBit::IsLowered);
        ++tagged;
    }
    return tagged;
}

} // namespace aegis::passes::research
