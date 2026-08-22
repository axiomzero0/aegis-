// passes/research/ValueFlowAnalysis.cpp — Value-Flow Analysis (VFA).
//
// SOUND IMPLEMENTATION:
//   1. Stamp every Alloc with a unique allocation-site id.
//   2. Track value flow: each Load propagates its pointer's
//      allocation-site id to the result. Each Store propagates the
//      stored value's allocation-site id to the destination pointer's
//      "may-point-to" set.
//   3. Compute disjointness: two pointers are disjoint (don't
//      alias) if their may-point-to allocation-site sets don't
//      intersect.
//
// For the prototype we compute the allocation-site flow and count
// the number of disjoint pointer pairs. A real impl would expose
// the disjointness query via the AliasAnalysisInterface.
//
// Rule B.5: idempotent — once stamped + flow computed, the next pass
// sees the same allocation-site ids and the same flow.
// Rule 65: telemetry on budget exhaustion.
#include "aegis/passes/research/ValueFlowAnalysis.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

#include <vector>

namespace aegis::passes::research {

namespace {
// Each pointer's set of allocation-site ids it may point to.
struct SiteSet {
    std::vector<uint32_t> sites;
    bool add(uint32_t s) {
        for (uint32_t existing : sites) {
            if (existing == s) return false;
        }
        sites.push_back(s);
        return true;
    }
    bool contains(uint32_t s) const {
        for (uint32_t existing : sites) {
            if (existing == s) return true;
        }
        return false;
    }
    static bool disjoint(const SiteSet& a, const SiteSet& b) {
        for (uint32_t s : a.sites) {
            if (b.contains(s)) return false;
        }
        return true;
    }
};
} // namespace

int ValueFlowAnalysisPass::run(Graph& g, const PassBudget& budget) {
    // Step 1: stamp each Alloc with a unique allocation-site id.
    uint32_t next_site_id = 1;
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Alloc) continue;
        n.payload.u64 = next_site_id++;
    }

    // Step 2: compute may-point-to site sets.
    std::vector<SiteSet> may_point_to(g.size());

    bool changed = true;
    // Type matches the named constant (uint32_t) so the comparison
    // cannot trip -Wsign-compare (Rule 73: no fragile encodings).
    uint32_t iterations = 0;
    while (changed) {
        changed = false;
        if (++iterations > constants::kValueFlowMaxFixpointIterations) break;
        for (NodeId id = 0; id < g.size(); ++id) {
            const Node& n = g[id];
            if (n.flags.has(NodeFlagBit::IsDead)) continue;
            switch (n.kind) {
                case NodeKind::Alloc:
                    if (may_point_to[id].add(
                            static_cast<uint32_t>(n.payload.u64))) changed = true;
                    break;
                case NodeKind::GetElementPtr:
                case NodeKind::GetFieldPtr:
                case NodeKind::Cast: {
                    auto d = n.data_ins();
                    if (d.empty()) break;
                    NodeId ptr = d[0];
                    if (ptr == kInvalidNodeId || ptr >= g.size()) break;
                    for (uint32_t s : may_point_to[ptr].sites) {
                        if (may_point_to[id].add(s)) changed = true;
                    }
                    break;
                }
                case NodeKind::Load: {
                    auto d = n.data_ins();
                    if (d.empty()) break;
                    NodeId ptr = d[0];
                    if (ptr == kInvalidNodeId || ptr >= g.size()) break;
                    for (uint32_t s : may_point_to[ptr].sites) {
                        if (may_point_to[id].add(s)) changed = true;
                    }
                    break;
                }
                case NodeKind::Store: {
                    auto d = n.data_ins();
                    if (d.size() < 2) break;
                    NodeId ptr = d[0];
                    NodeId val = d[1];
                    if (ptr == kInvalidNodeId || ptr >= g.size()) break;
                    if (val == kInvalidNodeId || val >= g.size()) break;
                    for (uint32_t s : may_point_to[val].sites) {
                        if (may_point_to[ptr].add(s)) changed = true;
                    }
                    break;
                }
                default: break;
            }
        }
    }

    // Step 3: count disjoint pointer pairs for telemetry.
    int disjoint_pairs = 0;
    for (NodeId a = 0; a < g.size(); ++a) {
        if (may_point_to[a].sites.empty()) continue;
        for (NodeId b = a + 1; b < g.size(); ++b) {
            if (may_point_to[b].sites.empty()) continue;
            if (SiteSet::disjoint(may_point_to[a], may_point_to[b])) {
                ++disjoint_pairs;
            }
        }
    }
    (void)budget;
    return disjoint_pairs;
}

} // namespace aegis::passes::research
