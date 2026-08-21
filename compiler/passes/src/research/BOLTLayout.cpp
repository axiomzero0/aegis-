// passes/research/BOLTLayout.cpp — Post-link machine-code layout optimization.
//
// Law (Section §II Post-Link / Binary Optimization):
//   "Machine Code Layout Optimization (BOLT-style): Reorders final
//    binary machine code pages based on PGO."
//
// Algorithm (BOLT, Panchenko et al. '19, simplified):
//   1. For each function, compute a hotness score from PGO data
//      (call frequency + callee hotness).
//   2. Sort functions by hotness descending.
//   3. Group hot functions together (top of the .text section) so
//      they fit in fewer I-TLB pages. Push cold functions to the
//      bottom.
//
// Law: Rule 50 — versioned; the layout is invalidated when the PGO
// profile is regenerated.
// Law: Rule 61 — kBoltMinFunctionSize bounds the heuristic.
//
// For the prototype we sort the MachineFunction list by name length
// as a placeholder for the real hotness-based sort.
#include "aegis/passes/research/BOLTLayout.hpp"

#include <algorithm>

#include "aegis/backend/MachineIR.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::passes::research {

int BOLTLayoutPass::run(Graph& g, const PassBudget& budget) {
    // Graph-level pass is a no-op for BOLT (it's post-link).
    // The real work happens in run_on_machine_funcs.
    (void)g; (void)budget;
    return 0;
}

int BOLTLayoutPass::run_on_machine_funcs(
        std::vector<aegis::MachineFunction>& mfs) noexcept {
    if (mfs.empty()) return 0;
    // Filter out tiny functions (kBoltMinFunctionSize threshold).
    size_t eligible = 0;
    for (const auto& mf : mfs) {
        if (mf.instrs.size() >= constants::kBoltMinFunctionSize) ++eligible;
    }
    if (eligible == 0) {
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::PassBudgetExceeded,
            "pass=bolt_layout reason=no_eligible_functions");
        return 0;
    }
    // Sort by instruction count descending (placeholder for the real
    // hotness-based sort that would consult PGO). Larger functions
    // first — they're more likely to be hot in practice.
    std::sort(mfs.begin(), mfs.end(),
              [](const aegis::MachineFunction& a,
                 const aegis::MachineFunction& b) {
                  return a.instrs.size() > b.instrs.size();
              });
    return static_cast<int>(eligible);
}

} // namespace aegis::passes::research
