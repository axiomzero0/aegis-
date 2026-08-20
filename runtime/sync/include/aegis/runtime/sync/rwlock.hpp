// aegis/runtime/sync/rwlock.hpp — Read-write lock.
#pragma once
#include <shared_mutex>
namespace aegis::runtime::sync {

class RwLock {
public:
    void read_lock()   { m_.lock_shared(); }
    void read_unlock() { m_.unlock_shared(); }
    void write_lock()   { m_.lock(); }
    void write_unlock() { m_.unlock(); }
private:
    std::shared_mutex m_;
};

} // namespace aegis::runtime::sync
