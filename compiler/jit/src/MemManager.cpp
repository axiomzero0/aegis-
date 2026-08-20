// jit/MemManager.cpp — Real RWX page manager + epoch-based reclamation.
//
// Law (Section §C.4 "Epoch-Based Reclamation"):
//   "Old JIT code and IR nodes are reclaimed using epoch-based garbage
//    collection. When the optimizer replaces a Node, the old node is
//    tagged with an epoch. Once all threads advance past that epoch,
//    the memory is bulk-freed."
//
// Implementation:
//   - allocate(): mmap MAP_PRIVATE | MAP_ANONYMOUS with PROT_READ |
//     PROT_WRITE | PROT_EXEC on Linux, VirtualAlloc PAGE_EXECUTE_READWRITE
//     on Windows.
//   - release(): mark the page as `in_use = false` and stamp its epoch.
//   - reclaim_old_pages(): for each page whose epoch < global_epoch_,
//     munmap/VirtualFree it.
//
// RWX is a security-sensitive operation; on Linux this requires
// either setting no_new_privs (PR_SET_NO_NEW_PRIVS) and/or using
// memfd_secret + userfaultfd. For the prototype we use plain mmap
// with RWX.
#include "aegis/jit/MemManager.hpp"

#include <algorithm>
#include <cstring>

#include "aegis/backend/TargetConstants.hpp"
#include "aegis/jit/JitConstants.hpp"

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace aegis::jit {

MemManager::~MemManager() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& p : pages_) {
        if (p.base == nullptr) continue;
#if defined(__linux__) || defined(__APPLE__)
        ::munmap(p.base, p.size);
#elif defined(_WIN32)
        ::VirtualFree(p.base, 0, MEM_RELEASE);
#endif
    }
}

void* MemManager::allocate(size_t size, uint64_t epoch) {
    std::lock_guard<std::mutex> lk(mutex_);
    // Law: Rule 66 — No Assumption of Stable Hardware. The page size
    // is queried at runtime via sysconf() (Linux/macOS) or
    // GetSystemInfo() (Windows). We do NOT hard-code 4096 — Apple
    // Silicon uses 16384-byte pages, ARM64 Linux can use 64K pages.
#if defined(__linux__) || defined(__APPLE__)
    long pagesize = ::sysconf(_SC_PAGESIZE);
    // Fallback if sysconf fails (returns -1). Use the documented
    // default from TargetConstants (4096 on most current x86-64/ARM64).
    if (pagesize <= 0) {
        pagesize = static_cast<long>(backend::constants::kDefaultPageSize);
    }
    size_t aligned_size = (size + static_cast<size_t>(pagesize) - 1)
                          & ~(static_cast<size_t>(pagesize) - 1);
    void* mem = ::mmap(nullptr, aligned_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (mem == MAP_FAILED) return nullptr;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t aligned_size = (size + si.dwPageSize - 1)
                          & ~(static_cast<size_t>(si.dwPageSize) - 1);
    void* mem = ::VirtualAlloc(nullptr, aligned_size,
                              MEM_COMMIT | MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
    if (mem == nullptr) return nullptr;
#else
    (void)size; (void)epoch;
    return nullptr;
#endif
    Page p{mem, aligned_size, epoch, true};
    pages_.push_back(p);
    return mem;
}

void MemManager::release(void* ptr, uint64_t epoch) noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& p : pages_) {
        if (p.base == ptr) {
            p.in_use = false;
            p.epoch = epoch;
            return;
        }
    }
}

void MemManager::reclaim_old_pages() {
    std::lock_guard<std::mutex> lk(mutex_);
    uint64_t cur = global_epoch_.load(std::memory_order_acquire);
    auto it = std::remove_if(pages_.begin(), pages_.end(),
        [&](const Page& p) {
            if (p.in_use) return false;
            if (p.epoch >= cur) return false;
            // Reclaim this page.
#if defined(__linux__) || defined(__APPLE__)
            ::munmap(p.base, p.size);
#elif defined(_WIN32)
            ::VirtualFree(p.base, 0, MEM_RELEASE);
#endif
            return true;
        });
    pages_.erase(it, pages_.end());
}

} // namespace aegis::jit
