// ============================================================
// aegis/pgo/Profiler.hpp — Instrumentation insertion & runtime counters.
// ============================================================
// Law (Rule 46 — "No profile data without confidence"):
//   "Profile data must include sample count, stability, age, decay,
//    variance, and deopt correlation (Meter). Low-confidence data must
//    not trigger aggressive speculation."
//
// The Profiler inserts instrumentation into AOT-compiled code that bumps
// counters at branch points, call sites, and lock acquisitions. The
// counters are written to a profile file that's consumed by the JIT
// on the next compilation.
// ============================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace aegis::pgo {

// Per-counter confidence metadata (Rule 46's "Meter").
struct ProfileConfidence {
    uint32_t sample_count{0};
    uint32_t stability{0};   // 0..100 (how often the same branch was taken)
    uint32_t age{0};         // profile age in JIT compilations
    uint32_t decay{0};       // multiplicative decay factor applied per compilation
    uint32_t variance{0};    // variance across samples (for branches with multiple edges)
    uint32_t deopt_correlation{0}; // number of deopts correlated with this counter
};

struct Counter {
    std::atomic<uint64_t> hits{0};
    ProfileConfidence    confidence{};
};

class Profiler {
public:
    Profiler() = default;

    // Register a new counter slot and return its index. Used by the
    // AOT compiler to know where to emit `inc` instructions.
    [[nodiscard]] uint32_t register_counter();

    // Per-thread accessor: mutator threads bump counters at runtime.
    [[nodiscard]] Counter& counter(uint32_t id) { return *counters_[id]; }
    [[nodiscard]] const Counter& counter(uint32_t id) const { return *counters_[id]; }

    // Serialize all counters to a flat buffer (for profile dump on shutdown).
    [[nodiscard]] std::vector<uint8_t> serialize() const;

    // Load counters from a flat buffer (consumed by the JIT on startup).
    void deserialize(const std::vector<uint8_t>& bytes);

    [[nodiscard]] size_t num_counters() const noexcept { return counters_.size(); }

private:
    // unique_ptr because std::atomic is non-movable, and vector growth
    // requires movable elements.
    std::vector<std::unique_ptr<Counter>> counters_{};
};

} // namespace aegis::pgo
