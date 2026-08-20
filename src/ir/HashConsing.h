// ir/HashConsing.h — Hash-cons table for IR nodes (native GVN).
#pragma once

#include "core/SwissTable.h"
#include "ir/Graph.h"

namespace aegis {

// A hash-cons of the Pure and Altered nodes in a graph. Lookup-or-insert
// by structural signature (kind + inputs + payload + type). Returns the
// existing NodeId if an identical node is already in the IR, otherwise
// allocates a new node and returns its id.
//
// This is the mechanism that gives GVN for free — Pure nodes are
// canonicalized as they're built. Altered nodes can also be hash-consed
// when they're effect-equivalent (no intervening effect that would
// distinguish them).
class HashCons {
public:
    explicit HashCons(Graph& g) : g_(g) {}

    // Lookup-or-insert by structural signature. Only meaningful for Pure
    // nodes (the common case) and Altered nodes that are
    // effect-independent (rare).
    NodeId lookup_or_insert(NodeKind kind, std::initializer_list<NodeId> inputs,
                            TypeId ty, NodePayload payload);

private:
    Graph& g_;
    // Key: a packed representation of (kind, type_id, payload bits).
    struct Key {
        uint16_t kind;
        uint16_t _pad;
        uint32_t type_id;
        uint64_t payload;
        // For simplicity, we hash the inputs separately by chaining
        // them into the payload bits via FNV-1a. (A more robust
        // implementation would use a proper 128-bit hash.)
        uint64_t inputs_hash;
        bool operator==(const Key&) const noexcept = default;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            uint64_t h = 0xcbf29ce484222325ULL;
            h ^= k.kind;       h *= 0x100000001b3ULL;
            h ^= k.type_id;    h *= 0x100000001b3ULL;
            h ^= k.payload;    h *= 0x100000001b3ULL;
            h ^= k.inputs_hash; h *= 0x100000001b3ULL;
            return static_cast<size_t>(h);
        }
    };
    SwissTable<Key, NodeId, KeyHash> table_{};
};

} // namespace aegis
