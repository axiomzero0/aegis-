// passes/research/ValueFlowAnalysis.cpp — Disjoint-region proof via allocation sites.
//
// Law: VFA tracks how values flow from allocation sites through the
// IR. Two pointers don't alias if their allocation sites are disjoint
// AND no operation mixes their flows.
//
// For the prototype we tag every Alloc with a unique allocation-site
// id (stored in the node's payload). Downstream passes (Speculative
// Effect Reordering) can use this id to quickly check disjointness.
#include "aegis/passes/research/ValueFlowAnalysis.hpp"

#include "aegis/ir/NodeKind.hpp"

namespace aegis::passes::research {

int ValueFlowAnalysisPass::run(Graph& g, const PassBudget& budget) {
    int tagged = 0;
    uint32_t next_alloc_site_id = 1; // 0 = "no allocation site"
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Alloc) continue;
        // Stamp the Alloc with a unique allocation-site id.
        n.payload.u64 = next_alloc_site_id++;
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::research
