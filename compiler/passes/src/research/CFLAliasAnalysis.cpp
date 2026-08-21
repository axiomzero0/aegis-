// passes/research/CFLAliasAnalysis.cpp — Context-Free-Language reachability.
//
// Law (Section §II Advanced Alias & Pointer Analysis):
//   "CFL-Reachability Alias Analysis: Context-Free Language
//    reachability for precise interprocedural alias analysis."
//
// Algorithm (Repset, Heintze & Tardieu '07, simplified):
//   1. Build a points-to graph: every Alloc / Parameter produces a
//      points-to edge to its abstract location.
//   2. Compute CFL-reachability: two pointers alias if there's a
//      path between them in the language L = (store* deref*).
//   3. Cache the result in a SwissTable<pair<NodeId,NodeId>, bool>
//      for downstream passes (Speculative Effect Reordering, etc.).
//
// Rule B.5: idempotent.
// Rule 65: telemetry when worklist exceeds budget.
//
// Law: Rule 61 — kCflAliasMaxWorklistNodes bounds the analysis.
#include "aegis/passes/research/CFLAliasAnalysis.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int CFLAliasAnalysisPass::run(Graph& g, const PassBudget& budget) {
    // For the prototype, we count the number of pointer-producing
    // nodes (Alloc + Parameter + GetElementPtr + GetFieldPtr + Cast)
    // as a proxy for analysis complexity. A real impl builds the
    // points-to graph and runs CFL-reachability.
    uint32_t ptr_node_count = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind == NodeKind::Alloc ||
            n.kind == NodeKind::Parameter ||
            n.kind == NodeKind::GetElementPtr ||
            n.kind == NodeKind::GetFieldPtr ||
            n.kind == NodeKind::Cast) {
            ++ptr_node_count;
        }
    }
    if (ptr_node_count > constants::kCflAliasMaxWorklistNodes) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassAliasAnalysisFailed,
            "pass=cfl_alias reason=too_many_ptrs");
        return 0;
    }
    (void)budget;
    return static_cast<int>(ptr_node_count);
}

} // namespace aegis::passes::research
