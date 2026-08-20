// runtime/alloc/src/system.cpp — SystemAllocator .cpp (the header is already complete).
//
// This file exists because the build system needs a translation unit
// for the SystemAllocator. The header-only inline implementation in
// system.hpp is sufficient for direct inclusion; this .cpp is empty.
#include "aegis/runtime/alloc/system.hpp"
namespace aegis::runtime::alloc {
// SystemAllocator is fully inline in the header.
} // namespace aegis::runtime::alloc
