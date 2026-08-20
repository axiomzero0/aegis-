// ============================================================
// aegis/jit/MemManager.hpp — RWX memory page management for JIT code.
// ============================================================
// Law (Section §C.4 "Epoch-Based Reclamation"):
//   "Old JIT code and IR nodes are reclaimed using epoch-based garbage
//    collection. When the optimizer replaces a Node, the old node is
//    tagged with an epoch. Once all threads advance past that epoch,
//    the memory is bulk-freed."
//
// The MemManager allocates RWX (read-write-execute) memory pages for
// JIT-compiled code, tracks epoch tags, and reclaims pages when all
// mutator threads have advanced past the page's epoch.
// ============================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace aegis::jit {

class MemManager {
public:
    MemManager() = default;
    ~MemManager();

    MemManager(const MemManager&) = delete;
    MemManager& operator=(const MemManager&) = delete;

    // Allocate a buffer of `size` bytes with RWX permissions. The
    // returned memory is aligned to a page boundary. The `epoch` is
    // stamped onto the page; the page becomes reclaimable once all
    // mutator threads have advanced past `epoch`.
    [[nodiscard]] void* allocate(size_t size, uint64_t epoch);

    // Mark a buffer as no longer used. The buffer is queued for
    // reclamation and will be freed once the global epoch advances past
    // its stamp.
    void release(void* ptr, uint64_t epoch) noexcept;

    // Bump the global epoch. Called by the runtime when mutator threads
    // have advanced past a safe point.
    void advance_epoch() noexcept { ++global_epoch_; }

    // Reclaim all pages whose epoch < global_epoch_.
    void reclaim_old_pages();

private:
    struct Page {
        void*    base;
        size_t   size;
        uint64_t epoch;
        bool     in_use;
    };
    std::vector<Page> pages_;
    std::mutex        mutex_;
    std::atomic<uint64_t> global_epoch_{1};
};

} // namespace aegis::jit
