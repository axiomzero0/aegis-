// aegis/runtime/core/panic.hpp — Panic handler (the only abort path).
#pragma once
#include <cstddef>
namespace aegis::runtime::core {
[[noreturn]] void panic(const char* msg, std::size_t len);
} // namespace aegis::runtime::core
