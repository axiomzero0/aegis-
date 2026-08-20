// ir/Graph.cpp — Sea-of-Nodes graph: node construction + edge maintenance.
#include "aegis/ir/Graph.hpp"

#include <charconv>
#include <sstream>

namespace aegis {

NodeId Graph::make_node(NodeKind kind, std::initializer_list<NodeId> inputs,
                        TypeId type_id, NodePayload payload) {
    NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind    = kind;
    n.effect  = effect_class_of(kind);
    n.flags   = NodeFlagBit::None;
    n.type_id = type_id;
    n.payload = payload;
    n.inputs  = SmallVector<NodeId, 3>{inputs};
    nodes_.push_back(std::move(n));
    ensure_output_slot(id);
    // Update reverse (output) edges.
    for (NodeId in : inputs) link_input_to_output(id, in);
    bump_version();
    return id;
}

NodeId Graph::make_constant_i64(int64_t v, TypeId ty) {
    NodePayload p; p.i64 = v;
    return make_node(NodeKind::Constant, {}, ty, p);
}
NodeId Graph::make_constant_u64(uint64_t v, TypeId ty) {
    NodePayload p; p.u64 = v;
    return make_node(NodeKind::Constant, {}, ty, p);
}
NodeId Graph::make_constant_f64(double v, TypeId ty) {
    NodePayload p; p.f64 = v;
    return make_node(NodeKind::Constant, {}, ty, p);
}
NodeId Graph::make_binop(NodeKind k, NodeId a, NodeId b, TypeId ty) {
    // Pure binops take only data inputs (no ctrl/eff in).
    return make_node(k, {a, b}, ty);
}
NodeId Graph::make_cmp(NodeKind k, NodeId a, NodeId b) {
    // Comparisons produce a bool result.
    return make_node(k, {a, b}, /*ty=*/kInvalidTypeId /*placeholder until TypeTable exists*/);
}
NodeId Graph::make_altered(NodeKind k, NodeId ctrl_in, NodeId eff_in,
                           std::initializer_list<NodeId> data_ins,
                           TypeId ty, NodePayload payload) {
    // Convention: inputs = { ctrl, eff, ...data }
    SmallVector<NodeId, 3> ins;
    ins.push_back(ctrl_in);
    ins.push_back(eff_in);
    for (NodeId d : data_ins) ins.push_back(d);
    NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = k;
    n.effect = effect_class_of(k);
    n.type_id = ty;
    n.payload = payload;
    n.inputs = std::move(ins);
    nodes_.push_back(std::move(n));
    ensure_output_slot(id);
    // Link outputs.
    if (ctrl_in != kInvalidNodeId) link_input_to_output(id, ctrl_in);
    if (eff_in  != kInvalidNodeId) link_input_to_output(id, eff_in);
    for (NodeId d : data_ins) link_input_to_output(id, d);
    bump_version();
    return id;
}

NodeId Graph::make_load(NodeId ctrl, NodeId eff, NodeId ptr, TypeId ty) {
    return make_altered(NodeKind::Load, ctrl, eff, {ptr}, ty);
}
NodeId Graph::make_store(NodeId ctrl, NodeId eff, NodeId ptr, NodeId val) {
    return make_altered(NodeKind::Store, ctrl, eff, {ptr, val});
}
NodeId Graph::make_alloc(NodeId ctrl, NodeId eff, TypeId ty) {
    return make_altered(NodeKind::Alloc, ctrl, eff, {}, ty);
}
NodeId Graph::make_if(NodeId ctrl, NodeId cond) {
    // If is a control node — convention: inputs = {ctrl_in, cond}
    return make_node(NodeKind::If, {ctrl, cond});
}
NodeId Graph::make_proj(NodeId src, uint32_t which, TypeId ty) {
    NodePayload p; p.proj_index = which;
    return make_node(NodeKind::Proj, {src}, ty, p);
}
NodeId Graph::make_region(std::initializer_list<NodeId> preds) {
    return make_node(NodeKind::Region, preds);
}
NodeId Graph::make_loop(NodeId back_pred, NodeId entry_pred) {
    // Convention: Region-like with {entry, back} preds.
    return make_node(NodeKind::Loop, {entry_pred, back_pred});
}
NodeId Graph::make_phi(NodeId region, std::initializer_list<NodeId> vals, TypeId ty) {
    // Convention: inputs = {region, ...vals}. Phi is Pure in the effect sense.
    SmallVector<NodeId, 3> ins;
    ins.push_back(region);
    for (NodeId v : vals) ins.push_back(v);
    NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = NodeKind::Phi;
    n.effect = EffectClass::Pure;
    n.type_id = ty;
    n.inputs = std::move(ins);
    nodes_.push_back(std::move(n));
    ensure_output_slot(id);
    link_input_to_output(id, region);
    for (NodeId v : vals) link_input_to_output(id, v);
    bump_version();
    return id;
}
NodeId Graph::make_return(NodeId ctrl, NodeId eff, NodeId val) {
    return make_node(NodeKind::Return, {ctrl, eff, val});
}
NodeId Graph::make_call(NodeId ctrl, NodeId eff, SymbolId callee,
                        std::initializer_list<NodeId> args, TypeId ret_ty,
                        EffectClass callee_effect) {
    return make_call(ctrl, eff, callee,
                     std::span<const NodeId>{args.begin(), args.size()},
                     ret_ty, callee_effect);
}

NodeId Graph::make_call(NodeId ctrl, NodeId eff, SymbolId callee,
                        std::span<const NodeId> args, TypeId ret_ty,
                        EffectClass callee_effect) {
    NodeKind k = NodeKind::CallPure;
    if (callee_effect == EffectClass::Altered) k = NodeKind::CallAltered;
    else if (callee_effect == EffectClass::Crowded) k = NodeKind::CallCrowded;

    NodePayload p; p.sym = callee;
    SmallVector<NodeId, 3> ins;
    ins.push_back(ctrl);
    ins.push_back(eff);
    for (NodeId a : args) ins.push_back(a);
    NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = k;
    n.effect = effect_class_of(k);
    n.type_id = ret_ty;
    n.payload = p;
    n.inputs = std::move(ins);
    nodes_.push_back(std::move(n));
    ensure_output_slot(id);
    if (ctrl != kInvalidNodeId) link_input_to_output(id, ctrl);
    if (eff  != kInvalidNodeId) link_input_to_output(id, eff);
    for (NodeId a : args) link_input_to_output(id, a);
    bump_version();
    return id;
}
NodeId Graph::make_guard(NodeId ctrl, NodeId eff, NodeId cond, FrameStateId fs) {
    NodePayload p; p.u64 = static_cast<uint64_t>(fs);
    NodeId id = make_altered(NodeKind::Guard, ctrl, eff, {cond}, kInvalidTypeId, p);
    nodes_[id].flags.set(NodeFlagBit::IsGuarded | NodeFlagBit::HasFrameState);
    return id;
}
NodeId Graph::make_frame_state(std::initializer_list<NodeId> snapshot) {
    return make_node(NodeKind::FrameState, snapshot);
}

// ---------- Edge mutation ----------
void Graph::swap_input(NodeId n, NodeId old_in, NodeId new_in) {
    Node& node = nodes_[n];
    bool  found = false;
    for (NodeId& i : node.inputs) {
        if (i == old_in) {
            i = new_in;
            found = true;
        }
    }
    if (found) {
        unlink_input_from_output(n, old_in);
        link_input_to_output(n, new_in);
        bump_version();
    }
}
void Graph::set_input(NodeId n, size_t i, NodeId new_in) {
    Node& node = nodes_[n];
    [[assume(i < node.inputs.size())]];
    NodeId old = node.inputs[i];
    if (old == new_in) return;
    node.inputs[i] = new_in;
    unlink_input_from_output(n, old);
    link_input_to_output(n, new_in);
    bump_version();
}
void Graph::mark_dead(NodeId n) noexcept {
    nodes_[n].flags.set(NodeFlagBit::IsDead);
    bump_version();
}

// ---------- Verification (Rule 42) ----------
bool Graph::verify(std::string& why) const {
    // Check 1: every NodeId in inputs refers to a node that exists and
    // is not marked Dead.
    for (NodeId id = 0; id < nodes_.size(); ++id) {
        const Node& n = nodes_[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        for (NodeId in : n.inputs) {
            if (in == kInvalidNodeId) continue;
            if (in >= nodes_.size()) {
                why = "dangling input NodeId on node " + std::to_string(id);
                return false;
            }
            if (nodes_[in].flags.has(NodeFlagBit::IsDead)) {
                why = "input " + std::to_string(in) + " of node " + std::to_string(id)
                      + " is marked dead but still referenced";
                return false;
            }
        }
    }

    // Check 2: effect-chain continuity. Every Altered/Crowded node's
    // eff_in must be a non-Pure node (effectively, must be Start or
    // another Altered/Crowded node). Pure nodes never have an eff_in.
    for (NodeId id = 0; id < nodes_.size(); ++id) {
        const Node& n = nodes_[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        if (n.is_pure()) {
            // Pure nodes do not participate in the effect chain; their
            // eff_in slot is conventionally kInvalidNodeId.
            continue;
        }
        // Non-Pure node: must have a real eff_in.
        NodeId eff_in = n.eff_in();
        if (eff_in == kInvalidNodeId && n.kind != NodeKind::Start) {
            why = "non-Pure node " + std::to_string(id) + " has no effect-in edge";
            return false;
        }
    }

    // Check 3: FrameState attached to every PGO-guard node (Rules A.5, 42).
    for (NodeId id = 0; id < nodes_.size(); ++id) {
        const Node& n = nodes_[id];
        if (n.flags.has(NodeFlagBit::IsPgoSpeculated) && n.flags.has(NodeFlagBit::IsGuarded)) {
            if (!n.flags.has(NodeFlagBit::HasFrameState)) {
                why = "PGO-speculated guard " + std::to_string(id)
                      + " is missing FrameState (Rule A.5)";
                return false;
            }
        }
    }

    // Check 4: use-def consistency — every output edge's target has the
    // source as one of its inputs.
    for (NodeId id = 0; id < outputs_.size(); ++id) {
        for (NodeId user : outputs_[id].view()) {
            if (user >= nodes_.size()) {
                why = "output list of " + std::to_string(id)
                      + " references non-existent node " + std::to_string(user);
                return false;
            }
            const Node& un = nodes_[user];
            if (un.flags.has(NodeFlagBit::IsDead)) continue;
            const auto& ins = un.inputs;
            bool found = false;
            for (NodeId i : ins) { if (i == id) { found = true; break; } }
            if (!found) {
                why = "use-def mismatch: node " + std::to_string(user)
                      + " listed as output of " + std::to_string(id)
                      + " but does not have it as input";
                return false;
            }
        }
    }

    return true;
}

} // namespace aegis
