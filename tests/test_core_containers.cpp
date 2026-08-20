// tests/test_core_containers.cpp — Unit tests for SmallVector/SparseSet/BitVector/SwissTable.
#include <cassert>
#include <cstdint>
#include <iostream>

#include "core/SmallVector.h"
#include "core/SparseSet.h"
#include "core/BitVector.h"
#include "core/SwissTable.h"

namespace {

using namespace aegis;

int test_smallvector_basic() {
    SmallVector<int, 4> v;
    assert(v.empty());
    for (int i = 0; i < 10; ++i) v.push_back(i);
    assert(v.size() == 10);
    for (int i = 0; i < 10; ++i) assert(v[i] == i);
    v.pop_back();
    assert(v.size() == 9);
    assert(v.back() == 8);
    return 0;
}

int test_smallvector_sbo_stays_inline_when_small() {
    SmallVector<int, 4> v;
    for (int i = 0; i < 4; ++i) v.push_back(i);
    assert(v.capacity() == 4); // still inline
    return 0;
}

int test_sparse_set_epoch() {
    SparseSet s(100);
    assert(s.insert(5));
    assert(s.insert(10));
    assert(!s.insert(5)); // already there
    assert(s.contains(5));
    assert(s.contains(10));
    s.clear(); // O(1)
    assert(!s.contains(5));
    assert(!s.contains(10));
    assert(s.insert(5));
    return 0;
}

int test_bitvector_basic() {
    BitVector b(128);
    b.set(0); b.set(64); b.set(127);
    assert(b.test(0));
    assert(b.test(64));
    assert(b.test(127));
    assert(!b.test(1));
    assert(b.popcount() == 3);
    b.clear(64);
    assert(!b.test(64));
    assert(b.popcount() == 2);
    return 0;
}

int test_swisstable_basic() {
    SwissTable<uint32_t, uint32_t> t;
    for (uint32_t i = 0; i < 100; ++i) t.insert(i, i * 2);
    for (uint32_t i = 0; i < 100; ++i) {
        const uint32_t* v = t.get(i);
        assert(v != nullptr);
        assert(*v == i * 2);
    }
    t.erase(50);
    assert(!t.contains(50));
    return 0;
}

} // namespace

int main() {
    int failures = 0;
    failures += test_smallvector_basic();
    failures += test_smallvector_sbo_stays_inline_when_small();
    failures += test_sparse_set_epoch();
    failures += test_bitvector_basic();
    failures += test_swisstable_basic();
    std::cout << "core_container tests passed (assertions OK)\n";
    return failures;
}
