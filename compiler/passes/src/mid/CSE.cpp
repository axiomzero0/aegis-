// passes/mid/CSE.cpp — Effect-sensitive Common Subexpression Elimination.
//
// GVN (already implemented in GVN.hpp) handles Pure nodes globally.
// CSE handles the cases that GVN cannot:
//   - Two Load nodes with the same pointer base that have no
//     intervening Store (the "load-after-load" case).
//   - Two computations that are equal but have *different* control
//     dependencies (e.g. both in branches that don't strictly dominate
//     each other).
//
// For the prototype we implement the load-after-load case:
//   - Walk the effect chain forward.
//   - Maintain a map from pointer NodeId -> last Load's NodeId.
//   - When we see a new Load on a pointer that's in the map and no
//     Store on that pointer has happened in between, replace the new
//     Load with the previous Load's id (rewire uses, mark dead).
#include "aegis/passes/mid/CSE.hpp"

#include "aegis/ir/NodeKind.hpp"

#include <unordered_map>

namespace aegis::passes::mid {

int CSEPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;
    // Walk the effect chain linearly. Start at Start's effect proj.
    NodeId current_eff = kInvalidNodeId;
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Start) {
            current_eff = g.make_proj(id, 1);
            break;
        }
    }
    if (current_eff == kInvalidNodeId) return 0;

    // Map from pointer NodeId -> last Load node id that read from it
    // (and no intervening Store on that pointer has happened).
    std::unordered_map<NodeId, NodeId> last_load_per_ptr;

    NodeId cursor = current_eff;
    while (cursor != kInvalidNodeId) {
        // Find the next effect in the chain (an output that is an
        // Altered/Crowded node whose eff_in == cursor).
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
        if (node.kind == NodeKind::Load) {
            auto d = node.data_ins();
            NodeId ptr = d.empty() ? kInvalidNodeId : d[0];
            if (ptr == kInvalidNodeId) continue;
            auto it = last_load_per_ptr.find(ptr);
            if (it != last_load_per_ptr.end()) {
                // Replace this Load with the previous one.
                NodeId prev_load = it->second;
                for (NodeId user : g.outputs()[cursor].view()) {
                    g.swap_input(user, cursor, prev_load);
                }
                g[cursor].flags.set(NodeFlagBit::IsDead);
                ++removed;
            } else {
                last_load_per_ptr[ptr] = cursor;
            }
        } else if (node.kind == NodeKind::Store) {
            // A Store invalidates any cached Load on the same pointer.
            auto d = node.data_ins();
            NodeId ptr = d.empty() ? kInvalidNodeId : d[0];
            if (ptr != kInvalidNodeId) {
                last_load_per_ptr.erase(ptr);
            }
        }
    }
    (void)budget;
    return removed;
}

} // namespace aegis::passes::mid
