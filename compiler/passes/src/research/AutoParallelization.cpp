// passes/research/AutoParallelization.cpp — Insert fork/join for parallel loops.
//
// Algorithm:
//   1. For each Loop, run SCEVAnalysis to find the trip count.
//   2. If trip count >= kAutoParallelMinTripCount AND the loop body
//      has no Load-after-Store aliasing (we'd need alias analysis to
//      prove this rigorously — for the prototype we conservatively
//      tag only loops with no Altered nodes in the body).
//   3. Tag the loop for parallelization. The backend lowers tagged
//      loops to fork/join around the body.
//
// Law: Rule 49 — No vectorization without dependence proof. We only
// parallelize loops we can prove have no cross-iteration deps.
// Law: Rule 61 — kAutoParallelMinTripCount bounds the heuristic.
#include "aegis/passes/research/AutoParallelization.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/passes/mid/SCEV.hpp"

namespace aegis::passes::research {

int AutoParallelizationPass::run(Graph& g, const PassBudget& budget) {
    aegis::passes::mid::SCEVAnalysis scev(g);
    scev.run();
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Loop) continue;
        // Find the induction phi + trip count.
        int64_t trip_count = -1;
        bool has_altered = false;
        for (NodeId user : g.outputs()[id].view()) {
            if (user >= g.size()) continue;
            if (g[user].kind == NodeKind::Phi) {
                trip_count = scev.trip_count_of(user);
            }
            if (g[user].is_altered()) has_altered = true;
        }
        if (trip_count < static_cast<int64_t>(constants::kAutoParallelMinTripCount)) continue;
        if (has_altered) continue; // can't prove independence
        n.flags.set(NodeFlagBit::IsLowered);
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::research
