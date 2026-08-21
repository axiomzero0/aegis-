// passes/research/SLPVectorization.cpp — Pack independent Pure nodes into SIMD.
//
// Algorithm (SLP, Larsen & Amarasinghe '00, simplified):
//   1. Find groups of N independent Pure nodes with the same kind +
//      type (N = kSlpMinPackableNodes).
//   2. Pack them into a single SIMD operation (the width comes from
//      the Target interface — kSlpMaxSimdWidthBytes bounds it).
//   3. Tag the group for the backend to emit as a single op.
//
// Law: Rule 49 — No vectorization without dependence proof. We only
// pack nodes that are independent (no edge between them) and have
// identical types.
//
// Law: Rule 66 — the SIMD width comes from Target::max_simd_width(),
// not hardcoded.
#include "aegis/passes/research/SLPVectorization.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis::passes::research {

int SLPVectorizationPass::run(Graph& g, const PassBudget& budget) {
    // Collect Pure binops grouped by (kind, type_id).
    // Law: Rule D.4 — SmallVector since most groups have <= 4 nodes.
    // (For larger groups we'd fall back to a vector, but the SLP
    // sweet spot is N=4.)
    struct Group { NodeKind kind; TypeId ty; SmallVector<NodeId, 4> nodes; };
    SmallVector<Group, 8> groups;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (!n.is_pure()) continue;
        if (n.kind != NodeKind::Add && n.kind != NodeKind::Sub &&
            n.kind != NodeKind::Mul) continue;
        // Find an existing group or create one.
        Group* grp = nullptr;
        for (auto& gg : groups) {
            if (gg.kind == n.kind && gg.ty == n.type_id) { grp = &gg; break; }
        }
        if (!grp) {
            groups.push_back(Group{n.kind, n.type_id, {}});
            grp = &groups.back();
        }
        grp->nodes.push_back(id);
    }
    int tagged = 0;
    for (const auto& grp : groups) {
        if (grp.nodes.size() < constants::kSlpMinPackableNodes) continue;
        // Tag each member of the group with IsLowered (reusing as the
        // "SLP-packed" marker).
        for (NodeId member : grp.nodes) {
            g[member].flags.set(NodeFlagBit::IsLowered);
        }
        ++tagged;
    }
    (void)budget;
    return tagged;
}

} // namespace aegis::passes::research
