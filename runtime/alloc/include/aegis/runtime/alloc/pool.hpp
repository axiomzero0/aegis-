// aegis/runtime/alloc/pool.hpp — PoolAllocator for fixed-size objects.
#pragma once
#include <cstddef>
#include <vector>
#include "aegis/runtime/alloc/allocator.hpp"
namespace aegis::runtime::alloc {

class PoolAllocator final : public Allocator {
public:
    PoolAllocator(std::size_t object_size, std::size_t initial_capacity);
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) override;
    void deallocate(void* ptr, std::size_t bytes) noexcept override;
private:
    std::size_t object_size_;
    std::vector<void*> free_list_;
    std::vector<void*> chunks_;
};

} // namespace aegis::runtime::alloc
