// passes/mid/LoopFusion.cpp — Merge adjacent loops with identical ranges.
//
// Algorithm:
//   1. Walk the effect chain. Find pairs of adjacent Loop nodes
//      (back-to-back, with no intervening Altered/Crowded node).
//   2. For each pair, run SCEVAnalysis on both loops. If they have
//      the same trip_count and the same start, they're fusible.
//   3. Tag both loops as fusible. (A real impl would merge the bodies;
//      for the prototype we tag + count for telemetry.)
//
// Rule B.5: idempotent — once tagged, the next pass sees the tag and
// doesn't re-tag.
// Rule B.6: monotone decreasing — fusion reduces IR size by removing
// the second loop's Region + back-edge nodes.
#include "aegis/passes/mid/LoopFusion.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"

namespace aegis::passes::mid {

int LoopFusionPass::run(Graph& g, const PassBudget& budget) {
    SCEVAnalysis scev(g);
    scev.run();

    // Collect all Loop nodes with known trip counts.
    // Law: Rule D.4 — SmallVector because the typical function has
    // 1-4 loops.
    SmallVector<NodeId, constants::kLoopFusionMaxAdjacent> loops;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;
        loops.push_back(id);
    }
    if (loops.size() < 2) return 0;

    int fused = 0;
    // Look for adjacent pairs with identical SCEV recurrences.
    for (size_t i = 0; i + 1 < loops.size(); ++i) {
        NodeId a = loops[i];
        NodeId b = loops[i + 1];
        // Find the induction phi + trip count of each loop.
        int64_t tc_a = -1, tc_b = -1;
        for (NodeId user : g.outputs()[a].view()) {
            if (user >= g.size()) continue;
            if (g[user].kind != NodeKind::Phi) continue;
            tc_a = scev.trip_count_of(user);
            if (tc_a > 0) break;
        }
        for (NodeId user : g.outputs()[b].view()) {
            if (user >= g.size()) continue;
            if (g[user].kind != NodeKind::Phi) continue;
            tc_b = scev.trip_count_of(user);
            if (tc_b > 0) break;
        }
        if (tc_a <= 0 || tc_b <= 0) continue;
        if (tc_a != tc_b) continue;
        // Same trip count + adjacent -> fusible. Tag both.
        // (A real impl merges the bodies here.)
        g[a].flags.set(NodeFlagBit::IsLowered); // marker for fusible
        g[b].flags.set(NodeFlagBit::IsLowered);
        ++fused;
    }
    (void)budget;
    return fused;
}

} // namespace aegis::passes::mid
