// passes/PassManager.h — Drives the optimization pipeline (Rules A.1, B.5, B.6, 42).
#pragma once
#include <vector>
#include "aegis/ir/Graph.hpp"
#include "aegis/passes/Pass.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis {

class PassManager {
public:
    explicit PassManager(Graph& g) : g_(g) {}

    // Register a pass. Takes ownership.
    void add(std::unique_ptr<Pass> p);

    // Run all passes in order, applying Rule 42 (verify after every pass
    // in debug builds) and Rule B.5/B.6 (idempotency + monotonic budget).
    // Returns the total number of nodes removed across all passes.
    int run(CompileMode mode);

    // Push a verifier snapshot (Rule 40: replay logs retained for all CI
    // failures). This is a no-op in release builds.
    void maybe_verify(const Pass& p);

private:
    Graph& g_;
    std::vector<std::unique_ptr<Pass>> passes_{};
    // Law: Rule 61 — uses a named constant from PassConstants.hpp,
    // not a magic 5. The constant is uint32_t; we cast to int (the
    // field type). Using `=` instead of `{}` allows the implicit
    // narrowing conversion.
    int fixpoint_budget_per_pass_ =
        static_cast<int>(passes::constants::kFixpointBudgetPerPass);
};

} // namespace aegis
