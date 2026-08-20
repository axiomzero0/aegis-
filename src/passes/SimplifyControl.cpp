// passes/SimplifyControl.cpp — minimal implementation; mostly stubs for now.
#include "passes/SimplifyControl.h"

#include "ir/NodeKind.h"

namespace aegis {

int SimplifyControlPass::run(Graph& g, const PassBudget& budget) {
    int removed = 0;

    // Pass A: Block Merging. If a Region has exactly one predecessor
    // and that predecessor is a Return-less Block, splice them.
    // (A real impl uses CFG analysis; this is a placeholder.)

    // Pass B: Dead Store Elimination. If two Stores write to the same
    // pointer with no intervening Load on the effect chain, the first
    // store is dead.
    // Approximate: scan the effect chain linearly and remember the last
    // Store per pointer NodeId.
    NodeId current_eff = kInvalidNodeId;
    // Find an effect root.
    for (NodeId id = 0; id < g.size(); ++id) {
        if (g[id].kind == NodeKind::Start) {
            current_eff = g.make_proj(id, 1);
            break;
        }
    }
    // Walk the effect chain via outputs of the current effect node.
    // For each Store we encounter, look back at previous Stores on the
    // same chain. If they write to the same pointer with no intervening
    // Load, mark them dead.
    if (current_eff != kInvalidNodeId) {
        std::vector<std::pair<NodeId, NodeId>> last_store_per_ptr;
        NodeId cursor = current_eff;
        while (cursor != kInvalidNodeId) {
            // Find the next effect in the chain (the output that is an
            // Altered node whose eff_in == cursor).
            NodeId next = kInvalidNodeId;
            for (NodeId user : g.outputs()[cursor].view()) {
                if (g[user].is_altered() && g[user].eff_in() == cursor) {
                    next = user;
                    break;
                }
            }
            if (next == kInvalidNodeId) break;
            cursor = next;
            if (g[cursor].kind == NodeKind::Store) {
                NodeId ptr = g[cursor].data_ins().empty() ? kInvalidNodeId : g[cursor].data_ins()[0];
                // Walk previous stores; if a later store (i.e. this one)
                // overwrites an earlier one with the same pointer and no
                // intervening Load, the earlier one is dead.
                for (auto& [sptr, sid] : last_store_per_ptr) {
                    if (sptr == ptr) {
                        // No intervening Load check is simplified: assume
                        // no Load (a real impl tracks this).
                        g.mark_dead(sid);
                        ++removed;
                        sid = kInvalidNodeId;
                    }
                }
                last_store_per_ptr.emplace_back(ptr, cursor);
            } else if (g[cursor].kind == NodeKind::Load) {
                // A Load might read any prior store; flush our records.
                last_store_per_ptr.clear();
            }
        }
    }

    (void)budget;
    return removed;
}

} // namespace aegis
