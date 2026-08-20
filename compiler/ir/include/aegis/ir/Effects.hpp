// ============================================================
// aegis/ir/Effects.hpp — Effect typing system & alias analysis interfaces.
// ============================================================
// Law (Section §1 of the spec, "How the Compiler Infers Effects"):
//   Pure     — only reads args + immutable locals; calls only Pure.
//   Altered  — writes to a &mut / mutable global / local var.
//   Crowded  — calls any std.io, std.atomic, std.thread, FFI.
//
// This header formalizes:
//   - The EffectClass enum (already in NodeKind.hpp).
//   - EffectTags — fine-grained orthogonal effect attributes stored as
//     a bitmask (Rule 51).
//   - The AliasAnalysisInterface — required by Speculative Effect
//     Reordering (Pass 49) and CFL-Reachability Alias Analysis.
// ============================================================
#pragma once

#include "aegis/support/Flags.hpp"
#include "aegis/support/Primitives.hpp"

namespace aegis::ir {

// Re-export EffectClass from NodeKind.hpp.
enum class NodeKind : uint16_t;
enum class EffectClass : uint8_t;

[[nodiscard]] EffectClass effect_class_of(NodeKind k) noexcept;
[[nodiscard]] bool is_pure(NodeKind k) noexcept;
[[nodiscard]] bool is_altered(NodeKind k) noexcept;
[[nodiscard]] bool is_crowded(NodeKind k) noexcept;

// Fine-grained orthogonal effect attributes (Rule 51: bitmasked).
// These subdivide the three coarse effect classes into the specific
// observable operations that the compiler must reason about.
enum class EffectTag : uint32_t {
    None              = 0,
    // ---- Altered sub-kinds ----
    WritesMemory      = 1u << 0,  // mutates a memory location
    ReadsMemory       = 1u << 1,  // reads a memory location that may be mutated elsewhere
    Allocates         = 1u << 2,  // allocates heap memory
    Frees             = 1u << 3,  // frees heap memory
    MutatesReference  = 1u << 4,  // writes through &mut
    // ---- Crowded sub-kinds ----
    IoWrite           = 1u << 5,  // writes to a file / network / stderr
    IoRead            = 1u << 6,  // reads from a file / network / stdin
    AtomicAccess      = 1u << 7,  // std::atomic load/store/rmw
    ThreadSync        = 1u << 8,  // mutex / condvar / channel
    FfiCall           = 1u << 9,  // FFI call (must respect C ABI)
    MayDeoptimize     = 1u << 10, // emits a guard that may deopt
    MayTrap           = 1u << 11, // integer div-by-zero, etc.
    // ---- Pure sub-kinds (for analysis only — Pure nodes never escape) ----
    MayOverflow       = 1u << 12, // arithmetic may overflow
    MayBeNaN          = 1u << 13, // float result may be NaN
};
} // namespace aegis::ir

// Enable bitmask ops on EffectTag (Rule 51).
namespace aegis {
template <typename E> class Flags;
}
namespace aegis::ir {
using EffectTags = aegis::Flags<EffectTag>;
}

#include "aegis/ir/NodeKind.hpp"  // pulls in EffectClass + effect_class_of

// Provide AEGIS_DEFINE_BITMASK_OPS for EffectTag here (after the using
// alias above).
namespace aegis {
// Defined via the macro in Flags.hpp — we re-include to ensure it's in scope.
}
#include "aegis/support/Flags.hpp"  // for AEGIS_DEFINE_BITMASK_OPS

namespace aegis::ir {
AEGIS_DEFINE_BITMASK_OPS(EffectTag);
}

namespace aegis::ir {

// ============================================================
// AliasAnalysisInterface — abstract interface used by Speculative
// Effect Reordering (Pass 49), Speculative Bounds Check Elimination
// (Pass 51), and Partial Escape Analysis (Pass 10).
// ============================================================
class AliasAnalysisInterface {
public:
    virtual ~AliasAnalysisInterface() = default;

    // Returns true iff pointer `a` and pointer `b` may alias.
    // Implementations should be conservative: returning `true` (may-alias)
    // is always safe; returning `false` (must-not-alias) requires proof.
    [[nodiscard]] virtual bool may_alias(NodeId a, NodeId b) const noexcept = 0;

    // Returns true iff pointer `a` is provably non-escaping within
    // the function (i.e., cannot be observed outside).
    [[nodiscard]] virtual bool is_non_escaping(NodeId a) const noexcept = 0;
};

// NoAliasAnalysis — the trivial, always-may-alias implementation.
// Used as the default when no alias analysis has run yet.
class NoAliasAnalysis final : public AliasAnalysisInterface {
public:
    [[nodiscard]] bool may_alias(NodeId, NodeId) const noexcept override { return true; }
    [[nodiscard]] bool is_non_escaping(NodeId) const noexcept override { return false; }
};

} // namespace aegis::ir
