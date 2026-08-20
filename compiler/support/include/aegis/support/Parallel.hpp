// ============================================================
// aegis/support/Parallel.hpp — std::execution wrappers for parallel passes.
// ============================================================
// Law: Section §C "Memory & Threading Laws" — compiler threads never
//       block on mutator state. Parallel passes must execute on a frozen
//       snapshot of the IR.
//
// This header exposes a thin wrapper over C++17 std::execution (C++26
// finalized the algorithms). For TUs built with C++ compilers that don't
// yet ship std::execution, the wrappers fall back to serial execution
// (preserving correctness at the cost of throughput).
// ============================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <execution>
#include <span>
#include <vector>

namespace aegis::support {

// Parallel for-each over a contiguous range. Falls back to serial
// execution when the policy isn't supported.
template <typename T, typename F>
void parallel_for_each(std::span<T> range, F&& f) {
    if (range.empty()) return;
    // For small ranges the overhead of thread coordination dominates.
    if (range.size() < 1024) {
        for (T& x : range) f(x);
        return;
    }
    std::for_each(std::execution::par_unseq, range.begin(), range.end(),
                  std::forward<F>(f));
}

// Parallel transform with serial fallback.
template <typename In, typename Out, typename F>
void parallel_transform(std::span<const In> in, std::span<Out> out, F&& f) {
    if (in.size() != out.size()) return;
    if (in.size() < 1024) {
        for (size_t i = 0; i < in.size(); ++i) out[i] = f(in[i]);
        return;
    }
    std::transform(std::execution::par_unseq,
                   in.begin(), in.end(), out.begin(),
                   std::forward<F>(f));
}

// Parallel reduce.
template <typename T, typename F, typename Init = T>
T parallel_reduce(std::span<const T> range, Init init, F&& f) {
    if (range.empty()) return static_cast<T>(init);
    if (range.size() < 1024) {
        T acc = static_cast<T>(init);
        for (const T& x : range) acc = f(acc, x);
        return acc;
    }
    return std::reduce(std::execution::par_unseq,
                       range.begin(), range.end(),
                       static_cast<T>(init), std::forward<F>(f));
}

} // namespace aegis::support
