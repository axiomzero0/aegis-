// ir/HashConsing.cpp — FNV-1a hash of inputs + lookup-or-insert.
#include "ir/HashConsing.h"

namespace aegis {

namespace {
uint64_t hash_inputs(std::initializer_list<NodeId> inputs) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (NodeId i : inputs) {
        h ^= static_cast<uint64_t>(i);
        h *= 0x100000001b3ULL;
    }
    return h;
}
} // namespace

NodeId HashCons::lookup_or_insert(NodeKind kind, std::initializer_list<NodeId> inputs,
                                  TypeId ty, NodePayload payload) {
    Key key{
        .kind        = static_cast<uint16_t>(kind),
        ._pad        = 0,
        .type_id     = ty,
        .payload     = payload.u64,
        .inputs_hash = hash_inputs(inputs),
    };
    if (NodeId* existing = table_.get(key); existing != nullptr) {
        return *existing;
    }
    // Allocate a new node in the graph and record it in the table.
    NodeId id = g_.make_node(kind, inputs, ty, payload);
    table_.insert(key, id);
    return id;
}

} // namespace aegis
