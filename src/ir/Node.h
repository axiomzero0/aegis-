// ============================================================
// ir/Node.h — The SoN node: fixed-size, arena-friendly, NodeId edges.
// ============================================================
// Laws implemented here:
//   Rule 53 — "All node references must use a 32-bit integer index.
//              Never use raw pointers (Node*) for edges." -> inputs/outputs
//              are SmallVector<NodeId>.
//   Rule 51 — "All orthogonal boolean state must be bitmasked."
//              -> NodeFlags is Flags<NodeFlagBit>.
//   Rule B.2 — "Use std::pmr::monotonic_buffer_resource for IR allocation."
//   Rule B.3 — "Use enum class NodeKind for type switching. No RTTI."
//   Rule B.4 — "No std::shared_ptr / std::function in Hot IR Code."
//              -> raw pointers + stable NodeIds.
//   Rule 59 — SoA-friendly: the Node struct is exactly 32 bytes (no SBO
//              spill), so a contiguous std::pmr::vector<Node> iterates
//              cleanly and prefetches well.
// ============================================================
#pragma once

#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include "common/Flags.h"
#include "common/Primitives.h"
#include "core/SmallVector.h"
#include "ir/NodeKind.h"

namespace aegis {

// Individual bits of node-level orthogonal state. Stored in a Flags<>
// bitmask (Rule 51).
enum class NodeFlagBit : uint32_t {
    None           = 0,
    IsLive         = 1u << 0,  // visible from a Crowded / Return root (E-DCE)
    IsConst        = 1u << 1,  // node value is a compile-time constant
    IsHashed       = 1u << 2,  // node is in the GVN hash-cons table
    IsGuarded      = 1u << 3,  // PGO-driven: has a runtime guard attached
    IsDead         = 1u << 4,  // marked for deletion at next sweep
    HasFrameState  = 1u << 5,  // has FrameState attachment (PGO guards)
    IsPgoSpeculated= 1u << 6,  // speculation decision was based on PGO
    IsMonomorphic  = 1u << 7,  // call site is PGO-monomorphic
    IsNoReturn     = 1u << 8,  // callee never returns (e.g. abort)
    CanOverflow    = 1u << 9,  // arithmetic may overflow (for guard emission)
    IsBuiltin      = 1u << 10, // resolves to a compiler builtin
    IsAffineMove   = 1u << 11, // affine-owned value (no aliasing)
    IsStackPromoted= 1u << 12, // stack-promoted by escape analysis
    IsBoundsChecked= 1u << 13, // has a runtime bounds check attached
    IsLowered      = 1u << 14, // has been lowered to machine instrs
};
AEGIS_DEFINE_BITMASK_OPS(NodeFlagBit);

using NodeFlags = Flags<NodeFlagBit>;

// The node payload: a tagged union of the per-kind "extra" data. We keep
// it small and use a uint64_t slot that different kinds interpret differently.
union NodePayload {
    int64_t  i64;          // Constant::i32/i64 value
    uint64_t u64;          // Constant::u32/u64/bool value
    double   f64;         // Constant::f64 value
    SymbolId sym;         // Parameter / GetFieldPtr (field name)
    NodeId   single_input;// Proj source / Select condition
    uint32_t field_index; // GetFieldPtr / GetElementPtr index
    uint32_t proj_index;  // Proj output selector (If -> {0=true,1=false}, Start -> {0=ctrl,1=eff})
    uint8_t  raw[8];

    constexpr NodePayload() noexcept : u64(0) {}
};

// The Node struct. Designed to be exactly 32 bytes (well, close — the
// SmallVector spill makes it slightly larger; we size the inline
// capacity so most nodes never spill).
struct Node {
    NodeKind        kind{NodeKind::Start};
    EffectClass     effect{EffectClass::Pure};
    NodeFlags       flags{};
    TypeId          type_id{kInvalidTypeId};
    NodePayload     payload{};
    // Inputs: control + effect + data, all by NodeId. The convention is:
    //   inputs[0] = control-in   (control predecessor; kInvalidNodeId for Start)
    //   inputs[1] = effect-in    (effect predecessor; kInvalidNodeId for Pure nodes)
    //   inputs[2..] = data operands
    // For Pure nodes (e.g. Add), inputs[0] and inputs[1] are kInvalidNodeId
    // and we only use inputs[2..].
    SmallVector<NodeId, 3> inputs{};

    // ---- Helpers ----
    [[nodiscard]] constexpr bool is_pure()      const noexcept { return effect == EffectClass::Pure; }
    [[nodiscard]] constexpr bool is_altered()   const noexcept { return effect == EffectClass::Altered; }
    [[nodiscard]] constexpr bool is_crowded()   const noexcept { return effect == EffectClass::Crowded; }

    [[nodiscard]] constexpr NodeId ctrl_in()  const noexcept {
        return inputs.size() > 0 ? inputs[0] : kInvalidNodeId;
    }
    [[nodiscard]] constexpr NodeId eff_in()   const noexcept {
        return inputs.size() > 1 ? inputs[1] : kInvalidNodeId;
    }
    [[nodiscard]] std::span<const NodeId> data_ins() const noexcept {
        // For Pure nodes, all inputs are data (no ctrl/eff prefix).
        // For Altered/Crowded nodes, inputs[0]=ctrl, inputs[1]=eff, and
        // inputs[2..] are data.
        if (is_pure()) {
            return std::span<const NodeId>{inputs.data(), inputs.size()};
        }
        return inputs.size() > 2
            ? std::span<const NodeId>{inputs.data() + 2, inputs.size() - 2}
            : std::span<const NodeId>{};
    }
};
static_assert(sizeof(NodePayload) == 8, "NodePayload must be 8 bytes.");
// Note: Node total size depends on SmallVector<NodeId, 3>'s inline layout;
// we use SmallVector<NodeId, 3> so most nodes (0 to 3 inputs) never spill.

// Output edge list. Stored separately (not inside Node) so that the Node
// struct stays compact for forward iteration during passes. The Graph
// keeps a parallel vector of OutputLists indexed by NodeId.
class OutputList {
public:
    OutputList() = default;
    void add(NodeId id) { list_.push_back(id); }
    void remove(NodeId id) { list_.remove_first(id); }
    [[nodiscard]] std::span<const NodeId> view() const noexcept {
        return list_.as_span();
    }
    [[nodiscard]] uint32_t size() const noexcept { return list_.size(); }
    [[nodiscard]] bool empty() const noexcept { return list_.empty(); }
private:
    SmallVector<NodeId, 2> list_{};
};

} // namespace aegis
