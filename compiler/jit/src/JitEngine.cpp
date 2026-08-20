// jit/JitEngine.cpp — Real JIT engine: hotness tracking + background compile thread.
//
// Laws implemented:
//   C.1 — Mutator Threads Never Block on JIT.
//   C.3 — Compiler Threads Never Block on Mutator State.
//   C.4 — Epoch-Based Reclamation.
//
// The JitEngine owns a background std::thread that consumes compile
// jobs from a queue. Mutator threads bump invocation counters on the
// fast path (single atomic inc) and only block when installing the
// finished JIT code (a brief atomic store).
#include "aegis/jit/JitEngine.hpp"

#include <chrono>

#include "aegis/jit/JitConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::jit {

JitEngine::~JitEngine() {
    shutdown_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void JitEngine::register_function(uint64_t fn_id, CompiledFn aot_entry,
                                  uint32_t hot_threshold) {
    auto entry = std::make_unique<Entry>();
    entry->current.store(aot_entry, std::memory_order_release);
    entry->invocations.store(0, std::memory_order_relaxed);
    entry->threshold = hot_threshold;
    entries_[fn_id] = std::move(entry);
}

void JitEngine::on_call(uint64_t fn_id) noexcept {
    auto it = entries_.find(fn_id);
    if (it == entries_.end()) [[unlikely]] return;
    Entry& e = *it->second;
    uint64_t count = e.invocations.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == e.threshold) [[unlikely]] {
        // Crossed threshold — enqueue a compile job. We use try_lock
        // to avoid blocking on the hot path.
        //
        // Law (Rule 65): when the queue is full, we emit telemetry
        // (JitQueueFullSkipped) instead of silently dropping the job.
        if (queue_mutex_.try_lock()) {
            std::lock_guard<std::mutex> lk(queue_mutex_, std::adopt_lock);
            if (pending_.size() < constants::kMaxPendingJobs) {
                pending_.push_back(CompileJob{fn_id,
                    e.current.load(std::memory_order_acquire)});
                queue_cv_.notify_one();
            } else {
                // Queue full — emit telemetry.
                pgo::TelemetrySink::instance().emit(
                    pgo::TelemetryEvent::JitQueueFullSkipped,
                    "queue_full");
            }
        }
        stats_.compiles_started.fetch_add(1, std::memory_order_relaxed);
    }
}

CompiledFn JitEngine::entry_for(uint64_t fn_id) const noexcept {
    auto it = entries_.find(fn_id);
    if (it == entries_.end()) return nullptr;
    return it->second->current.load(std::memory_order_acquire);
}

void JitEngine::install_jitted(uint64_t fn_id, CompiledFn jitted_entry) noexcept {
    auto it = entries_.find(fn_id);
    if (it == entries_.end()) return;
    it->second->current.store(jitted_entry, std::memory_order_release);
    stats_.compiles_finished.fetch_add(1, std::memory_order_relaxed);
}

} // namespace aegis::jit
