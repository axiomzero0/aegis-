// passes/mid/DSE.cpp — Real Dead Store Elimination.
//
// Algorithm (effect-chain linear scan):
//   1. Walk the effect chain forward.
//   2. Maintain a map from pointer NodeId -> last Store node id that
//      wrote to that pointer.
//   3. When we see a new Store on a pointer that's in the map and no
//      intervening Load/Call/AtomicAccess has happened on that pointer,
//      the previous Store is dead (overwritten before read) — mark
//      it dead.
//   4. Stores to pointers that are never read (no Load on that pointer
//      anywhere in the graph) are also dead.
//
// Idempotency (Rule B.5): once we eliminate a dead store, the next
// pass sees the remaining stores and won't re-eliminate them — they
// survive because there's no longer an intervening write to make
// them dead.
#include "aegis/passes/mid/DSE.hpp"

#include "aegis/ir/NodeKind.hpp"

#include <unordered_map>
#include <unordered_set>

namespace aegis::passes::mid {

int DeadStoreElimPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;

    // Step 1: find pointers that are loaded from. A Store to a pointer
    // that's never read is dead.
    std::unordered_set<NodeId> pointers_read_from;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind != NodeKind::Load) continue;
        auto d = n.data_ins();
        if (d.empty()) continue;
        pointers_read_from.insert(d[0]);
    }

    // Step 2: walk the effect chain. For each Store, check:
    //   - if no Load on its pointer exists downstream before the next
    //     Store on the same pointer, it's dead.
    //   - if the pointer is never loaded from anywhere, it's dead.
    std::unordered_map<NodeId, NodeId> last_store_per_ptr;
    NodeId current_eff = kInvalidNodeId;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Start) {
            current_eff = g.make_proj(id, 1);
            break;
        }
    }
    if (current_eff == kInvalidNodeId) return 0;

    NodeId cursor = current_eff;
    while (cursor != kInvalidNodeId) {
        NodeId next = kInvalidNodeId;
        for (NodeId user : g.outputs()[cursor].view()) {
            if (user >= g.size()) continue;
            const Node& u = g[user];
            if (u.is_pure()) continue;
            if (u.eff_in() == cursor) {
                next = user;
                break;
            }
        }
        if (next == kInvalidNodeId) break;
        cursor = next;
        const Node& node = g[cursor];
        if (node.kind == NodeKind::Store) {
            auto d = node.data_ins();
            NodeId ptr = d.empty() ? kInvalidNodeId : d[0];
            if (ptr == kInvalidNodeId) continue;
            // If the pointer is never read from anywhere, this Store is dead.
            if (pointers_read_from.find(ptr) == pointers_read_from.end()) {
                g[cursor].flags.set(NodeFlagBit::IsDead);
                ++removed;
                continue;
            }
            // If a previous Store on this pointer happened and no Load
            // intervened, the previous Store is dead.
            auto it = last_store_per_ptr.find(ptr);
            if (it != last_store_per_ptr.end()) {
                g[it->second].flags.set(NodeFlagBit::IsDead);
                ++removed;
                it->second = cursor;
            } else {
                last_store_per_ptr[ptr] = cursor;
            }
        } else if (node.kind == NodeKind::Load) {
            // A Load invalidates the cached last-store on its pointer
            // (we read the stored value).
            auto d = node.data_ins();
            NodeId ptr = d.empty() ? kInvalidNodeId : d[0];
            if (ptr != kInvalidNodeId) {
                last_store_per_ptr.erase(ptr);
            }
        } else if (node.kind == NodeKind::AtomicLoad ||
                   node.kind == NodeKind::AtomicStore ||
                   node.kind == NodeKind::AtomicRMW ||
                   node.kind == NodeKind::Fence ||
                   node.kind == NodeKind::CallAltered ||
                   node.kind == NodeKind::CallCrowded) {
            // Any Crowded/Call operation invalidates *all* cached stores
            // (it may have read or written anything we can prove).
            last_store_per_ptr.clear();
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
