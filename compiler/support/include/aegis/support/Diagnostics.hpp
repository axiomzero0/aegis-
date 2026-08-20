// ============================================================
// common/Diagnostics.h — Error + DiagnosticSink (no exceptions on hot path).
// ============================================================
// Law: Rule B.1 — All fallible operations use Result<T, Error>. No throw.
//
// `Error` is a small, value-typed (non-throwing) error carrier. It carries:
//   - a Severity (Error/Fatal)
//   - a source Span (file / line / column)
//   - a Category (Lex/Parse/Type/Effect/IR/Verify/Backend/...)
//   - a 32-bit interned message-id (Rule 54: no std::string in hot path)
//   - a 64-bit payload slot for small attached data
//
// `DiagnosticSink` is a write-only destination for diagnostics that the
// *frontend* (parsing, type checking) can use ergonomically. It writes to
// an unbuffered stdio stream (debug build) or to a monotonic buffer
// (release). Errors flow back through `Expected<T>` on the hot path.
// ============================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include "aegis/support/Flags.hpp"
#include "aegis/support/Primitives.hpp"

namespace aegis {

// Severity of a diagnostic.
enum class Severity : uint8_t {
    Note    = 0,
    Warning = 1,
    Error   = 2,
    Fatal   = 3,
};

// Coarse category — which stage produced the diagnostic.
enum class DiagCategory : uint8_t {
    Lex      = 0,
    Parse    = 1,
    Type     = 2,
    Effect   = 3,
    IR       = 4,
    Verify   = 5, // Rule 42 graph verifier
    Backend  = 6,
    Pass     = 7,
    Internal = 8, // Aegis internal invariant violation
};

// Source span — (file, line, col) of the start + length in chars.
// 16 bytes; cheap to pass by value.
struct Span {
    SymbolId file_id{kInvalidSymbolId}; // interned filename
    uint32_t line{0};
    uint32_t col{0};
    uint32_t len{0};
    constexpr bool valid() const noexcept { return file_id != kInvalidSymbolId; }
};

// Compact, value-typed error. 32 bytes. No allocations.
struct Error {
    Severity    severity{Severity::Error};
    DiagCategory category{DiagCategory::Internal};
    Span        span{};
    uint32_t    message_id{0};     // interned message id (Rule 54)
    uint64_t    payload{0};        // small attached data (e.g. offending id)
    bool        operator==(const Error&) const noexcept = default;

    // Convenience constructors for common error kinds.
    static constexpr Error lex(uint32_t msg_id, Span sp) noexcept {
        return Error{Severity::Error, DiagCategory::Lex, sp, msg_id, 0};
    }
    static constexpr Error parse(uint32_t msg_id, Span sp) noexcept {
        return Error{Severity::Error, DiagCategory::Parse, sp, msg_id, 0};
    }
    static constexpr Error type_(uint32_t msg_id, Span sp) noexcept {
        return Error{Severity::Error, DiagCategory::Type, sp, msg_id, 0};
    }
    static constexpr Error effect(uint32_t msg_id, Span sp) noexcept {
        return Error{Severity::Error, DiagCategory::Effect, sp, msg_id, 0};
    }
    static constexpr Error verify(uint32_t msg_id) noexcept {
        return Error{Severity::Fatal, DiagCategory::Verify, Span{}, msg_id, 0};
    }
};

// A simple in-memory diagnostic sink. Not on the hot path; can use exceptions
// in the *constructor* for setup but the `report()` path must be noexcept.
class DiagnosticSink {
public:
    DiagnosticSink() = default;
    explicit DiagnosticSink(FILE* out) : out_(out ? out : stderr) {}

    void report(Error e) noexcept;
    void note(std::string_view text) noexcept; // appends to a small buffer

    [[nodiscard]] bool has_errors() const noexcept { return error_count_ > 0; }
    [[nodiscard]] uint32_t error_count() const noexcept { return error_count_; }

    // Reset for reuse (does not deallocate; capacity is retained).
    void clear() noexcept {
        error_count_ = 0;
        buffer_.clear();
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {reinterpret_cast<const std::byte*>(buffer_.data()), buffer_.size()};
    }

private:
    FILE* out_{stderr};
    uint32_t error_count_{0};
    std::vector<char> buffer_{}; // not hot-path
};

} // namespace aegis
