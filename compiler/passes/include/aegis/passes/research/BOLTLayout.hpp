// passes/research/BOLTLayout.hpp — Machine Code Layout Optimization (BOLT-style).
// ============================================================
// Law (Section §II Post-Link / Binary Optimization):
//   "Machine Code Layout Optimization (BOLT-style): Reorders final
//    binary machine code pages based on PGO. Groups hot functions,
//    separates cold functions. Reduces I-TLB misses."
// ============================================================
#pragma once
#include <vector>
#include "aegis/backend/MachineIR.hpp"
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::research {
class BOLTLayoutPass : public Pass {
public:
    BOLTLayoutPass() : Pass("bolt_layout") {}
    int run(Graph& g, const PassBudget& budget) override;
    // Post-link version: operates on MachineFunction list.
    int run_on_machine_funcs(std::vector<aegis::MachineFunction>& mfs) noexcept;
};
} // namespace aegis::passes::research
