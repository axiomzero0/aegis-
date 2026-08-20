// aegis/runtime/sync/atomic.hpp — std::atomic wrappers (Crowded effect).
#pragma once
#include <atomic>
#include <type_traits>
#include "aegis/runtime/core/basic_types.hpp"
namespace aegis::runtime::sync {

enum class Ordering : uint8_t {
    Relaxed = 0,
    Acquire = 1,
    Release = 2,
    AcqRel  = 3,
    SeqCst  = 4,
};

template <typename T>
class Atomic {
public:
    Atomic() = default;
    explicit Atomic(T v) : value_(v) {}
    T load(Ordering o = Ordering::SeqCst) const noexcept {
        return value_.load(to_std_memory_order(o));
    }
    void store(T v, Ordering o = Ordering::SeqCst) noexcept {
        value_.store(v, to_std_memory_order(o));
    }
    // SFINAE-disable fetch_add / fetch_sub for non-integral T (e.g. bool).
    template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U>>>
    T fetch_add(T v, Ordering o = Ordering::SeqCst) noexcept {
        return value_.fetch_add(v, to_std_memory_order(o));
    }
    template <typename U = T, typename = std::enable_if_t<std::is_integral_v<U>>>
    T fetch_sub(T v, Ordering o = Ordering::SeqCst) noexcept {
        return value_.fetch_sub(v, to_std_memory_order(o));
    }
    bool compare_exchange_strong(T& expected, T desired,
                                Ordering success = Ordering::SeqCst,
                                Ordering failure = Ordering::SeqCst) noexcept {
        return value_.compare_exchange_strong(expected, desired,
                                              to_std_memory_order(success),
                                              to_std_memory_order(failure));
    }
private:
    static constexpr std::memory_order to_std_memory_order(Ordering o) noexcept {
        switch (o) {
            case Ordering::Relaxed: return std::memory_order_relaxed;
            case Ordering::Acquire: return std::memory_order_acquire;
            case Ordering::Release: return std::memory_order_release;
            case Ordering::AcqRel:  return std::memory_order_acq_rel;
            case Ordering::SeqCst:  return std::memory_order_seq_cst;
        }
        return std::memory_order_seq_cst;
    }
    std::atomic<T> value_{};
};

} // namespace aegis::runtime::sync
