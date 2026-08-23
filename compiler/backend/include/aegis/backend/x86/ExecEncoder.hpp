// ============================================================
// aegis/backend/x86/ExecEncoder.hpp — Encode MachineFunction to
// executable x86-64 machine code (in-memory, JIT-ready).
// ============================================================
// Laws:
//   Rule D.1/D.2 — every opcode byte, shift, and mask is a NAMED
//   constant (declarative encoding table below; values from the
//   Intel SDM Vol. 2). No magic numbers in encoding logic.
//   Rule D.3     — any unsupported construct (spills, calls, memory
//   ops, register-amount shifts, >6 params) FAILS LOUDLY with a
//   reason; there is no silent fallback encoding.
//   Rule 73      — the encoder validates every operand's register
//   assignment before writing a byte.
//
// Register model for the executable path:
//   - 12 HOMES for vregs: RCX, RSI, RDI, R8-R11, RBX, R12-R15. The
//     homes overlap the SysV argument registers, so parameters enter
//     through a CYCLE-SAFE PARALLEL MOVE (RAX scratch) — see
//     ExecEncoder.cpp; no entry move can clobber an unread argument.
//   - RAX + RDX are NEVER homes: they are implicit operands of
//     idiv/div (and hold the return value at exit), so keeping them
//     scratch-only makes every division sequence safe without the
//     register allocator knowing about clobbers.
//   - RSP/RBP are reserved (stack discipline).
//   - Parameters enter in the SysV argument registers and are moved
//     to their homes by `param` pseudo-instructions.
// ============================================================
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/RegAlloc/LinearScan.hpp"
#include "aegis/support/StringIntern.hpp"

namespace aegis::backend::x86 {

// Encode one machine function into executable bytes.
//
// Precondition: `ra` has allocated `fn` with at most kExecHomeRegCount
// GPRs and produced ZERO spills (spill slots are not implemented in
// the executable path — failing loudly here is the contract, the
// caller asserts spills==0 first).
//
// Returns true on success. On failure returns false and fills `err`
// with the offending instruction + reason (Rule D.3).
[[nodiscard]] bool encode_executable(const MachineFunction& fn,
                                     const LinearScanAllocator& ra,
                                     std::vector<uint8_t>& out,
                                     std::string& err);

// The number of GPR homes the executable path supports. Pass this as
// the allocator's num_gpr so preg ids line up with the home table.
// Homes 0..6 are caller-saved (clobbered by calls); pass 7 as the
// allocator's callee_saved_from when the function contains calls.
inline constexpr uint16_t kExecHomeRegCount{13};
/// First home index whose register survives a call (callee-saved):
/// RBX, RBP, R12-R15 — six homes (RBP is callee-saved in SysV and the
/// generated code never needs it as a frame pointer).
inline constexpr uint16_t kExecFirstCalleeSaved{7};

// One function in a linked executable module.
struct ModuleFunction {
    SymbolId symbol{kInvalidSymbolId};   // callee identity
    const MachineFunction* mf{nullptr};
    const LinearScanAllocator* ra{nullptr};
};

// Encode + LINK a whole module into one executable image: functions
// are laid out contiguously (prologues first to last), each `call`'s
// rel32 is patched to its callee's image offset. Calls to symbols not
// in the module fail loudly. On success `entry_offsets` holds each
// function's byte offset (same order as `fns`).
[[nodiscard]] bool encode_module(std::span<const ModuleFunction> fns,
                                 std::vector<uint8_t>& out,
                                 std::vector<size_t>& entry_offsets,
                                 std::string& err);

} // namespace aegis::backend::x86
