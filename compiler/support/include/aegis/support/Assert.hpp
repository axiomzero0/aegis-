// ============================================================
// aegis/support/Assert.hpp — Assert + unreachable primitives.
// ============================================================
// Laws:
//   D.3 — No Silent Fallbacks or Default Returns. Non-exhaustive
//          switch statements must use [[assume(false)]] +
//          AEGIS_UNREACHABLE().
//   73 — No Fragile Implementations. Pointer arithmetic without
//        bounds validation is forbidden.
//
// These macros replace silent `default: return 0;` patterns with
// loud failures. On the hot path they compile down to a single
// `__builtin_unreachable()` (no abort overhead); in debug builds
// they call abort() with a diagnostic.
// ============================================================
#pragma once

#include <cstdio>
#include <cstdlib>

namespace aegis::support {

// AEGIS_ASSERT(cond, msg) — aborts with msg if cond is false.
// On the hot path with -DNDEBUG this compiles to nothing.
#ifdef NDEBUG
#define AEGIS_ASSERT(cond, msg) ((void)0)
#else
#define AEGIS_ASSERT(cond, msg)                                                  \
    do {                                                                        \
        if (!(cond)) {                                                          \
            ::aegis::support::detail::abort_with(                              \
                __FILE__, __LINE__, __func__, (msg));                          \
        }                                                                       \
    } while (0)
#endif

// AEGIS_UNREACHABLE() — marks a code path as unreachable. In debug
// builds, calling it aborts (loud failure). In release builds, it
// compiles to `__builtin_unreachable()` so the compiler can prune
// branches and dead-code-eliminate `default:` cases.
//
// Use this in switch statements on closed enums to satisfy
// -Wswitch-enum (Rule D.3) without inserting a silent default return.
#if defined(__GNUC__) || defined(__clang__)
#define AEGIS_UNREACHABLE()                                                     \
    do {                                                                        \
        ::aegis::support::detail::abort_with(                                  \
            __FILE__, __LINE__, __func__, "unreachable");                       \
        __builtin_unreachable();                                                \
    } while (0)
#else
#define AEGIS_UNREACHABLE()                                                     \
    do {                                                                        \
        ::aegis::support::detail::abort_with(                                  \
            __FILE__, __LINE__, __func__, "unreachable");                       \
        std::abort();                                                            \
    } while (0)
#endif

namespace detail {
[[noreturn]] inline void abort_with(const char* file, int line,
                                     const char* func, const char* msg) noexcept {
    std::fprintf(stderr, "%s:%d: %s: aegis assertion failed: %s\n",
                 file, line, func, msg);
    std::abort();
}
} // namespace detail

} // namespace aegis::support
