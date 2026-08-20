// aegis/runtime/alloc/system.hpp — SystemAllocator (malloc/free wrapper).
#pragma once
#include <cstdlib>
#include "aegis/runtime/alloc/allocator.hpp"
namespace aegis::runtime::alloc {

// The default OS heap allocator. Wraps malloc/free. Used only when no
// other allocator is provided — the IR uses MonoArena; collections take
// an explicit allocator.
class SystemAllocator final : public Allocator {
public:
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) override {
#ifdef _WIN32
        return _aligned_malloc(bytes, align);
#else
        return std::aligned_alloc(align, bytes);
#endif
    }
    void deallocate(void* ptr, std::size_t) noexcept override {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }
};

} // namespace aegis::runtime::alloc
