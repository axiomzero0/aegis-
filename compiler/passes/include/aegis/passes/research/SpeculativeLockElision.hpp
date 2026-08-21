// passes/research/SpeculativeLockElision.hpp — SLE.
// ============================================================
// Law (Section §II Advanced Vectorization & Parallelization):
//   "Speculative Lock Elision (SLE): Replaces HTM. The compiler
//    identifies critical sections protected by mutexes. If PGO shows
//    low contention, it inlines the critical section and guards it
//    with an atomic version counter. If the counter changes
//    (contention detected), it deopts to the standard lock path."
// ============================================================
#pragma once
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class SpeculativeLockElisionPass : public Pass {
public:
    SpeculativeLockElisionPass() : Pass("speculative_lock_elision") {}
    int run(Graph& g, const PassBudget& budget) override;
};
} // namespace aegis::passes::research
