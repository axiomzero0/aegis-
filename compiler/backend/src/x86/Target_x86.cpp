// backend/x86/Target_x86.cpp — x86-64 target implementation.
//
// Initializes the System V AMD64 and Windows x64 calling conventions
// and provides the register-file shape that the Linear Scan allocator
// uses to assign physical registers.
#include "aegis/backend/x86/Target_x86.hpp"

#include "aegis/backend/TargetConstants.hpp"

namespace aegis::backend::x86 {

TargetX8664::TargetX8664() {
    // ---- System V AMD64 ----
    sysv_arg_regs_ = {
        static_cast<uint16_t>(GPR::RDI), static_cast<uint16_t>(GPR::RSI),
        static_cast<uint16_t>(GPR::RDX), static_cast<uint16_t>(GPR::RCX),
        static_cast<uint16_t>(GPR::R8),  static_cast<uint16_t>(GPR::R9)
    };
    sysv_arg_fpregs_ = {
        static_cast<uint16_t>(XMM::XMM0), static_cast<uint16_t>(XMM::XMM1),
        static_cast<uint16_t>(XMM::XMM2), static_cast<uint16_t>(XMM::XMM3),
        static_cast<uint16_t>(XMM::XMM4), static_cast<uint16_t>(XMM::XMM5),
        static_cast<uint16_t>(XMM::XMM6), static_cast<uint16_t>(XMM::XMM7)
    };
    sysv_callee_saved_ = {
        static_cast<uint16_t>(GPR::RBX), static_cast<uint16_t>(GPR::RBP),
        static_cast<uint16_t>(GPR::R12), static_cast<uint16_t>(GPR::R13),
        static_cast<uint16_t>(GPR::R14), static_cast<uint16_t>(GPR::R15)
    };
    sysv_caller_saved_ = {
        static_cast<uint16_t>(GPR::RAX), static_cast<uint16_t>(GPR::RCX),
        static_cast<uint16_t>(GPR::RDX), static_cast<uint16_t>(GPR::RSI),
        static_cast<uint16_t>(GPR::RDI), static_cast<uint16_t>(GPR::R8),
        static_cast<uint16_t>(GPR::R9),  static_cast<uint16_t>(GPR::R10),
        static_cast<uint16_t>(GPR::R11)
    };
    sysv_.arg_regs       = sysv_arg_regs_;
    sysv_.arg_fpregs     = sysv_arg_fpregs_;
    sysv_.ret_reg        = static_cast<uint16_t>(GPR::RAX);
    sysv_.ret_fpreg      = static_cast<uint16_t>(XMM::XMM0);
    sysv_.callee_saved   = sysv_callee_saved_;
    sysv_.caller_saved   = sysv_caller_saved_;
    sysv_.stack_alignment = constants::sysv::kStackAlignmentBytes;
    sysv_.red_zone        = true;

    // ---- Windows x64 ----
    win_arg_regs_ = {
        static_cast<uint16_t>(GPR::RCX), static_cast<uint16_t>(GPR::RDX),
        static_cast<uint16_t>(GPR::R8),  static_cast<uint16_t>(GPR::R9)
    };
    win_arg_fpregs_ = {
        static_cast<uint16_t>(XMM::XMM0), static_cast<uint16_t>(XMM::XMM1),
        static_cast<uint16_t>(XMM::XMM2), static_cast<uint16_t>(XMM::XMM3)
    };
    win_callee_saved_ = {
        static_cast<uint16_t>(GPR::RBX), static_cast<uint16_t>(GPR::RBP),
        static_cast<uint16_t>(GPR::RDI), static_cast<uint16_t>(GPR::RSI),
        static_cast<uint16_t>(GPR::R12), static_cast<uint16_t>(GPR::R13),
        static_cast<uint16_t>(GPR::R14), static_cast<uint16_t>(GPR::R15)
    };
    win_caller_saved_ = {
        static_cast<uint16_t>(GPR::RAX), static_cast<uint16_t>(GPR::RCX),
        static_cast<uint16_t>(GPR::RDX), static_cast<uint16_t>(GPR::R8),
        static_cast<uint16_t>(GPR::R9),  static_cast<uint16_t>(GPR::R10),
        static_cast<uint16_t>(GPR::R11)
    };
    win_.arg_regs       = win_arg_regs_;
    win_.arg_fpregs     = win_arg_fpregs_;
    win_.ret_reg        = static_cast<uint16_t>(GPR::RAX);
    win_.ret_fpreg      = static_cast<uint16_t>(XMM::XMM0);
    win_.callee_saved   = win_callee_saved_;
    win_.caller_saved   = win_caller_saved_;
    win_.stack_alignment = constants::win::kStackAlignmentBytes;
    win_.red_zone        = false;
}

uint16_t TargetX8664::num_regs(RegClass rc) const noexcept {
    switch (rc) {
        case RegClass::General: return static_cast<uint16_t>(constants::x86_64::kNumGprs);
        case RegClass::Float:   return static_cast<uint16_t>(constants::x86_64::kNumVectorRegs);
        // YMM/ZMM share the XMM register encoding, so the count is
        // identical (Rule 66: shape comes from the constants table,
        // not from literals in logic).
        case RegClass::Vector:  return static_cast<uint16_t>(constants::x86_64::kNumVectorRegs);
    }
    return static_cast<uint16_t>(constants::x86_64::kNumGprs);
}

uint16_t TargetX8664::spill_slot_size(RegClass rc) const noexcept {
    switch (rc) {
        case RegClass::General: return static_cast<uint16_t>(constants::x86_64::kGprSpillSlotBytes);
        case RegClass::Float:   return static_cast<uint16_t>(constants::x86_64::kXmmSpillSlotBytes);
        case RegClass::Vector:  return static_cast<uint16_t>(constants::x86_64::kYmmSpillSlotBytes);
    }
    return static_cast<uint16_t>(constants::x86_64::kGprSpillSlotBytes);
}

} // namespace aegis::backend::x86
