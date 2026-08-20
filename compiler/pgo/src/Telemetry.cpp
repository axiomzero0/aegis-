// pgo/Telemetry.cpp — Real telemetry sink implementation.
//
// Law (Rule 65 — No Silent Fallbacks Without Telemetry):
//   Every fallback / deopt / skip path MUST emit a telemetry event.
//   The event is counted atomically; in debug builds it's also
//   written to stderr.
//
// Law (Rule 73 — No Fragile Implementations):
//   The event counts are stored in a fixed-size atomic array indexed
//   by the TelemetryEvent enum. No bounds check is needed because
//   the enum is closed and the array is sized to its cardinality.
#include "aegis/pgo/Telemetry.hpp"

#include <cstdio>

#ifdef AEGIS_DEBUG
#include "aegis/runtime/io/io.hpp"
#endif

namespace aegis::pgo {

namespace {
const char* event_name(TelemetryEvent ev) noexcept {
    switch (ev) {
        case TelemetryEvent::JitFallbackToAot:           return "jit_fallback_to_aot";
        case TelemetryEvent::JitGuardFailed:             return "jit_guard_failed";
        case TelemetryEvent::JitQueueFullSkipped:        return "jit_queue_full_skipped";
        case TelemetryEvent::JitCodeCacheFull:           return "jit_code_cache_full";
        case TelemetryEvent::PassBudgetExceeded:         return "pass_budget_exceeded";
        case TelemetryEvent::PassNodeCountOverflow:      return "pass_node_count_overflow";
        case TelemetryEvent::PassAliasAnalysisFailed:    return "pass_alias_analysis_failed";
        case TelemetryEvent::RegAllocSpillOverflow:      return "regalloc_spill_overflow";
        case TelemetryEvent::RegAllocNoFreeReg:          return "regalloc_no_free_reg";
        case TelemetryEvent::VerifierFailed:             return "verifier_failed";
        case TelemetryEvent::ProfileDataVersionMismatch: return "profile_version_mismatch";
        case TelemetryEvent::ProfileDataCorrupt:         return "profile_data_corrupt";
    }
    return "<unknown>";
}
} // namespace

void TelemetrySink::emit(TelemetryEvent ev, std::string_view detail) noexcept {
    // Hot path: atomic increment. Branch-free, lock-free.
    counts_[static_cast<size_t>(ev)].fetch_add(1, std::memory_order_relaxed);
#ifdef AEGIS_DEBUG
    // Debug-only: log to stderr via the io syscall (no allocation, no
    // stdio buffering — keeps the telemetry path allocation-free even
    // when verbose).
    char buf[256];
    int n = std::snprintf(buf, sizeof(buf),
                          "[aegis-telemetry] %s: %.*s\n",
                          event_name(ev),
                          static_cast<int>(detail.size()),
                          detail.data());
    if (n > 0) {
        aegis::runtime::io::write_stderr(buf, static_cast<size_t>(n));
    }
#else
    (void)detail;
#endif
}

} // namespace aegis::pgo
