// runtime/sync/src/mutex.cpp — Mutex + ScopedLock (header-only wrapper around std::mutex).
//
// On Linux/macOS, std::mutex wraps pthread_mutex_t. On Windows, it
// wraps the SRWLOCK primitive. Both are futex-based and uncontended
// acquire is a single atomic CAS.
//
// Law (Rule C.2): thread-local allocations must not block; this mutex
// is only used for explicit cross-thread synchronization (channels,
// global state, JIT code-cache updates).
#include "aegis/runtime/sync/mutex.hpp"
namespace aegis::runtime::sync {
// Mutex is fully inline in the header.
} // namespace aegis::runtime::sync
