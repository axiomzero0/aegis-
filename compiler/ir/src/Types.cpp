// ir/Types.cpp — Real TypeTable implementation.
//
// Builds the affine type table. Types are deduplicated so equal types
// have equal TypeIds. Reference types carry their region id for borrow
// lifetime scoping. Struct/Enum types store their field type lists.
#include "aegis/ir/Types.hpp"

#include <algorithm>

namespace aegis::ir {

TypeTable::TypeTable() {
    entries_.reserve(64);
    // Reserve a small range for the primitives so they have stable ids.
    for (int i = 0; i <= 14; ++i) {
        TypeEntry e;
        e.kind = TypeKind::Prim;
        e.prim = static_cast<PrimType>(i);
        entries_.push_back(e);
    }
}

TypeId TypeTable::intern_prim(PrimType p) noexcept {
    // The primitives are at entries_[1..14].
    return static_cast<TypeId>(static_cast<uint32_t>(p));
}

TypeId TypeTable::intern_named(TypeKind k, SymbolId name,
                               std::vector<TypeId> sub_types,
                               TypeFlags flags) {
    // Dedup by (kind, name).
    for (TypeId i = 0; i < entries_.size(); ++i) {
        const TypeEntry& e = entries_[i];
        if (e.kind == k && e.name == name && e.flags.raw() == flags.raw() &&
            e.sub_types == sub_types) {
            return i;
        }
    }
    TypeEntry e;
    e.kind = k;
    e.name = name;
    e.sub_types = std::move(sub_types);
    e.flags = flags;
    entries_.push_back(std::move(e));
    return static_cast<TypeId>(entries_.size() - 1);
}

TypeId TypeTable::intern_ref(TypeId pointee, RegionId region, bool is_mut) {
    // Dedup by (pointee, region, is_mut).
    for (TypeId i = 0; i < entries_.size(); ++i) {
        const TypeEntry& e = entries_[i];
        if (e.kind == TypeKind::Ref && e.element_type == pointee &&
            e.region == region &&
            e.flags.has(TypeFlag::Mutable) == is_mut) {
            return i;
        }
    }
    TypeEntry e;
    e.kind = TypeKind::Ref;
    e.element_type = pointee;
    e.region = region;
    if (is_mut) e.flags.set(TypeFlag::Mutable);
    entries_.push_back(e);
    return static_cast<TypeId>(entries_.size() - 1);
}

TypeId TypeTable::intern_array(TypeId element, uint32_t count) {
    for (TypeId i = 0; i < entries_.size(); ++i) {
        const TypeEntry& e = entries_[i];
        if (e.kind == TypeKind::Array && e.element_type == element &&
            e.array_count == count) {
            return i;
        }
    }
    TypeEntry e;
    e.kind = TypeKind::Array;
    e.element_type = element;
    e.array_count = count;
    entries_.push_back(e);
    return static_cast<TypeId>(entries_.size() - 1);
}

TypeId TypeTable::intern_fn(std::vector<TypeId> param_tys, TypeId ret_ty,
                            EffectClass callee_effect) {
    // Dedup by (params, ret, effect).
    for (TypeId i = 0; i < entries_.size(); ++i) {
        const TypeEntry& e = entries_[i];
        if (e.kind == TypeKind::Fn && e.sub_types == param_tys &&
            e.element_type == ret_ty &&
            e.array_count == static_cast<uint32_t>(callee_effect)) {
            return i;
        }
    }
    TypeEntry e;
    e.kind = TypeKind::Fn;
    e.sub_types = std::move(param_tys);
    e.element_type = ret_ty;
    e.array_count = static_cast<uint32_t>(callee_effect);
    entries_.push_back(e);
    return static_cast<TypeId>(entries_.size() - 1);
}

const TypeEntry& TypeTable::at(TypeId id) const noexcept {
    return entries_[id];
}

} // namespace aegis::ir
