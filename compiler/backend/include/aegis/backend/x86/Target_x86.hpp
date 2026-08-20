// backend/x86/Target_x86.hpp — x86-64 target implementation.
// ============================================================
// Implements the abstract Target interface for x86-64. Provides:
//   - System V AMD64 calling convention (Linux/macOS/BSD)
//   - Windows x64 calling convention (Windows)
//   - Register file: 16 GPRs (RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
//     R8-R15), 16 XMMs (XMM0-XMM15).
// ============================================================
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aegis/backend/Target.hpp"

namespace aegis::backend::x86 {

// x86-64 GPR ids. Stable.
enum class GPR : uint16_t {
    RAX = 0,  RCX = 1,  RDX = 2,  RBX = 3,
    RSP = 4,  RBP = 5,  RSI = 6,  RDI = 7,
    R8  = 8,  R9  = 9,  R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
};

// XMM register ids.
enum class XMM : uint16_t {
    XMM0 = 0,  XMM1 = 1,  XMM2 = 2,  XMM3 = 3,
    XMM4 = 4,  XMM5 = 5,  XMM6 = 6,  XMM7 = 7,
    XMM8 = 8,  XMM9 = 9,  XMM10 = 10, XMM11 = 11,
    XMM12 = 12, XMM13 = 13, XMM14 = 14, XMM15 = 15,
};

class TargetX8664 final : public Target {
public:
    TargetX8664();
    [[nodiscard]] Arch                arch() const noexcept override { return Arch::X86_64; }
    [[nodiscard]] std::string_view     name() const noexcept override { return "x86_64"; }

    [[nodiscard]] uint16_t            num_regs(RegClass rc) const noexcept override;
    [[nodiscard]] uint16_t            spill_slot_size(RegClass rc) const noexcept override;
    [[nodiscard]] const CallingConvention& sysv_cc() const noexcept override    { return sysv_; }
    [[nodiscard]] const CallingConvention& windows_cc() const noexcept override { return win_; }

private:
    // Storage for the std::span<const uint16_t> objects in
    // CallingConvention. The vectors keep their data alive for the
    // lifetime of the Target.
    std::vector<uint16_t> sysv_arg_regs_;
    std::vector<uint16_t> sysv_arg_fpregs_;
    std::vector<uint16_t> sysv_callee_saved_;
    std::vector<uint16_t> sysv_caller_saved_;
    std::vector<uint16_t> win_arg_regs_;
    std::vector<uint16_t> win_arg_fpregs_;
    std::vector<uint16_t> win_callee_saved_;
    std::vector<uint16_t> win_caller_saved_;
    CallingConvention    sysv_{};
    CallingConvention    win_{};
};

} // namespace aegis::backend::x86
