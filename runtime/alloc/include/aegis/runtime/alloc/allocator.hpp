// aegis/runtime/alloc/allocator.hpp — The Allocator interface.
// ============================================================
// Law (Section §2 std.mem):
//   Allocator: The core interface. All collections and allocations require
//   an allocator.
// ============================================================
#pragma once
#include <cstddef>
namespace aegis::runtime::alloc {

class Allocator {
public:
    virtual ~Allocator() = default;
    [[nodiscard]] virtual void* allocate(std::size_t bytes, std::size_t align) = 0;
    virtual void deallocate(void* ptr, std::size_t bytes) noexcept = 0;
};

} // namespace aegis::runtime::alloc
