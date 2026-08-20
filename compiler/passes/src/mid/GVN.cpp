// passes/GVN.cpp — re-hash-cons all Pure nodes, replace duplicates.
#include "aegis/passes/mid/GVN.hpp"

#include "aegis/support/SwissTable.hpp"
#include "aegis/ir/HashConsing.hpp"
#include "aegis/ir/NodeKind.hpp"

namespace aegis {

namespace {
struct GvnKey {
    uint16_t kind;
    uint16_t _pad;
    uint32_t type_id;
    uint64_t payload;
    uint64_t inputs_hash;
    bool operator==(const GvnKey&) const noexcept = default;
};
struct GvnKeyHash {
    size_t operator()(const GvnKey& k) const noexcept {
        uint64_t h = 0xcbf29ce484222325ULL;
        h ^= k.kind;       h *= 0x100000001b3ULL;
        h ^= k.type_id;    h *= 0x100000001b3ULL;
        h ^= k.payload;    h *= 0x100000001b3ULL;
        h ^= k.inputs_hash; h *= 0x100000001b3ULL;
        return static_cast<size_t>(h);
    }
};

uint64_t hash_data_ins(const Node& n) noexcept {
    // Hash only the *data* inputs (skip control + effect at slots 0/1).
    uint64_t h = 0xcbf29ce484222325ULL;
    auto data = n.data_ins();
    for (NodeId i : data) {
        h ^= static_cast<uint64_t>(i);
        h *= 0x100000001b3ULL;
    }
    return h;
}
} // namespace

int GVNPass::run(Graph& g, const PassBudget& budget) {
    SwissTable<GvnKey, NodeId, GvnKeyHash> table;
    table.reserve(g.size());
    int replaced = 0;

    // For replacement: we build a NodeId -> NodeId "rewrite" map so
    // downstream nodes get their inputs rewired. After the pass, the
    // old node ids are marked Dead.
    std::vector<NodeId> rewrite_map(g.size(), kInvalidNodeId);

    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        // GVN only applies to Pure nodes (no control/effect dependence).
        if (!n.is_pure()) continue;
        // Skip nodes that have no structural identity (e.g. Parameter).
        if (n.kind == NodeKind::Parameter) continue;
        if (n.kind == NodeKind::Constant) {
            // Constants: dedup by payload + type. Hash-consing keys on
            // (kind, type, payload) with no inputs.
        }

        GvnKey key{
            .kind        = static_cast<uint16_t>(n.kind),
            ._pad        = 0,
            .type_id     = n.type_id,
            .payload     = n.payload.u64,
            .inputs_hash = hash_data_ins(n),
        };

        if (NodeId* existing = table.get(key); existing != nullptr) {
            // Redundant node — rewrite all its uses to point to *existing.
            rewrite_map[id] = *existing;
            ++replaced;
        } else {
            table.insert(key, id);
        }
    }

    if (replaced == 0) return 0;

    // Apply the rewrite: for each node, replace any input that points
    // at a rewritten id with its target.
    for (NodeId id = 0; id < g.size(); ++id) {
        Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            NodeId in = n.inputs[i];
            if (in == kInvalidNodeId) continue;
            if (in < rewrite_map.size() && rewrite_map[in] != kInvalidNodeId) {
                g.set_input(id, i, rewrite_map[in]);
            }
        }
    }

    // Mark the rewritten nodes dead.
    for (NodeId id = 0; id < g.size(); ++id) {
        if (rewrite_map[id] != kInvalidNodeId) {
            g.mark_dead(id);
        }
    }

    (void)budget;
    return replaced;
}

} // namespace aegis
