// tests/unit/test_telemetry.cpp — Tests for the telemetry event sink (Rule D.9).
//
// Law: Rule 65 — every fallback path emits telemetry. These tests
// verify the telemetry sink works correctly so downstream consumers
// (PGO feedback loops, debug logs) can rely on the data.
#include <cassert>
#include <iostream>
#include <string>

#include "aegis/pgo/Telemetry.hpp"

namespace {

using namespace aegis::pgo;

int test_emit_increments_count() {
    TelemetrySink::instance().reset();
    TelemetrySink::instance().emit(TelemetryEvent::JitGuardFailed, "test1");
    TelemetrySink::instance().emit(TelemetryEvent::JitGuardFailed, "test2");
    TelemetrySink::instance().emit(TelemetryEvent::JitGuardFailed, "test3");
    assert(TelemetrySink::instance().count(TelemetryEvent::JitGuardFailed) == 3);
    return 0;
}

int test_different_events_counted_separately() {
    TelemetrySink::instance().reset();
    TelemetrySink::instance().emit(TelemetryEvent::JitGuardFailed, "a");
    TelemetrySink::instance().emit(TelemetryEvent::PassBudgetExceeded, "b");
    TelemetrySink::instance().emit(TelemetryEvent::JitQueueFullSkipped, "c");
    TelemetrySink::instance().emit(TelemetryEvent::JitGuardFailed, "d");
    assert(TelemetrySink::instance().count(TelemetryEvent::JitGuardFailed) == 2);
    assert(TelemetrySink::instance().count(TelemetryEvent::PassBudgetExceeded) == 1);
    assert(TelemetrySink::instance().count(TelemetryEvent::JitQueueFullSkipped) == 1);
    return 0;
}

int test_reset_clears_all_counters() {
    TelemetrySink::instance().emit(TelemetryEvent::VerifierFailed, "x");
    TelemetrySink::instance().emit(TelemetryEvent::RegAllocSpillOverflow, "y");
    TelemetrySink::instance().reset();
    for (size_t i = 0;
         i <= static_cast<size_t>(TelemetryEvent::ProfileDataCorrupt);
         ++i) {
        assert(TelemetrySink::instance().count(
            static_cast<TelemetryEvent>(i)) == 0);
    }
    return 0;
}

} // namespace

int main() {
    test_emit_increments_count();
    test_different_events_counted_separately();
    test_reset_clears_all_counters();
    std::cout << "telemetry tests passed (assertions OK)\n";
    return 0;
}
