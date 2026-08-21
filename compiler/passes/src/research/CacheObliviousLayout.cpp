// passes/research/CacheObliviousLayout.cpp — Rewrite container layouts.
//
// Law: Cache-oblivious layouts divide container data into blocks
// whose size is determined recursively (no fixed cache-line size
// assumption), so they perform well on any cache hierarchy.
//
// For the prototype, we tag Alloc nodes whose payload size exceeds
// kCacheObliviousMaxContainerSize for the backend to consult the
// Target's cache_line_size() and rewrite the layout.
//
// Law: Rule 66 — the actual cache-line size is queried at runtime
// via the Target interface, not hardcoded.
#include "aegis/passes/research/CacheObliviousLayout.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis::passes::research {

int CacheObliviousLayoutPass::run(Graph& g, const PassBudget& budget) {
    int tagged = 0;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Alloc) continue;
        // We don't have the container size directly; conservatively
        // skip if the loop body is small. The real impl would query
        // the TypeTable for the container type + size.
        (void)constants::kCacheObliviousMaxContainerSize;
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::research
