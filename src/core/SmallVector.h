// ============================================================
// core/SmallVector.h — Inline-storage vector for the 1-to-4 case.
// ============================================================
// Law: Rule 57 — "Ban std::vector for data that usually has 1 to 4 elements.
//       For Use-Def chains, instruction operands, and basic block
//       predecessors/successors, use a SmallVector<T, N>."
//
// This SmallVector stores N elements inline (SBO); spills to a heap/arena
// buffer when grown past N. The growth path uses `::operator new` via
// `std::allocator_traits` so it is replaceable in arena builds.
// ============================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace aegis {

template <typename T, size_t kInlineCapacity = 4>
class SmallVector {
public:
    static_assert(kInlineCapacity > 0, "SmallVector inline capacity must be > 0.");
    using value_type      = T;
    using size_type       = uint32_t;
    using difference_type = std::ptrdiff_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;

    constexpr SmallVector() noexcept = default;

    explicit SmallVector(size_type n) { resize(n); }
    SmallVector(size_type n, const T& v) { resize(n, v); }
    SmallVector(std::initializer_list<T> il) {
        if (il.size() > capacity()) grow(il.size());
        for (const auto& x : il) emplace_back(x);
    }

    SmallVector(const SmallVector& o) {
        if (o.size() > capacity()) grow(o.size());
        for (const auto& x : o) emplace_back(x);
    }
    SmallVector(SmallVector&& o) noexcept {
        if (o.is_inline()) {
            for (size_type i = 0; i < o.size_; ++i) {
                ::new (inline_ptr() + i) T(std::move(o.inline_ptr()[i]));
            }
            size_ = o.size_;
            o.size_ = 0;
        } else {
            // Steal remote buffer wholesale.
            remote_data_ = o.remote_data_;
            remote_cap_  = o.remote_cap_;
            size_         = o.size_;
            o.remote_data_ = nullptr;
            o.remote_cap_  = 0;
            o.size_         = 0;
        }
    }

    SmallVector& operator=(const SmallVector& o) {
        if (this != &o) {
            clear();
            if (o.size() > capacity()) grow(o.size());
            for (const auto& x : o) emplace_back(x);
        }
        return *this;
    }
    SmallVector& operator=(SmallVector&& o) noexcept {
        if (this != &o) {
            destroy_all();
            if (o.is_inline()) {
                for (size_type i = 0; i < o.size_; ++i) {
                    ::new (inline_ptr() + i) T(std::move(o.inline_ptr()[i]));
                }
                size_ = o.size_;
                o.size_ = 0;
            } else {
                if (!is_inline()) free_remote();
                remote_data_ = o.remote_data_;
                remote_cap_  = o.remote_cap_;
                size_         = o.size_;
                o.remote_data_ = nullptr;
                o.remote_cap_  = 0;
                o.size_         = 0;
            }
        }
        return *this;
    }

    ~SmallVector() { destroy_all(); }

    // ---- Capacity / size ----
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr size_type capacity() const noexcept {
        return is_inline() ? static_cast<size_type>(kInlineCapacity) : remote_cap_;
    }

    void reserve(size_type n) {
        if (n > capacity()) grow(n);
    }
    void resize(size_type n) {
        if (n < size_) {
            for (size_type i = n; i < size_; ++i) pop_back_nodestroy();
            size_ = n;
        } else {
            reserve(n);
            for (size_type i = size_; i < n; ++i) {
                ::new (data() + i) T{};
            }
            size_ = n;
        }
    }
    void resize(size_type n, const T& v) {
        if (n < size_) {
            for (size_type i = n; i < size_; ++i) pop_back_nodestroy();
            size_ = n;
        } else {
            reserve(n);
            for (size_type i = size_; i < n; ++i) {
                ::new (data() + i) T(v);
            }
            size_ = n;
        }
    }

    // ---- Element access ----
    [[nodiscard]] pointer       data() noexcept {
        return is_inline() ? inline_ptr() : remote_data_;
    }
    [[nodiscard]] const_pointer data() const noexcept {
        return is_inline() ? inline_ptr() : remote_data_;
    }
    [[nodiscard]] reference       operator[](size_type i) noexcept { return data()[i]; }
    [[nodiscard]] const_reference operator[](size_type i) const noexcept { return data()[i]; }
    [[nodiscard]] reference       front() noexcept { return data()[0]; }
    [[nodiscard]] const_reference front() const noexcept { return data()[0]; }
    [[nodiscard]] reference       back() noexcept { return data()[size_ - 1]; }
    [[nodiscard]] const_reference back() const noexcept { return data()[size_ - 1]; }

    [[nodiscard]] std::span<T>       as_span() noexcept { return {data(), size_}; }
    [[nodiscard]] std::span<const T> as_span() const noexcept { return {data(), size_}; }

    // Iterators (range-for compatible).
    [[nodiscard]] pointer       begin()        noexcept { return data(); }
    [[nodiscard]] const_pointer begin()  const noexcept { return data(); }
    [[nodiscard]] pointer       end()          noexcept { return data() + size_; }
    [[nodiscard]] const_pointer end()    const noexcept { return data() + size_; }

    // ---- Mutators ----
    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if (size_ == capacity()) [[unlikely]] grow_for_one();
        ::new (data() + size_) T(std::forward<Args>(args)...);
        ++size_;
        return data()[size_ - 1];
    }
    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }

    void pop_back() noexcept {
        [[assume(size_ > 0)]];
        --size_;
        data()[size_].~T();
    }
    void pop_back_nodestroy() noexcept {
        [[assume(size_ > 0)]];
        --size_;
    }

    void clear() noexcept {
        destroy_all();
        size_ = 0;
    }

    void erase_unordered(size_type i) noexcept {
        [[assume(i < size_)]];
        if (i + 1 != size_) {
            data()[i] = std::move(data()[size_ - 1]);
        }
        pop_back();
    }

    // Find + erase the first occurrence of v (linear; for small N).
    bool remove_first(const T& v) noexcept {
        for (size_type i = 0; i < size_; ++i) {
            if (data()[i] == v) {
                erase_unordered(i);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool contains(const T& v) const noexcept {
        for (size_type i = 0; i < size_; ++i) {
            if (data()[i] == v) return true;
        }
        return false;
    }

private:
    // Inline SBO storage — uses std::byte storage to avoid alignment pitfalls.
    alignas(T) std::byte inline_storage_[sizeof(T) * kInlineCapacity]{};
    T*       remote_data_{nullptr};
    size_type remote_cap_{0};
    size_type size_{0};

    [[nodiscard]] constexpr bool is_inline() const noexcept { return remote_cap_ == 0; }
    [[nodiscard]] constexpr T* inline_ptr() noexcept {
        return std::launder(reinterpret_cast<T*>(inline_storage_));
    }
    [[nodiscard]] constexpr const T* inline_ptr() const noexcept {
        return std::launder(reinterpret_cast<const T*>(inline_storage_));
    }

    void destroy_all() noexcept {
        T* p = data();
        for (size_type i = 0; i < size_; ++i) p[i].~T();
    }

    void free_remote() noexcept {
        if (remote_data_) {
            std::allocator<T> alloc;
            alloc.deallocate(remote_data_, remote_cap_);
            remote_data_ = nullptr;
            remote_cap_ = 0;
        }
    }

    void grow_for_one() {
        grow(std::max<size_type>(capacity() * 2, capacity() + 4));
    }

    void grow(size_type new_cap) {
        if (new_cap <= capacity()) return;
        // Allocate a new remote buffer, move-construct existing elements in.
        std::allocator<T> alloc;
        T* new_buf = alloc.allocate(new_cap);
        T* old = data();
        for (size_type i = 0; i < size_; ++i) {
            ::new (new_buf + i) T(std::move(old[i]));
            old[i].~T();
        }
        if (!is_inline()) free_remote();
        remote_data_ = new_buf;
        remote_cap_  = new_cap;
    }
};

// Deduction guide for the common case.
template <typename T, size_t N>
bool operator==(const SmallVector<T, N>& a, const SmallVector<T, N>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

} // namespace aegis
