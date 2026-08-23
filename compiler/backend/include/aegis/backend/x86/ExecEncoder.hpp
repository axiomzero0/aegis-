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
#include <string>
#include <vector>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/RegAlloc/LinearScan.hpp"

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
inline constexpr uint16_t kExecHomeRegCount{12};

} // namespace aegis::backend::x86
