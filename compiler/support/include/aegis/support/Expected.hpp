// ============================================================
// common/Expected.h — Zero-cost error propagation wrapper.
// ============================================================
// Law: Rule 60 — "Use std::expected<T, Error> ... or a custom TRY() macro
//       that compiles down to a single branch." Success path must be
//       linear and branchless.
//
// Law: Rule B.1 — "All fallible operations MUST use std::expected<T,
//       Diagnostic> or Result<T, Error>." No throw on the hot path.
//
// In C++26 std::expected is finally standard. We use it directly. The
// `AEGIS_TRY(expr)` macro below is the standard pattern: returns the
// error immediately if the expected holds an error; binds the success
// value to `name` otherwise. It produces a single branch on the success
// path because errors are the rare case.
// ============================================================
#pragma once

#include <expected>
#include <utility>
#include <variant>

#include "aegis/support/Diagnostics.hpp"

namespace aegis {

template <typename T>
using Expected = std::expected<T, Error>;

// AEGIS_TRY(out_name, expected_expr) — single-branch success path.
//
//   Expected<int> parse_int(std::string_view s);
//
//   Expected<int> compute(std::string_view a, std::string_view b) {
//       AEGIS_TRY(x, parse_int(a));    // binds `x` on success
//       AEGIS_TRY(y, parse_int(b));    // binds `y` on success
//       return x + y;
//   }
//
// On the success path the compiler emits essentially a MOV + branch-likely
// prediction: the error handler lives in a separate, cold code section.
#define AEGIS_TRY(name, expr)                                               \
    auto _try_result_##name = (expr);                                       \
    if (!_try_result_##name.has_value()) [[unlikely]] {                     \
        return std::unexpected(_try_result_##name.error());                 \
    }                                                                       \
    auto name = std::move(*_try_result_##name);                             \
    (void)0

// Simpler form that doesn't bind a name — useful for fire-and-forget calls.
#define AEGIS_TRY_VOID(expr)                  \
    do {                                      \
        auto _r = (expr);                     \
        if (!_r.has_value()) [[unlikely]] {   \
            return std::unexpected(_r.error()); \
        }                                     \
    } while (0)

} // namespace aegis
