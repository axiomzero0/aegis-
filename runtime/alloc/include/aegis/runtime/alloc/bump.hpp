// aegis/runtime/alloc/bump.hpp — BumpAllocator (Rule C.2 thread-local).
#pragma once
#include <cstddef>
#include <cstdint>
#include "aegis/runtime/alloc/allocator.hpp"
namespace aegis::runtime::alloc {

// Bump allocator: linear allocation, no per-node free. Used by mutator
// threads for thread-local temporary storage (Rule C.2).
class BumpAllocator final : public Allocator {
public:
    explicit BumpAllocator(std::size_t capacity);
    ~BumpAllocator() override;
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align) override;
    void deallocate(void*, std::size_t) noexcept override {}
    void reset() noexcept;
private:
    void*       base_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};
};

} // namespace aegis::runtime::alloc
