// ============================================================
// aegis/backend/TargetConstants.hpp — Named target/hardware constants.
// ============================================================
// Laws:
//   Rule 66 (No Assumption of Stable Hardware) — these are DEFAULTS
//   only; the Target interface MUST be queried at runtime.
//   Rule D.1 (No Magic Numbers) — every cache/SIMD/latency constant
//   has a name + rationale.
// ============================================================
#pragma once

#include <cstdint>

namespace aegis::backend::constants {

/// Default cache line size (bytes). x86-64 + ARM64 both use 64-byte
/// cache lines today. The Target interface reports the actual value
/// at runtime; this is the fallback for AOT builds where the target
/// wasn't pinned at compile time.
constexpr uint32_t kDefaultCacheLineSize{64};

/// Default maximum SIMD width (bytes). AVX-512 = 64, AVX2/AVX = 32,
/// SSE = 16. The Target interface reports the actual value; this is
/// the conservative fallback (SSE-class).
constexpr uint32_t kDefaultMaxSimdWidth{16};

/// Default page size (bytes). Linux x86-64 = 4096, ARM64 = 4096 or
/// 65536 (Apple Silicon uses 16384). The runtime queries sysconf()
/// or VirtualQuery() to get the real value; this is the fallback.
constexpr uint32_t kDefaultPageSize{4096};

/// Default stack alignment (bytes) at function call boundaries.
/// System V AMD64 + AAPCS64 both mandate 16-byte alignment at the
/// call site (8-byte on function entry after the return address push).
constexpr uint32_t kDefaultStackAlignment{16};

/// Default red-zone size (bytes). x86-64 SysV has a 128-byte red
/// zone; ARM64 + Windows x64 have none. The Target interface reports
/// the real value per-calling-convention.
constexpr uint32_t kDefaultRedZoneSize{128};

/// Maximum number of GPRs any supported target exposes. x86-64 = 16,
/// ARM64 = 31 (X0-X30, with X31/SP), RISC-V = 32. This is an upper
/// bound for array sizing; the Target interface reports the actual
/// count per RegClass.
constexpr uint32_t kMaxGprsAnyTarget{32};

/// Maximum number of FPRs any supported target exposes.
constexpr uint32_t kMaxFprsAnyTarget{32};

// ---- Calling convention: System V AMD64 ----
namespace sysv {
    constexpr uint32_t kNumArgGprs{6};     // RDI, RSI, RDX, RCX, R8, R9
    constexpr uint32_t kNumArgFprs{8};     // XMM0-XMM7
    constexpr uint32_t kStackAlignmentBytes{16};
    constexpr uint32_t kRedZoneBytes{128};
    constexpr uint32_t kCalleeSavedGprsCount{6}; // RBX, RBP, R12-R15
} // namespace sysv

// ---- Calling convention: Windows x64 ----
namespace win {
    constexpr uint32_t kNumArgGprs{4};     // RCX, RDX, R8, R9
    constexpr uint32_t kNumArgFprs{4};     // XMM0-XMM3
    constexpr uint32_t kStackAlignmentBytes{16};
    constexpr uint32_t kRedZoneBytes{0};  // No red zone on Windows x64.
    constexpr uint32_t kCalleeSavedGprsCount{7}; // RBX, RBP, RDI, RSI, R12-R15
    constexpr uint32_t kShadowSpaceBytes{32};    // 32-byte shadow space
} // namespace win


// ---- x86-64 register file shape (Rule 66: defaults; Target queries at runtime) ----
namespace x86_64 {
    /// Architectural GPR count (RAX-R15). Includes RSP/RBP which are
    /// reserved by the frame lowering; the allocator sees fewer.
    constexpr uint32_t kNumGprs{16};
    /// XMM0-XMM15 (AVX-256 lower halves). YMM/ZMM reuse the same
    /// register encoding, so the count is identical.
    constexpr uint32_t kNumVectorRegs{16};
    /// Spill slot size for one GPR (64-bit slot).
    constexpr uint32_t kGprSpillSlotBytes{8};
    /// Spill slot size for one XMM register (128-bit).
    constexpr uint32_t kXmmSpillSlotBytes{16};
    /// Spill slot size for one YMM register (256-bit).
    constexpr uint32_t kYmmSpillSlotBytes{32};
} // namespace x86_64

/// Stride in bytes of one simplified FDE record in the .eh_frame
/// section as emitted by the prototype unwinder (EHTables.cpp):
/// a fixed 24-byte record per function (length + CIE pointer +
/// function start). Real DWARF CFI varies; this pins the prototype's
/// deterministic layout so unwind_info_offset math stays greppable.
constexpr uint32_t kEhFrameEntryStrideBytes{24};

} // namespace aegis::backend::constants
