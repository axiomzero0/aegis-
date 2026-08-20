// ============================================================
// aegis/ir/Types.hpp — Affine type system representations.
// ============================================================
// Law (Section §I "Affine Type Checking"):
//   "Enforces strict ownership, borrowing, and lexical region lifetimes.
//    Guarantees memory safety without runtime GC."
//
// Aegis's type system is affine (each value used at most once by default)
// with explicit borrow lifetimes (`&` and `&mut`). This header provides
// the type table and the per-function type checker interface.
// ============================================================
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aegis/support/Flags.hpp"
#include "aegis/support/Primitives.hpp"
#include "aegis/ir/NodeKind.hpp"

namespace aegis::ir {

// Primitive builtin types. The enum values double as TypeIds in a small
// reserved range [0, 256).
enum class PrimType : uint8_t {
    I8     = 1,
    I16    = 2,
    I32    = 3,
    I64    = 4,
    U8     = 5,
    U16    = 6,
    U32    = 7,
    U64    = 8,
    F32    = 9,
    F64    = 10,
    Bool   = 11,
    Str    = 12,
    Unit   = 13,
    Ptr    = 14,  // raw pointer; affine ownership
};

// Type kind (for the TypeTable's tagged union).
enum class TypeKind : uint8_t {
    Prim     = 0,
    Ref      = 1,  // borrow: &T (immutable) or &mut T (mutable)
    Array    = 2,  // [T; N]
    Slice    = 3,  // &[T]
    Tuple    = 4,
    Struct   = 5,
    Enum     = 6,
    Fn       = 7,  // function type with signature
    Region   = 8,  // lifetime region
};

// Orthogonal type qualifiers (Rule 51: bitmasked).
enum class TypeFlag : uint32_t {
    None       = 0,
    Mutable    = 1u << 0,  // &mut (vs. &)
    Sendable   = 1u << 1,  // safe to move across threads
    Syncable   = 1u << 2,  // safe to share across threads
    NoAlias    = 1u << 3,  // pointer doesn't alias anything else in scope
    ReadOnly   = 1u << 4,  // immutable after construction
    Affine     = 1u << 5,  // value may be used at most once
    Owned      = 1u << 6,  // value owns its resource (drop on scope exit)
};
} // namespace aegis::ir

#include "aegis/support/Flags.hpp"
namespace aegis::ir {
using TypeFlags = aegis::Flags<TypeFlag>;
}
namespace aegis::ir {
AEGIS_DEFINE_BITMASK_OPS(TypeFlag);
}

namespace aegis::ir {

// A single type entry in the TypeTable. Stored as a tagged union; the
// union payload is the field type ids for structs/enums, the parameter
// types for fn types, the element type for arrays, or the region id for
// references.
struct TypeEntry {
    TypeKind   kind{TypeKind::Prim};
    TypeFlags  flags{};
    SymbolId   name{kInvalidSymbolId};   // for named types (struct/enum)
    PrimType   prim{PrimType::Unit};
    TypeId     element_type{kInvalidTypeId};
    uint32_t   array_count{0};
    RegionId   region{kInvalidRegionId}; // for reference types
    // For struct/enum: list of field types (struct) or list of variant
    // type-sets (enum).
    std::vector<TypeId> sub_types{};
};

// The TypeTable owns all deduplicated TypeId entries. Built by the
// frontend's type checker.
class TypeTable {
public:
    TypeTable();

    // Intern a primitive type. Returns its TypeId (fast path).
    TypeId intern_prim(PrimType p) noexcept;

    // Intern a struct/enum type with the given name + fields/variants.
    TypeId intern_named(TypeKind k, SymbolId name,
                        std::vector<TypeId> sub_types,
                        TypeFlags flags = TypeFlag::None);

    // Intern a reference (borrow) type: &T or &mut T.
    TypeId intern_ref(TypeId pointee, RegionId region, bool is_mut);

    // Intern an array type.
    TypeId intern_array(TypeId element, uint32_t count);

    // Intern a function type.
    TypeId intern_fn(std::vector<TypeId> param_tys, TypeId ret_ty,
                     EffectClass callee_effect);

    [[nodiscard]] const TypeEntry& at(TypeId id) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

private:
    std::vector<TypeEntry> entries_{};
};

} // namespace aegis::ir
