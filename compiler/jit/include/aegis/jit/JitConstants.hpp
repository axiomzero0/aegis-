// ============================================================
// aegis/jit/JitConstants.hpp — Named thresholds for the JIT engine.
// ============================================================
// Law: Rule 61 (No Hard-Coded Constants in Optimization Logic) +
//      Rule 65 (No Silent Fallbacks Without Telemetry).
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace aegis::jit::constants {

/// Default hot threshold for JIT compilation. A function that exceeds
/// this many invocations is enqueued for JIT recompilation. The value
/// is conservative (1000 invocations is roughly the cost of one
/// compile job at ~1ms each) — tunable via PGO.
constexpr uint32_t kDefaultHotThreshold{1000};

/// Maximum size of the JIT compile job queue. If the queue is full,
/// new jobs are dropped silently (with telemetry, Rule 65) — the
/// mutator thread continues on the AOT path.
constexpr uint32_t kMaxPendingJobs{1024};

/// Initial allocation size for the first JIT code page. Subsequent
/// allocations grow on demand.
constexpr uint64_t kJitCodeInitialPageSize{64 * 1024}; // 64 KiB

/// Maximum total JIT code memory before the MemManager forces a
/// full epoch reclaim. Prevents unbounded growth.
constexpr uint64_t kJitCodeMaxTotalBytes{256 * 1024 * 1024}; // 256 MiB

/// Number of epochs the MemManager will keep pages before reclaiming.
/// Higher = more memory used, lower = more frequent reclamation.
constexpr uint32_t kMemManagerEpochRetention{4};

/// Telemetry counter IDs for the JIT. These are stable indices into
/// the Profiler's counter array, allocated at JitEngine init.
constexpr uint32_t kTelemetryCompilesStarted{0};
constexpr uint32_t kTelemetryCompilesFinished{1};
constexpr uint32_t kTelemetryDeopts{2};
constexpr uint32_t kTelemetryQueueFullSkips{3};
constexpr uint32_t kTelemetryTotalCompiledBytes{4};
constexpr uint32_t kTelemetryCount{5};

} // namespace aegis::jit::constants
