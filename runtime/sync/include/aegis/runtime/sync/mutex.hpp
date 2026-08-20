// aegis/runtime/sync/mutex.hpp — Mutex + scoped lock.
#pragma once
#include <mutex>
namespace aegis::runtime::sync {

class Mutex {
public:
    void lock() { m_.lock(); }
    bool try_lock() { return m_.try_lock(); }
    void unlock() { m_.unlock(); }
private:
    std::mutex m_;
};

class ScopedLock {
public:
    explicit ScopedLock(Mutex& m) : m_(m) { m_.lock(); }
    ~ScopedLock() { m_.unlock(); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
private:
    Mutex& m_;
};

} // namespace aegis::runtime::sync
