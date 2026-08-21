// backend/Peephole.hpp — Local pattern matching on final instruction stream.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Peephole Optimization: Local pattern matching on final
//    instruction stream."
// ============================================================
#pragma once
#include "aegis/backend/MachineIR.hpp"
namespace aegis::backend {
class PeepholeOptimizer {
public:
    explicit PeepholeOptimizer(MachineFunction& mf) : mf_(mf) {}
    // Returns the number of peephole rewrites applied.
    int run() noexcept;
private:
    MachineFunction& mf_;
};
} // namespace aegis::backend
