// passes/research/MemPoolSynthesis.cpp — Replace malloc/free in loops with pools.
//
// Algorithm:
//   1. Find every Loop in the graph.
//   2. For each Loop, count the number of CallAltered nodes whose
//      callee is "__malloc" or "__free" (we don't have the callee
//      SymbolIds at this stage — we conservatively count all
//      CallAltered nodes as potential malloc/free).
//   3. If the count >= kMemPoolSynthMinMallocPairs, tag the loop
//      for the backend to synthesize a pool allocator.
//
// Law: Rule 61 — kMemPoolSynthMinMallocPairs is the threshold.
// Law: Rule 65 — telemetry when loops are tagged.
#include "aegis/passes/research/MemPoolSynthesis.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis::passes::research {

int MemPoolSynthesisPass::run(Graph& g, const PassBudget& budget) {
    int tagged = 0;
    for (NodeId loop_id = 0; loop_id < g.size(); ++loop_id) {
        const Node& lp = g[loop_id];
        if (lp.flags.has(NodeFlagBit::IsDead)) continue;
        if (lp.kind != NodeKind::Loop) continue;
        // Count CallAltered nodes whose ctrl_in traces back to the loop.
        uint32_t call_count = 0;
        for (NodeId id = 0; id < g.size(); ++id) {
            const Node& n = g[id];
            if (n.flags.has(NodeFlagBit::IsDead)) continue;
            if (n.kind != NodeKind::CallAltered) continue;
            // Walk ctrl_in chain.
            NodeId cur = n.ctrl_in();
            while (cur != kInvalidNodeId && cur < g.size()) {
                if (cur == loop_id) {
                    ++call_count;
                    break;
                }
                if (g[cur].kind == NodeKind::Start) break;
                cur = g[cur].ctrl_in();
            }
        }
        if (call_count >= constants::kMemPoolSynthMinMallocPairs) {
            g[loop_id].flags.set(NodeFlagBit::IsLowered);
            ++tagged;
        }
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::research
