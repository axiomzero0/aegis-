// runtime/sync/src/atomic.cpp — Atomic ops translation unit.
//
// The header-only template aegis::runtime::sync::Atomic<T> is complete.
// This .cpp provides explicit instantiations for the common types so
// the symbols exist in the static library (helps the linker).
#include "aegis/runtime/sync/atomic.hpp"

namespace aegis::runtime::sync {
template class Atomic<uint8_t>;
template class Atomic<uint16_t>;
template class Atomic<uint32_t>;
template class Atomic<uint64_t>;
template class Atomic<int8_t>;
template class Atomic<int16_t>;
template class Atomic<int32_t>;
template class Atomic<int64_t>;
template class Atomic<bool>;
} // namespace aegis::runtime::sync
