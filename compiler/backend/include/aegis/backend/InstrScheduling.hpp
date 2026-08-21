// backend/InstrScheduling.hpp — Reorder instructions to avoid pipeline stalls.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Instruction Scheduling: Reorders instructions to avoid pipeline
//    stalls."
// ============================================================
#pragma once
#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/Target.hpp"
namespace aegis::backend {
class InstructionScheduler {
public:
    InstructionScheduler(MachineFunction& mf, const Target& target)
        : mf_(mf), target_(target) {}
    // Returns the number of instruction reorderings performed.
    int run() noexcept;
private:
    MachineFunction& mf_;
    const Target&    target_;
};
} // namespace aegis::backend
