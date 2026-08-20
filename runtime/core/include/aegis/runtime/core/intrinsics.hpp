// ============================================================
// aegis/runtime/core/intrinsics.hpp — Compiler intrinsics exposed to Aegis.
// ============================================================
// Law: Section §2 (std.mem, std.math, std.simd, std.atomic, std.io,
//       std.thread, std.time, std.fmt, std.collections) — all
//       functions in std.math are Pure; all atomic operations are
//       Crowded; all I/O operations are Crowded.
// ============================================================
#pragma once

#include <cstdint>

namespace aegis::runtime::core {

// Panic — aborts the program. Used as the only "exception" path (Rule B.1).
[[noreturn]] void panic(const char* message);

// Memcpy / memmove / memset — the only memory primitives exposed.
void memcpy_bytes(void* dst, const void* src, size_t n);
void memmove_bytes(void* dst, const void* src, size_t n);
void memset_bytes(void* dst, uint8_t value, size_t n);

} // namespace aegis::runtime::core
