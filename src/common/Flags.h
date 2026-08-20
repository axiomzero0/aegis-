// ============================================================
// common/Flags.h — Type-safe bitmask for orthogonal boolean state.
// ============================================================
// Law: Rule 51 — "All orthogonal boolean state must be bitmasked."
//       Raw integers are forbidden for flag-like state on hot-path
//       data structures (NodeFlags, EffectTags, etc.).
//
// Usage:
//   enum class NodeFlag : uint32_t {
//       None        = 0,
//       IsPure      = 1 << 0,
//       IsAltered   = 1 << 1,
//       IsCrowded   = 1 << 2,
//       HasSideEffect = 1 << 3,
//       IsGuard     = 1 << 4,
//       // ...
//   };
//   AEGIS_DEFINE_BITMASK_OPS(NodeFlag);
//   Flags<NodeFlag> f = NodeFlag::IsPure | NodeFlag::IsGuard;
//   if (f.has(NodeFlag::IsGuard)) { ... }
// ============================================================
#pragma once

#include <cstdint>
#include <type_traits>

namespace aegis {

// Strongly-typed Flags<E> wrapper. Holds any enum whose underlying type is
// an unsigned integer. Cannot be implicitly converted to/from raw integers.
template <typename E>
class Flags {
public:
    using Underlying = std::underlying_type_t<E>;

    static_assert(std::is_enum_v<E>,
                  "Flags<E> requires E to be an enum type.");
    static_assert(std::is_unsigned_v<Underlying>,
                  "Flags<E> requires E's underlying type to be unsigned.");

    constexpr Flags() noexcept = default;
    constexpr Flags(E v) noexcept : value_(static_cast<Underlying>(v)) {}

    // Compound assignment operators (Rule 51 idiomatic API).
    constexpr Flags& operator|=(Flags o) noexcept { value_ |= o.value_; return *this; }
    constexpr Flags& operator&=(Flags o) noexcept { value_ &= o.value_; return *this; }
    constexpr Flags& operator^=(Flags o) noexcept { value_ ^= o.value_; return *this; }

    // Queries.
    [[nodiscard]] constexpr bool has(E bit) const noexcept {
        return (value_ & static_cast<Underlying>(bit)) != 0;
    }
    [[nodiscard]] constexpr bool has_all(Flags mask) const noexcept {
        return (value_ & mask.value_) == mask.value_;
    }
    [[nodiscard]] constexpr bool has_any(Flags mask) const noexcept {
        return (value_ & mask.value_) != 0;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return value_ == 0; }
    [[nodiscard]] constexpr Underlying raw() const noexcept { return value_; }

    // Mutators.
    constexpr Flags& set(E bit) noexcept {
        value_ |= static_cast<Underlying>(bit);
        return *this;
    }
    constexpr Flags& clear(E bit) noexcept {
        value_ &= ~static_cast<Underlying>(bit);
        return *this;
    }
    constexpr Flags& toggle(E bit) noexcept {
        value_ ^= static_cast<Underlying>(bit);
        return *this;
    }
    constexpr void clear_all() noexcept { value_ = 0; }

private:
    Underlying value_{0};
};

// Free-function bitmask operators.
template <typename E>
[[nodiscard]] constexpr Flags<E> operator|(Flags<E> a, Flags<E> b) noexcept {
    return Flags<E>(static_cast<E>(a.raw() | b.raw()));
}
template <typename E>
[[nodiscard]] constexpr Flags<E> operator&(Flags<E> a, Flags<E> b) noexcept {
    return Flags<E>(static_cast<E>(a.raw() & b.raw()));
}
template <typename E>
[[nodiscard]] constexpr Flags<E> operator^(Flags<E> a, Flags<E> b) noexcept {
    return Flags<E>(static_cast<E>(a.raw() ^ b.raw()));
}
template <typename E>
[[nodiscard]] constexpr Flags<E> operator~(Flags<E> a) noexcept {
    return Flags<E>(static_cast<E>(~a.raw()));
}

} // namespace aegis

// Convenience macro: define the bitmask ops for a given enum.
#define AEGIS_DEFINE_BITMASK_OPS(EnumType)                                    \
    static_assert(std::is_enum_v<EnumType>, #EnumType " must be an enum.");    \
    inline constexpr EnumType operator|(EnumType a, EnumType b) noexcept {     \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));  \
    }                                                                          \
    inline constexpr EnumType operator&(EnumType a, EnumType b) noexcept {     \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));  \
    }                                                                          \
    inline constexpr EnumType operator^(EnumType a, EnumType b) noexcept {     \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(static_cast<U>(a) ^ static_cast<U>(b));  \
    }                                                                          \
    inline constexpr EnumType operator~(EnumType a) noexcept {                 \
        using U = std::underlying_type_t<EnumType>;                            \
        return static_cast<EnumType>(~static_cast<U>(a));                      \
    }                                                                          \
    inline constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept {  \
        a = a | b; return a;                                                   \
    }                                                                          \
    inline constexpr EnumType& operator&=(EnumType& a, EnumType b) noexcept {  \
        a = a & b; return a;                                                   \
    }                                                                          \
    inline constexpr EnumType& operator^=(EnumType& a, EnumType b) noexcept {  \
        a = a ^ b; return a;                                                   \
    }
