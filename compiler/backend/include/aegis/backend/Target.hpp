// ============================================================
// aegis/backend/Target.hpp — Abstract target interface (x86, ARM).
// ============================================================
// Law: Section §II Backend & Low-Level —
//   "Instruction Selection (BURS/DAG Combiner): Maps SoN nodes to
//    target machine instructions."
//
// The Target interface abstracts the per-architecture details that the
// backend passes need: register file shape, calling convention, memory
// operand constraints, instruction encoding entry points.
// ============================================================
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "aegis/support/Primitives.hpp"

namespace aegis::backend {

enum class Arch : uint8_t {
    X86_64,
    ARM64,
};

enum class RegClass : uint8_t {
    General = 0, // GPR
    Float   = 1, // FP/SIMD (XMM on x86-64, V on ARM64)
    Vector  = 2, // SIMD-only (YMM/ZMM, SVE)
};

// Calling-convention descriptor. Used by the backend to map function
// signature -> (input reg slots, output reg, callee-saved set).
struct CallingConvention {
    std::span<const uint16_t> arg_regs;        // GPR arg slots (e.g. rdi,rsi,rdx,rcx,r8,r9 on x86-64 SysV)
    std::span<const uint16_t> arg_fpregs;      // FP arg slots
    uint16_t                  ret_reg;         // return value register
    uint16_t                  ret_fpreg;
    std::span<const uint16_t> callee_saved;    // registers the callee must preserve
    std::span<const uint16_t> caller_saved;    // registers the caller must save
    uint8_t                   stack_alignment; // bytes
    bool                      red_zone;       // 128-byte red zone available?
};

class Target {
public:
    virtual ~Target() = default;
    [[nodiscard]] virtual Arch                arch() const noexcept = 0;
    [[nodiscard]] virtual std::string_view     name() const noexcept = 0;
    [[nodiscard]] virtual uint16_t            num_regs(RegClass rc) const noexcept = 0;
    [[nodiscard]] virtual uint16_t            spill_slot_size(RegClass rc) const noexcept = 0;
    [[nodiscard]] virtual const CallingConvention& sysv_cc() const noexcept = 0;  // System V / AAPCS64
    [[nodiscard]] virtual const CallingConvention& windows_cc() const noexcept = 0; // MS x64 / ARM64 Windows
};

} // namespace aegis::backend
