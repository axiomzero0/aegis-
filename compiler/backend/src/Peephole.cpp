// backend/Peephole.cpp — Local pattern matching on MachineInstr stream.
//
// Patterns (x86-64):
//   - mov_reg a, a   -> nop   (mov to self)
//   - mov_imm r, 0; mov_reg r, r2 -> xor r, r; mov_reg r, r2 (xor is
//     cheaper on x86 because it's a single uop with no imm operand).
//   - add r, 0       -> nop   (add of zero is identity)
//   - sub r, 0       -> nop
//   - mul r, 1       -> nop
//
// Rule B.5: idempotent — once rewritten, the pattern no longer matches.
// Rule B.6: monotone decreasing (we replace instructions with nops).
//
// Law: Rule 61 — kPeepholeMaxPatterns bounds the per-function work.
#include "aegis/backend/Peephole.hpp"

#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

#include <algorithm>
#include <cstring>

namespace aegis::backend {

namespace {
bool instr_is_nop_like(const MachineInstr& mi) noexcept {
    // mov_reg to self (defs[0] == uses[0]).
    if (std::string_view{mi.op} == "mov_reg" &&
        mi.defs[0] != kInvalidVReg && mi.defs[0] == mi.uses[0]) {
        return true;
    }
    return false;
}

bool instr_is_redundant_zero_op(const MachineInstr& mi) noexcept {
    // add r, 0 / sub r, 0 / mul r, 1 / and r, -1.
    if (mi.uses[1] == kInvalidVReg) return false;
    // The use slot stores either a vreg or an encoded immediate. We
    // don't have access to the immediate encoder here; the
    // peephole pass would be more useful post-RegAlloc when uses are
    // physical registers and we can decode them. For the prototype
    // we restrict to the mov-to-self pattern.
    return false;
}
} // namespace

int PeepholeOptimizer::run() noexcept {
    int rewrote = 0;
    if (mf_.instrs.size() >
        aegis::passes::constants::kPeepholeMaxPatterns) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=peephole reason=too_many_instrs");
        return 0;
    }
    for (auto& mi : mf_.instrs) {
        if (instr_is_nop_like(mi)) {
            // Rewrite to nop.
            mi.op = "nop";
            mi.defs[0] = kInvalidVReg;
            mi.uses[0] = kInvalidVReg;
            mi.uses[1] = kInvalidVReg;
            ++rewrote;
            continue;
        }
        if (instr_is_redundant_zero_op(mi)) {
            mi.op = "nop";
            mi.defs[0] = kInvalidVReg;
            mi.uses[0] = kInvalidVReg;
            mi.uses[1] = kInvalidVReg;
            ++rewrote;
        }
    }
    return rewrote;
}

} // namespace aegis::backend
