// core/SmallVector.cpp — nothing concrete needed (header-only template),
// but we provide a translation unit so the symbol appears in the build
// graph and CMake has a real source to depend on.
#include "core/SmallVector.h"

namespace aegis {
// Intentionally empty: SmallVector is header-only.
// This file exists so that:
//   1. The CMake target has a real .cpp file to compile for this module.
//   2. Future explicit template instantiations (SmallVector<NodeId, 2>,
//      SmallVector<NodeId, 4>, etc.) can live here.
} // namespace aegis
