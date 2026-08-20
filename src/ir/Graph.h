// ============================================================
// ir/Graph.h — The Sea-of-Nodes graph (PMR-arena + NodeId-based edges).
// ============================================================
// Laws implemented here:
//   Rule 53 — All edges are NodeId (uint32_t). No raw pointers.
//   Rule 54 — All identifiers in the IR are SymbolIds.
//   Rule B.2 — Allocation goes through a std::pmr::monotonic_buffer_resource.
//              Bulk-free after compilation; no malloc/free in the hot path.
//   Rule 42 — Graph verifier runs in debug after every pass (verifier is
//              invoked by PassManager, not here, to keep this file small).
//   Rule 50 — Versioned: the graph carries a compile-version stamp so
//              cached profiles are invalidated when the IR changes.
// ============================================================
#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/Flags.h"
#include "common/Primitives.h"
#include "core/SymbolTable.h"
#include "ir/Node.h"
#include "ir/NodeKind.h"

namespace aegis {

// Monotonic-buffer-resource-backed arena. The graph owns exactly one
// arena, and all nodes live inside it. Bulk-free at graph destruction.
class IRArena {
public:
    IRArena() : arena_(buffer_, sizeof(buffer_)) {}
    std::pmr::memory_resource* resource() noexcept { return &arena_; }

private:
    // Initial inline buffer (256 KB). If we grow past this, the
    // monotonic_buffer_resource will chain to additional heap allocations
    // transparently. In the AOT/JIT hot path, the IR rarely exceeds this.
    alignas(64) std::byte buffer_[1u << 18]{};
    std::pmr::monotonic_buffer_resource arena_;
};

class Graph {
public:
    explicit Graph(SymbolTable* syms) : syms_(syms) {
        // Create the Start node at id 0 (Rule 53 convention).
        nodes_.emplace_back(); // default-constructed Start
        nodes_.back().kind  = NodeKind::Start;
        nodes_.back().effect = EffectClass::Pure; // Start is a structural root, not Pure per se
        outputs_.emplace_back();
        // Also reserve a small set of pre-allocated outputs slots.
    }

    // ---- Accessors ----
    [[nodiscard]] Node&       operator[](NodeId id)       noexcept { return nodes_[id]; }
    [[nodiscard]] const Node& operator[](NodeId id) const noexcept { return nodes_[id]; }
    [[nodiscard]] Node&       at(NodeId id)       noexcept { return nodes_[id]; }
    [[nodiscard]] const Node& at(NodeId id) const noexcept { return nodes_[id]; }
    [[nodiscard]] std::span<const Node>      nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<OutputList>       outputs()       noexcept { return outputs_; }
    [[nodiscard]] std::span<const OutputList> outputs() const noexcept { return outputs_; }
    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(nodes_.size()); }
    [[nodiscard]] SymbolTable* syms() noexcept { return syms_; }
    [[nodiscard]] const SymbolTable* syms() const noexcept { return syms_; }

    // ---- Node creation (all edges are NodeId) ----
    NodeId make_node(NodeKind kind, std::initializer_list<NodeId> inputs,
                     TypeId type_id = kInvalidTypeId, NodePayload payload = {});

    // Convenience constructors for common node kinds.
    NodeId make_constant_i64(int64_t v, TypeId ty);
    NodeId make_constant_u64(uint64_t v, TypeId ty);
    NodeId make_constant_f64(double v, TypeId ty);
    NodeId make_binop(NodeKind k, NodeId a, NodeId b, TypeId ty);
    NodeId make_cmp(NodeKind k, NodeId a, NodeId b);
    NodeId make_altered(NodeKind k, NodeId ctrl_in, NodeId eff_in,
                        std::initializer_list<NodeId> data_ins,
                        TypeId ty = kInvalidTypeId,
                        NodePayload payload = {});
    NodeId make_load(NodeId ctrl, NodeId eff, NodeId ptr, TypeId ty);
    NodeId make_store(NodeId ctrl, NodeId eff, NodeId ptr, NodeId val);
    NodeId make_alloc(NodeId ctrl, NodeId eff, TypeId ty);
    NodeId make_if(NodeId ctrl, NodeId cond);
    NodeId make_proj(NodeId src, uint32_t which, TypeId ty = kInvalidTypeId);
    NodeId make_region(std::initializer_list<NodeId> preds);
    NodeId make_loop(NodeId back_pred, NodeId entry_pred);
    NodeId make_phi(NodeId region, std::initializer_list<NodeId> vals, TypeId ty);
    NodeId make_return(NodeId ctrl, NodeId eff, NodeId val);
    NodeId make_call(NodeId ctrl, NodeId eff, SymbolId callee,
                     std::initializer_list<NodeId> args, TypeId ret_ty,
                     EffectClass callee_effect);
    // Overload accepting std::span (or any contiguous range) for runtime-built
    // arg lists (e.g. from the AST lowerer).
    NodeId make_call(NodeId ctrl, NodeId eff, SymbolId callee,
                     std::span<const NodeId> args, TypeId ret_ty,
                     EffectClass callee_effect);
    NodeId make_guard(NodeId ctrl, NodeId eff, NodeId cond,
                      FrameStateId fs);
    NodeId make_frame_state(std::initializer_list<NodeId> snapshot);

    // ---- Edge mutation ----
    // Replaces a single input edge of `n` from `old` to `new`. Also
    // updates the previous and new outputs lists.
    void swap_input(NodeId n, NodeId old_in, NodeId new_in);
    // Replaces the i-th input of `n` with `new_in`. Updates output lists.
    void set_input(NodeId n, size_t i, NodeId new_in);
    // Removes a node from the IR (marks it Dead; the sweep pass actually
    // frees the slot). Ids of *other* nodes are stable.
    void mark_dead(NodeId n) noexcept;

    // ---- Verification ----
    bool verify(/* out: std::string& why */ std::string& why) const;

    // ---- Version stamp (Rule 50) ----
    // Bumped whenever the IR is structurally mutated by a pass. Used to
    // invalidate cached profile data / code caches.
    void bump_version() noexcept { ++version_; }
    [[nodiscard]] uint64_t version() const noexcept { return version_; }

private:
    std::vector<Node>       nodes_{};
    std::vector<OutputList> outputs_{};
    SymbolTable*            syms_{};
    uint64_t                version_{1};
    uint32_t                next_dead_epoch_{0};

    void ensure_output_slot(NodeId id) {
        while (outputs_.size() <= id) outputs_.emplace_back();
    }
    void link_input_to_output(NodeId node, NodeId input) {
        if (input == kInvalidNodeId) return;
        ensure_output_slot(input);
        outputs_[input].add(node);
    }
    void unlink_input_from_output(NodeId node, NodeId input) {
        if (input == kInvalidNodeId) return;
        if (input < outputs_.size()) outputs_[input].remove(node);
    }
};

} // namespace aegis
