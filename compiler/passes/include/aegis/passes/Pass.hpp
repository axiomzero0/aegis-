// ============================================================
// passes/Pass.h — Base class for optimization passes.
// ============================================================
// Laws enforced:
//   B.5 — Every Pass must be Idempotent. Running the same pass twice
//         produces the identical IR.
//   B.6 — Every Pass must be Monotonic Decreasing in IR Size. A pass
//         either reduces node count or moves the IR closer to a normal
//         form. Passes that may grow the IR (e.g., Loop Unrolling, SLP)
//         must run inside a guarded fixpoint with a strict budget.
//   Rule 47 — No aggressive pass without a cost model. Passes that
//         can grow the IR or speculate must have a budget.
//   Rule 42 — Graph verifier runs in debug builds after every pass.
//   Rule 40 — Replay logs retained for all CI failures. The Pass
//         carries a name and version stamp so failures can be replayed.
// ============================================================
#pragma once
#include <string>
#include "aegis/ir/Graph.hpp"
#include "aegis/passes/PassConstants.hpp"

namespace aegis {

// Compile-time modes (Rule A.1: One Pipeline, Two Inputs).
enum class CompileMode : uint8_t {
    AOT,  // Static IR + default heuristics; speculative passes require proof.
    JIT,  // Static IR + PGO data; speculative passes use guards.
};

// Pass budget: the "Regulator" (Rule 47) decides whether a pass may
// run, may grow the IR, may speculate, etc.
//
// Law: Rule 61 — every numeric default comes from PassConstants.hpp
// (no magic numbers).
struct PassBudget {
    uint32_t max_nodes_growth{0};  // 0 = pass must not grow the IR
    uint32_t max_runtime_ms{passes::constants::kDefaultMaxRuntimeMs};
    bool     allow_speculation{false};
    bool     pgo_available{false};
    CompileMode mode{CompileMode::AOT};
};

class Pass {
public:
    explicit Pass(std::string name) : name_(std::move(name)) {}
    virtual ~Pass() = default;

    // Run the pass. Returns the number of nodes changed/removed (for
    // fixpoint detection). Returns -1 on internal failure (which is
    // surfaced to the DiagnosticSink and skips the rest of the pipeline).
    virtual int run(Graph& g, const PassBudget& budget) = 0;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] uint64_t version() const noexcept { return version_; }
    void set_version(uint64_t v) noexcept { version_ = v; }

    // Idempotency check (Rule B.5). Default returns true; passes that
    // mutate state in a non-idempotent way should override this.
    [[nodiscard]] virtual bool is_idempotent() const noexcept { return true; }

private:
    std::string name_;
    uint64_t    version_{0};
};

} // namespace aegis
