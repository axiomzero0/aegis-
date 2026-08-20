// passes/PassManager.cpp — Runs the pipeline.
#include "aegis/passes/PassManager.hpp"

#include <iostream>

#ifdef AEGIS_VERIFY_IR
#include "aegis/ir/Verifier.hpp"
#include "aegis/ir/Printer.hpp"
#endif

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
#ifdef AEGIS_VERIFY_IR
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
        // Rule 42 violation — print the broken graph and exit.
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
