// passes/PassManager.cpp — Runs the pipeline.
//
// Law (Rule 65 — No Silent Fallbacks Without Telemetry):
//   When a pass exceeds its fixpoint budget (a fallback — the pass is
//   non-idempotent or stuck), the PassManager emits a telemetry event
//   so the failure is observable rather than silent.
//
// Law (Rule 42 — Graph verifier runs in debug after every pass):
//   On verifier failure we emit a VerifierFailed telemetry event
//   AND abort loudly (no silent fallback to corrupted IR).
#include "aegis/passes/PassManager.hpp"

#include <iostream>
#include <string>

#include "aegis/ir/Printer.hpp"
#include "aegis/ir/Verifier.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis {

void PassManager::add(std::unique_ptr<Pass> p) {
    passes_.push_back(std::move(p));
}

int PassManager::run(CompileMode mode) {
    int total_removed = 0;
    PassBudget budget{};
    budget.mode = mode;
    budget.pgo_available = (mode == CompileMode::JIT);
    budget.allow_speculation = (mode == CompileMode::JIT);

    for (auto& p : passes_) {
        // Rule B.5: idempotency fixpoint. Run until stable or until
        // fixpoint_budget_per_pass_ iterations are exceeded.
        int iters = 0;
        int last_changed = 0;
        do {
            last_changed = p->run(g_, budget);
            ++iters;
            if (iters > fixpoint_budget_per_pass_) {
                // Rule 65: emit telemetry, not a silent warning.
                std::string detail = "pass=";
                detail += p->name();
                detail += " iters=";
                detail += std::to_string(iters);
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::PassBudgetExceeded,
                    detail);
#ifdef AEGIS_DEBUG
                std::cerr << "AEGIS WARNING: pass '" << p->name()
                          << "' exceeded fixpoint budget; possible non-idempotency.\n";
#endif
                break;
            }
        } while (last_changed > 0 && p->is_idempotent());

        total_removed += last_changed;
        maybe_verify(*p);
    }
    return total_removed;
}

void PassManager::maybe_verify(const Pass& p) {
#ifdef AEGIS_VERIFY_IR
    std::string why;
    if (!verify_graph(g_, why)) {
        // Rule 42 violation — emit telemetry (Rule 65), print the broken
        // graph, and abort. No silent fallback to corrupted IR.
        std::string detail = "pass=";
        detail += p.name();
        detail += " reason=";
        detail += why;
        pgo::TelemetrySink::instance().emit(
            pgo::TelemetryEvent::VerifierFailed, detail);
        std::cerr << "AEGIS VERIFY FAILED after pass '" << p.name() << "': "
                  << why << "\n";
        std::cerr << "----- Graph dump -----\n";
        std::cerr << format_graph(g_);
        std::cerr << "----------------------\n";
        std::abort();
    }
#else
    (void)p;
#endif
}

} // namespace aegis
