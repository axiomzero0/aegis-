// passes/EDCE.cpp — Mark-and-sweep from Crowded / Return roots.
#include "aegis/passes/mid/EDCE.hpp"

#include "aegis/support/BitVector.hpp"
#include "aegis/ir/NodeKind.hpp"

#include <vector>

namespace aegis {

int EDCEPass::run(Graph& g, const PassBudget& budget) {
    uint32_t n = g.size();
    BitVector live(n);

    // Step 1: mark roots.
    //   - Crowded nodes are always live.
    //   - Return / Stop nodes are roots.
    //   - Altered nodes are live only if they're reachable from a Crowded
    //     or Return root through the effect chain. We approximate this
    //     by walking effect-chain edges: if an Altered node's eff_in
    //     feeds into a live Crowded/Return, it's live.
    std::vector<NodeId> worklist;
    worklist.reserve(n);
    for (NodeId id = 0; id < n; ++id) {
        const Node& nd = g[id];
        if (nd.flags.has(NodeFlagBit::IsDead)) continue;
        if (nd.is_crowded() || nd.kind == NodeKind::Return || nd.kind == NodeKind::Stop) {
            live.set(id);
            worklist.push_back(id);
        }
    }

    // Step 2: backward reachability over inputs.
    while (!worklist.empty()) {
        NodeId id = worklist.back();
        worklist.pop_back();
        const Node& nd = g[id];
        for (NodeId in : nd.inputs) {
            if (in == kInvalidNodeId) continue;
            if (in >= n) continue;
            if (g[in].flags.has(NodeFlagBit::IsDead)) continue;
            if (!live.test(in)) {
                live.set(in);
                worklist.push_back(in);
            }
        }
    }

    // Step 3: sweep.
    int removed = 0;
    for (NodeId id = 0; id < n; ++id) {
        if (id == kStartNodeId) continue; // Start is always live.
        Node& nd = g[id];
        if (nd.flags.has(NodeFlagBit::IsDead)) continue;
        if (live.test(id)) {
            nd.flags.set(NodeFlagBit::IsLive);
        } else {
            nd.flags.set(NodeFlagBit::IsDead);
            ++removed;
        }
    }

    (void)budget;
    return removed;
}

} // namespace aegis
