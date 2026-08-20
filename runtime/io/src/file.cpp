// runtime/io/src/file.cpp — File handle (header-only wrapper around syscall.cpp).
//
// The File class in file.hpp wraps a file descriptor. This .cpp is
// mostly a placeholder since the implementation lives in the header
// and in syscall.cpp. It exists so the build has a translation unit.
#include "aegis/runtime/io/file.hpp"
namespace aegis::runtime::io {
// File is fully inline in the header.
} // namespace aegis::runtime::io
