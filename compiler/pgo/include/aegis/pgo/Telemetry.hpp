// ============================================================
// aegis/pgo/Telemetry.hpp — Telemetry event sink for fallbacks + deopts.
// ============================================================
// Law (Rule 65 — No Silent Fallbacks Without Telemetry):
//   "When the JIT falls back to AOT, when a speculative guard fails,
//    when a pass skips an optimization due to budget/proof failure,
//    or when regalloc spills excessively, the event MUST be recorded
//    in telemetry/profile data."
//
// Every fallback path emits an event via TelemetrySink::emit(). The
// sink writes to a Profiler counter (so the next PGO run can detect
// "this function deopts constantly" patterns) AND to stderr in
// debug builds (so the developer can see what's happening).
//
// Usage:
//   TelemetrySink::instance().emit(TelemetryEvent::JitFallbackToAot,
//                                  "function_id=42");
// ============================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace aegis::pgo {

enum class TelemetryEvent : uint8_t {
    // ---- JIT / AOT fallback paths ----
    JitFallbackToAot          = 0,  // JIT compilation failed; fall back to AOT.
    JitGuardFailed           = 1,  // speculative guard tripped; deoptimize.
    JitQueueFullSkipped       = 2,  // compile queue full; job dropped.
    JitCodeCacheFull          = 3,  // code cache full; reclaim needed.
    // ---- Pass skip paths ----
    PassBudgetExceeded        = 4,  // pass ran past its wall-clock budget.
    PassNodeCountOverflow     = 5,  // graph too large for the pass.
    PassAliasAnalysisFailed   = 6,  // CFL-Alias couldn't prove; skip reordering.
    // ---- Pass success paths (Rule 65: wins are observable too) ----
    PassOptimized             = 12, // pass performed a profitable rewrite.
    // ---- Register allocator ----
    RegAllocSpillOverflow     = 7,  // spilled more than 25% of vregs.
    RegAllocNoFreeReg         = 8,  // no free PReg available.
    // ---- Verifier ----
    VerifierFailed            = 9,  // Rule 42 graph verifier caught a defect.
    // ---- Profiler ----
    ProfileDataVersionMismatch = 10, // Rule 50 cache mismatch.
    ProfileDataCorrupt        = 11, // deserialization failed.
};

class TelemetrySink {
public:
    static TelemetrySink& instance() noexcept {
        static TelemetrySink s;
        return s;
    }

    // Emit a telemetry event. In debug builds the event is also written
    // to stderr with the supplied detail string. In release builds the
    // event is only counted (zero overhead on the hot path).
    void emit(TelemetryEvent ev, std::string_view detail) noexcept;

    // Test accessor — read a counter without resetting.
    [[nodiscard]] uint64_t count(TelemetryEvent ev) const noexcept {
        return counts_[static_cast<size_t>(ev)].load(std::memory_order_relaxed);
    }

    // Reset all counters (test-only).
    void reset() noexcept {
        for (auto& c : counts_) c.store(0, std::memory_order_relaxed);
    }

private:
    TelemetrySink() = default;
    std::atomic<uint64_t> counts_[12]{};
};

} // namespace aegis::pgo
