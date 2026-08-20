// ============================================================
// aegis/jit/JitEngine.hpp — Hotness tracking + JIT compilation.
// ============================================================
// Law (Section §C "Memory & Threading Laws"):
//   C.1 — "Mutator Threads Never Block on JIT".
//   C.3 — "Compiler Threads Never Block on Mutator State".
//   C.4 — "Epoch-Based Reclamation".
//
// The JitEngine tracks per-function execution counters. When a
// counter crosses a threshold, the engine enqueues a compile job on
// a background thread. The compile job runs the unified pipeline in
// JIT mode (PGO + guards). When done, a safe-point patch atomically
// swaps the function pointer.
// ============================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "aegis/jit/JitConstants.hpp"

namespace aegis::jit {

// Function entry point type (the calling convention for compiled code).
using CompiledFn = void (*)();

struct JitStats {
    std::atomic<uint64_t> compiles_started{0};
    std::atomic<uint64_t> compiles_finished{0};
    std::atomic<uint64_t> deoptimizations{0};
    std::atomic<uint64_t> total_compiled_bytes{0};
};

class JitEngine {
public:
    JitEngine() = default;
    ~JitEngine();

    JitEngine(const JitEngine&) = delete;
    JitEngine& operator=(const JitEngine&) = delete;

    // Register an AOT-compiled function so the engine can monitor its
    // hotness and trigger JIT recompilation when needed.
    //
    // Law: Rule 61 — the default threshold comes from JitConstants.hpp
    // (kDefaultHotThreshold), not a magic 1000.
    void register_function(uint64_t fn_id, CompiledFn aot_entry,
                           uint32_t hot_threshold = constants::kDefaultHotThreshold);

    // Called by the AOT code's prologue on each invocation to bump the
    // invocation counter. MUST be inlined + branchless on the fast path
    // — typically a single `inc; cmp; jmp` sequence.
    void on_call(uint64_t fn_id) noexcept;

    // Returns the entry point to call (AOT or JIT, depending on whether
    // the JIT version is ready). Mutator threads call this for every
    // dispatch; the lookup is a single hash-table read.
    [[nodiscard]] CompiledFn entry_for(uint64_t fn_id) const noexcept;

    // Apply the safe-point patch that swaps the function pointer. Called
    // by the background compiler thread once the JIT version is ready.
    void install_jitted(uint64_t fn_id, CompiledFn jitted_entry) noexcept;

    [[nodiscard]] const JitStats& stats() const noexcept { return stats_; }

private:
    struct Entry {
        std::atomic<CompiledFn> current;       // pointer to AOT or JIT code
        std::atomic<uint64_t>   invocations;   // hotness counter
        uint32_t                 threshold;
    };
    struct CompileJob {
        uint64_t    fn_id;
        CompiledFn aot_entry;
    };

    std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries_;
    std::mutex                   queue_mutex_;
    std::condition_variable     queue_cv_;
    std::deque<CompileJob>      pending_;
    std::atomic<bool>           shutdown_{false};
    std::thread                 worker_;
    JitStats                    stats_;
};

} // namespace aegis::jit
