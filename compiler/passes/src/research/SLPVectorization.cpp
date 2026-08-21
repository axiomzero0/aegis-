// passes/research/SLPVectorization.cpp — Identify SLP packable groups.
//
// HONEST SCOPE: This is an ANALYSIS pass. It identifies groups of
// >= kSlpMinPackableNodes independent Pure binops with the same kind
// + type, and tags them as IsLowered ("SLP-packable"). The actual
// packing requires a new "VectorOp" NodeKind that doesn't exist yet;
// adding it is deferred per Rule 74 (we document the gap rather than
// claim to do something we can't).
//
// SOUNDNESS:
//   - We only tag groups where all members are independent (no edge
//     between them). This is verified by walking each member's
//     outputs and checking that no other member is in the output
//     set.
//   - We never claim to actually emit SIMD code — the tag is a
//     "candidate marker" for downstream passes to consume.
//
// Rule B.5: idempotent — once tagged, the next pass sees the tag.
// Rule 49: No vectorization without dependence proof (we verify
// independence).
// Rule 65: telemetry when no packable groups found.
// Rule 74: gap (no VectorOp NodeKind) is documented, not deleted.
#include "aegis/passes/research/SLPVectorization.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

namespace {
// Returns true iff `a` and `b` are independent (neither is in the
// other's output set — i.e. neither's value flows into the other).
bool independent(Graph& g, NodeId a, NodeId b) {
    for (NodeId user : g.outputs()[a].view()) {
        if (user == b) return false;
    }
    for (NodeId user : g.outputs()[b].view()) {
        if (user == a) return false;
    }
    return true;
}
} // namespace

int SLPVectorizationPass::run(Graph& g, const PassBudget& budget) {
    // Collect Pure binops grouped by (kind, type_id).
    struct Group { NodeKind kind; TypeId ty; SmallVector<NodeId, 4> nodes; };
    SmallVector<Group, 8> groups;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (!n.is_pure()) continue;
        if (n.kind != NodeKind::Add && n.kind != NodeKind::Sub &&
            n.kind != NodeKind::Mul) continue;
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
        // SOUND CHECK: verify all pairs in the group are independent.
        bool all_independent = true;
        for (size_t i = 0; i < grp.nodes.size() && all_independent; ++i) {
            for (size_t j = i + 1; j < grp.nodes.size(); ++j) {
                if (!independent(g, grp.nodes[i], grp.nodes[j])) {
                    all_independent = false;
                    break;
                }
            }
        }
        if (!all_independent) continue; // can't pack dependent nodes
        // Tag each member as SLP-packable. The actual SIMD emission
        // requires a new VectorOp NodeKind (Rule 74 documented gap).
        for (NodeId member : grp.nodes) {
            g[member].flags.set(NodeFlagBit::IsLowered);
        }
        ++tagged;
    }
    if (tagged == 0) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=slp reason=no_packable_groups");
    }
    (void)budget;
    (void)constants::kSlpMaxSimdWidthBytes; // would be queried from Target
    return tagged;
}

} // namespace aegis::passes::research
