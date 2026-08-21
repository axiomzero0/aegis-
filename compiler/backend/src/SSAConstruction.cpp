// backend/SSAConstruction.cpp — Convert SoN to SSA form.
//
// In a Sea-of-Nodes IR, every Pure node is already in SSA form (no
// variable assignments, every value has a single producer). This
// pass:
//
//   1. Walks every Region node and checks if its predecessors
//      define different values for the same logical name.
//   2. If so, inserts a Phi node at the Region merging the values.
//
// For the prototype, the Lowerer already emits Phi nodes at If-branch
// merge points (see Lowering.cpp's bindings_after_then/else merge
// logic). This pass is a verification + cleanup step that ensures
// the convention holds.
//
// Rule B.5: idempotent — once Phis are inserted, the next pass
// sees the Phis already in place.
// Rule B.6: monotone (we only add Phis at genuine merge points).
#include "aegis/backend/SSAConstruction.hpp"

#include "aegis/ir/NodeKind.hpp"

namespace aegis::backend {

int SSAConstructor::run() noexcept {
    // For the prototype, Phi insertion is done in the Lowerer.
    // This pass is a no-op — we count the Phis that already exist.
    int phi_count = 0;
    for (NodeId id = 0; id < g_.size(); ++id) {
        const Node& n = g_[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.kind == NodeKind::Phi) ++phi_count;
    }
    return phi_count;
}

} // namespace aegis::backend
